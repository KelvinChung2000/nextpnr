/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2026  The nextpnr Authors
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "clock_assign.h"

#include <algorithm>

#include "Highs.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

// An edge of the channel graph: a net that has taken `from` may go on to take
// `to`, because `to` can be entered from somewhere `from` delivers to.
struct ChannelEdge
{
    int from;
    int to;
};

// A net's use of the network must be one connected structure rooted at its
// source. Expressing that as "a channel may be taken if a predecessor was
// taken" is wrong: channels can enter each other, so two channels connected to
// nothing can justify one another. A ladder network already does this, because
// a channel's reach includes its own entry wires, which are also its
// neighbour's. So connectivity is enforced by flow instead: one unit of supply
// leaves the source per channel taken, each taken channel consumes one unit,
// and flow may only travel along taken channels. A detached group has no
// supply and cannot balance.
struct ClockModel
{
    const Context *ctx;
    const ClockNetwork &network;
    const std::vector<ClockCandidate> &candidates;
    const std::vector<std::vector<ClockSink>> &sinks;

    int num_channels;
    int num_nets;
    std::vector<ChannelEdge> edges;
    // Channels each net can enter directly from its source wire.
    std::vector<std::vector<int>> root_edges;

    Highs highs;
    // Column indices.
    std::vector<std::vector<int>> take;      // take[net][channel]
    std::vector<std::vector<int>> drop;      // drop[net][sink]
    std::vector<std::vector<int>> edge_flow; // edge_flow[net][edge]
    std::vector<std::vector<int>> root_flow; // root_flow[net][root_edges index]

    ClockModel(const Context *ctx, const ClockNetwork &network, const std::vector<ClockCandidate> &candidates,
               const std::vector<std::vector<ClockSink>> &sinks)
            : ctx(ctx), network(network), candidates(candidates), sinks(sinks),
              num_channels(int(network.channels.size())), num_nets(int(candidates.size()))
    {
    }

    bool enterable_from(int channel, const pool<WireId> &from) const
    {
        for (WireId entry : network.channels.at(channel).entries)
            if (from.count(entry))
                return true;
        return false;
    }

    void build_graph()
    {
        for (int from = 0; from < num_channels; from++)
            for (int to = 0; to < num_channels; to++)
                if (from != to && enterable_from(to, network.reach.at(from)))
                    edges.push_back(ChannelEdge{from, to});

        root_edges.resize(num_nets);
        for (int n = 0; n < num_nets; n++) {
            WireId src = ctx->getNetinfoSourceWire(candidates.at(n).net);
            for (int c = 0; c < num_channels; c++)
                for (WireId entry : network.channels.at(c).entries)
                    if (entry == src) {
                        root_edges.at(n).push_back(c);
                        break;
                    }
        }
    }

    int add_binary()
    {
        int col = highs.getNumCol();
        highs.addVar(0.0, 1.0);
        highs.changeColIntegrality(col, HighsVarType::kInteger);
        return col;
    }

    int add_flow()
    {
        int col = highs.getNumCol();
        highs.addVar(0.0, double(num_channels));
        return col;
    }

    void add_row(double lower, double upper, const std::vector<int> &cols, const std::vector<double> &values)
    {
        highs.addRow(lower, upper, int(cols.size()), cols.data(), values.data());
    }

    void build()
    {
        build_graph();
        highs.setOptionValue("output_flag", false);
        // A single thread makes a run reproducible; the tie-break in the final
        // objective narrows the remaining choice between equal-cost solutions.
        highs.setOptionValue("threads", 1);
        highs.setOptionValue("random_seed", 0);

        take.resize(num_nets);
        drop.resize(num_nets);
        edge_flow.resize(num_nets);
        root_flow.resize(num_nets);
        for (int n = 0; n < num_nets; n++) {
            for (int c = 0; c < num_channels; c++)
                take.at(n).push_back(add_binary());
            for (size_t s = 0; s < sinks.at(n).size(); s++)
                drop.at(n).push_back(add_binary());
            for (size_t e = 0; e < edges.size(); e++)
                edge_flow.at(n).push_back(add_flow());
            for (size_t e = 0; e < root_edges.at(n).size(); e++)
                root_flow.at(n).push_back(add_flow());
        }

        add_capacity_rows();
        add_coverage_rows();
        add_flow_rows();
    }

    // A channel carries at most one net. Contention on dedicated clock
    // resources is exclusive, not shared.
    void add_capacity_rows()
    {
        for (int c = 0; c < num_channels; c++) {
            std::vector<int> cols;
            std::vector<double> values;
            for (int n = 0; n < num_nets; n++) {
                cols.push_back(take.at(n).at(c));
                values.push_back(1.0);
            }
            add_row(0.0, 1.0, cols, values);
        }
    }

    // Each sink is either reached by a channel this net takes, or dropped.
    // Dropping is always allowed, so the model is never infeasible and the
    // solution says which sinks fell off rather than just failing.
    void add_coverage_rows()
    {
        for (int n = 0; n < num_nets; n++) {
            for (size_t s = 0; s < sinks.at(n).size(); s++) {
                std::vector<int> cols;
                std::vector<double> values;
                for (int c = 0; c < num_channels; c++) {
                    if (!network.reach.at(c).count(sinks.at(n).at(s).wire))
                        continue;
                    cols.push_back(take.at(n).at(c));
                    values.push_back(1.0);
                }
                cols.push_back(drop.at(n).at(s));
                values.push_back(1.0);
                add_row(1.0, kHighsInf, cols, values);
            }
        }
    }

    void add_flow_rows()
    {
        double big = double(num_channels);
        for (int n = 0; n < num_nets; n++) {
            // Flow only moves between channels this net has taken.
            for (size_t e = 0; e < edges.size(); e++) {
                add_row(-kHighsInf, 0.0, {edge_flow.at(n).at(e), take.at(n).at(edges.at(e).from)}, {1.0, -big});
                add_row(-kHighsInf, 0.0, {edge_flow.at(n).at(e), take.at(n).at(edges.at(e).to)}, {1.0, -big});
            }
            for (size_t e = 0; e < root_edges.at(n).size(); e++)
                add_row(-kHighsInf, 0.0, {root_flow.at(n).at(e), take.at(n).at(root_edges.at(n).at(e))}, {1.0, -big});

            // Every taken channel consumes exactly one unit.
            for (int c = 0; c < num_channels; c++) {
                std::vector<int> cols;
                std::vector<double> values;
                for (size_t e = 0; e < edges.size(); e++) {
                    if (edges.at(e).to == c) {
                        cols.push_back(edge_flow.at(n).at(e));
                        values.push_back(1.0);
                    } else if (edges.at(e).from == c) {
                        cols.push_back(edge_flow.at(n).at(e));
                        values.push_back(-1.0);
                    }
                }
                for (size_t e = 0; e < root_edges.at(n).size(); e++)
                    if (root_edges.at(n).at(e) == c) {
                        cols.push_back(root_flow.at(n).at(e));
                        values.push_back(1.0);
                    }
                cols.push_back(take.at(n).at(c));
                values.push_back(-1.0);
                add_row(0.0, 0.0, cols, values);
            }

            // The source supplies exactly as much as the taken channels consume.
            std::vector<int> cols;
            std::vector<double> values;
            for (size_t e = 0; e < root_edges.at(n).size(); e++) {
                cols.push_back(root_flow.at(n).at(e));
                values.push_back(1.0);
            }
            for (int c = 0; c < num_channels; c++) {
                cols.push_back(take.at(n).at(c));
                values.push_back(-1.0);
            }
            add_row(0.0, 0.0, cols, values);
        }
    }

    void clear_objective()
    {
        for (int col = 0; col < highs.getNumCol(); col++)
            highs.changeColCost(col, 0.0);
    }

    void solve(const char *what)
    {
        HighsStatus status = highs.run();
        if (status != HighsStatus::kOk && status != HighsStatus::kWarning)
            log_error("clock channel assignment (%s) failed: HiGHS returned status %d\n", what, int(status));
        if (highs.getModelStatus() != HighsModelStatus::kOptimal)
            log_error("clock channel assignment (%s) did not reach an optimum: %s\n", what,
                      highs.modelStatusToString(highs.getModelStatus()).c_str());
    }

    // Strict priority, one class at a time: minimise the sinks dropped in this
    // class, then hold that result while the next class is optimised. Doing it
    // with one weighted objective instead would need weights separated by
    // orders of magnitude, which a solver's tolerances cannot be trusted to
    // respect.
    void solve_by_priority()
    {
        std::vector<int> priorities;
        for (const ClockCandidate &candidate : candidates)
            priorities.push_back(candidate.priority);
        std::sort(priorities.begin(), priorities.end(), std::greater<int>());
        priorities.erase(std::unique(priorities.begin(), priorities.end()), priorities.end());

        for (int priority : priorities) {
            std::vector<int> cols;
            std::vector<double> values;
            for (int n = 0; n < num_nets; n++) {
                if (candidates.at(n).priority != priority)
                    continue;
                for (int col : drop.at(n)) {
                    cols.push_back(col);
                    values.push_back(1.0);
                }
            }
            if (cols.empty())
                continue;

            clear_objective();
            for (size_t i = 0; i < cols.size(); i++)
                highs.changeColCost(cols.at(i), 1.0);
            solve("priority class");

            double dropped = 0.0;
            for (int col : cols)
                dropped += highs.getSolution().col_value.at(col);
            // Hold this class at its best while lower classes are optimised.
            add_row(-kHighsInf, std::round(dropped), cols, values);
        }
    }

    // With coverage settled, use as few channels as possible, and break ties
    // towards the lowest channel indices so the result does not depend on
    // which of several equal solutions the solver happened to find.
    void solve_channel_usage()
    {
        clear_objective();
        double unit = double(num_nets * num_channels) * double(num_nets * num_channels) + 1.0;
        for (int n = 0; n < num_nets; n++)
            for (int c = 0; c < num_channels; c++)
                highs.changeColCost(take.at(n).at(c), unit + double(n * num_channels + c));
        solve("channel usage");
    }

    ClockAssignment result() const
    {
        ClockAssignment assignment;
        assignment.channels.resize(num_nets);
        assignment.dropped.resize(num_nets);
        const std::vector<double> &values = highs.getSolution().col_value;
        for (int n = 0; n < num_nets; n++) {
            for (int c = 0; c < num_channels; c++)
                if (values.at(take.at(n).at(c)) > 0.5)
                    assignment.channels.at(n).push_back(c);
            for (size_t s = 0; s < sinks.at(n).size(); s++)
                if (values.at(drop.at(n).at(s)) > 0.5)
                    assignment.dropped.at(n).insert(sinks.at(n).at(s).wire);
        }
        return assignment;
    }
};

} // namespace

ClockAssignment assign_clock_channels(const Context *ctx, const ClockNetwork &network,
                                      const std::vector<ClockCandidate> &candidates,
                                      const std::vector<std::vector<ClockSink>> &sinks)
{
    NPNR_ASSERT(candidates.size() == sinks.size());
    ClockModel model(ctx, network, candidates, sinks);
    model.build();
    model.solve_by_priority();
    model.solve_channel_usage();
    return model.result();
}

NEXTPNR_NAMESPACE_END
