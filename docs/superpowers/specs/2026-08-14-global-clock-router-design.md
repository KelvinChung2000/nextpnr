# Global clock/signal router for FABulous fabrics

**Status:** design approved, ready for planning
**Date:** 2026-08-14
**Base:** nextpnr `upstream/main` @ `4d235150`

## Problem

FABulous fabrics support exactly one clock today. The himbaechel chipdb generator emits a
per-tile `CLK_DRV` BEL driving a local `user_clk_o`, and the example fabric wires tiles into
a daisy chain (`CLK_PREV -> CLK`) whose own source comments call it *"not intended to be a
sensible clock structure"*. There is no way to express a second clock domain, and no way to
put a high-fanout reset or enable onto dedicated resources.

We want multiple independent clock domains, on a clock network the fabric generator can
size, with high-fanout control signals (reset/enable) able to use spare capacity.

nextpnr will not help here. `common/route/` contains only `router1`/`router2`, neither of
which has any concept of a clock; the sole global-net awareness in shared code is
`#ifdef ARCH_ECP5 if (net->is_global) return true;`. Every architecture that routes clocks
does so in a private pass — nine independent implementations, with `machxo2` and `mistral`
byte-identical. This is deliberate: `docs/coding.md:19` states architectures implement
"specialized parts of placement and routing such as global clock networks" themselves.

## Goals

- Multiple independent clock domains, count parameterised by the fabric.
- Two network topologies as fabric-generator options: **ladder** (small fabrics) and
  **mesh** (large fabrics), served by **one** router.
- High-fanout reset/enable may occupy spare channels.
- Oversubscription is always diagnosed, never silent.

**Both topologies are committed scope.** The finished router must handle ladder *and* mesh.
Delivery is staged (ladder first, M1–M2) because it validates the model and the router2
contract cheaply, but a ladder-only result is not a completed project.

The defining acceptance criterion follows directly:

> The same router binary routes a ladder fabric and a mesh fabric, with **only chipdb data
> differing** — no recompilation, no topology flag, no branch in the router keyed on
> topology.

If that fails, the model is wrong, not the fabric.

## Non-goals

- **Skew optimisation.** Skew is reported through the existing STA (`timing.cc` already
  models launch/capture skew via `with_clock_skew`), not optimised. No tree balancing, no
  useful-skew scheduling.
- **Clock-aware placement.** Placement is an input; we do not feed capacity pressure back.
- **FABulous fabric generation, HDL, or bitstream.** Separate specs.
- **Upstreaming.** Build first, propose later with evidence.

## Prior art

| Source | Relevance |
|---|---|
| Fraisse, Joshi, Gaitonde, Kaviani, *"Boolean Satisfiability-Based Routing and Its Application to Xilinx UltraScale Clock Network"*, FPGA'16 pp. 74–79 | Xilinx's own architects formulate clock routing as SAT and report it dramatically beating conventional routing. Confirms this is a different problem class from signal routing. |
| ISPD 2017 Clock-Aware FPGA Placement Contest | UltraScale capacity model worth copying: 5×8 = 40 clock regions, 24 clock usages per region, all loads of a clock confined to a rectangle of regions where **empty regions in the rectangle still consume a slot**. |
| VTR `--clock_modeling ideal\|route\|dedicated_network`, `<clocknetworks>` arch tag | Working open-source precedent: architecture-described clock networks with two-stage routing (source→root, root→sinks). The closest thing to what we want, and a natural staging model. |
| [fpga-interchange-schema#34](https://github.com/chipsalliance/fpga-interchange-schema/issues/34) | The open-source world hit our schema problem and left it **unresolved**. Identifies three tiers — route (buffer→root), distribute (root→columns), column globals (→endpoints). |

All four describe fixed commercial silicon. Our fabric is generated and parameterisable, so
root selection and region budgets become *inputs* rather than constants. That is the novel
part.

## Why the general router cannot do this

Not skew — resource ownership and structure:

1. **A net should occupy a minimal channel set** (exactly one on a ladder). router2 routes
   each sink independently and would consume much of the network for a single clock,
   defeating the entire point.
2. **Contention is binary.** A channel is owned by exactly one net across its span.
   Negotiated congestion converges badly on hard exclusivity; assignment/colouring is native.
3. **Two-way exclusion.** Clock pins may be reachable only via tap muxes, and general
   signals must be kept off those muxes.
4. **Root selection** is a placement-dependent global decision per net, with no analogue in
   signal routing.
5. **Fanout scale.** Per-arc A* with ripup over thousands of sinks is wasteful when the
   structure means resources are *selected*, not searched.

## Core model

A network is a set of **channels**. A channel is one independently-assignable distribution
path with three properties:

- **entry set** — what may drive it (dedicated clock source now; arbitrary fabric logic via
  injection points later),
- **reach** — which sinks it can serve,
- **resource set** — the wires and pips to bind when it is used.

This single abstraction covers both topologies:

- **Ladder:** N channels, each with reach = whole fabric. A net needs exactly one.
- **Mesh:** channels per region plus tier structure. A net needs a *set* — a route-tier
  channel to the root, plus a distribute channel per region containing sinks.

The general problem is therefore **minimum channel-set cover per net under a
one-net-per-channel capacity constraint**. The ladder is the degenerate case where any
single channel covers everything, collapsing the cover to a sort. The mesh is where it
becomes a real optimisation, and the only place an ILP would ever be warranted.

**Topology lives in the data, not the code.** The router asks "which channels reach my
sinks, and are they free?" — never "am I a ladder or a mesh?"

## Mesh specifics

The ladder exercises steps 1–5 with a trivial cover. The mesh adds three things, and they
are the only places the two topologies differ — all of them in data or in step 3.

**Tiers.** Following the structure identified in fpga-interchange#34, a channel carries a
tier: `ROUTE` (source→root), `DISTRIBUTE` (root→region), `LEAF` (within region→taps). A
ladder declares a single `LEAF`-tier channel per ladder with fabric-wide reach; nothing in
the router special-cases that.

**Root selection.** A net's root is chosen from the roots its `ROUTE` channels can reach,
scoring by the sink centroid — pick the legal root minimising total distribution cost to the
regions containing sinks. Ties break deterministically by region index, never by iteration
order (the ice40 lesson). Root selection is per-net and made once, before cover.

**Region capacity.** Each region declares a channel budget. We adopt the ISPD 2017 model as
the default because it is validated and makes our fabrics comparable to published
benchmarks: a net's regions form a bounding rectangle, and **every region in that rectangle
consumes a slot even if it holds no sink**. The rectangle rule is a fabric-declared property,
not a hardcoded assumption — a fabric whose distribution does not work that way declares
per-region occupancy instead, and only the capacity check changes.

Cover then becomes: choose a root, then select the `DISTRIBUTE`/`LEAF` channels covering all
sink regions, subject to per-region budgets. Greedy in priority order first. An ILP is
considered **only if measurement shows greedy failing on realistic fabrics** — the FPGA'16
SAT result suggests exact methods pay off at commercial scale, but that is not evidence for
our scale, and Gurobi is available if it becomes warranted.

## Pipeline

1. **Discover** — an adapter builds the channel set. Hand-constructed in tests now;
   chipdb-backed later. The chipdb encoding is deliberately deferred: himbaechel's core
   schema already has unused generic extension points (`GroupDataPOD` with `group_type` +
   `group_wires`/`group_pips`, and `TileWireDataPOD.flags`, documented as "32 bits of
   arbitrary data"), so no upstream schema change will be needed.
2. **Classify and rank** — identify nets whose sinks are tap-reachable. Clocks outrank
   control signals; ties broken by fanout.
3. **Assign** — in priority order, select a minimal covering channel set subject to capacity.
4. **Bind** — wires and pips at `STRENGTH_LOCKED`; reserve tap muxes against the general router.
5. **Report** — per-channel occupancy, and every net or sink that did not get on the network.

## Integration with router2 (verified)

router2 already has the exact hook we need, and no core change is required.

At setup (`router2.cc:242-249`) it scans every net and calls `check_arc_routing`; any arc
already bound along a complete src→sink path is passed to `record_prerouted_net`, which
marks it `pre_routed` and adopts the existing binding. Pre-routed arcs are then skipped by
reservation (`:649`) and by the main routing loop (`:710`).

**Our contract is therefore exactly: bind a complete src→sink path for every assigned arc
before router2 runs.** Nothing else.

Note `4d235150` ("router2: Fix reservation around pre-routed nets", 2026-08-13) repaired
reservation handling for precisely this case — this design should be built on that commit or
later, and M1 should confirm the behaviour rather than assume it.

## Diagnostics

A hard requirement, not polish. Measured on nextpnr `0.11.1-4` built from master: a 12
clock-domain design on hx8k promoted 8 clocks and silently dropped 4 to general routing with
`1 warning, 0 errors` — and that warning was about a missing PCF. All 12 nets had identical
fanout, so the choice of losers came down to map iteration order.

Requirements:
- Every net or sink not placed on the network is logged explicitly, with the reason.
- Falling back to general routing stays legal but is a *reported decision*.
- A capacity summary is emitted whenever the network is used.

## Milestones

All four are in scope. Staging is about de-risking order, not about what ships.

| | Scope | Outcome |
|---|---|---|
| **M1** | Single channel, ladder | Reproduces today's behaviour; proves plumbing and the router2 pre-routed contract. |
| **M2** | N channels, ladder | Multi-domain works. First useful increment. |
| **M3** | Reset/enable eligibility | Spare channels carry high-fanout control. |
| **M4** | Regions / mesh | Multi-tier set cover with root selection. **Completes the project.** |

M4 is not blocked on FABulous generating a mesh: the discovery adapter is an interface, so
mesh is developed and tested against a hand-constructed mesh network long before the fabric
generator can emit one. That decoupling is why mesh can be committed scope now.

## Testing

Parameterised over (topology, channel count, net count, fanout) so mesh cases reuse ladder
cases. Targeted, not coverage-driven. Two assertions matter most:

- Capacity is never exceeded — no channel is ever bound to two nets.
- **Oversubscription always produces a diagnostic** — the ice40 silent-drop behaviour
  written as a regression test from day one.

Plus: a net assigned to a channel is fully bound src→sink, and router2 subsequently reports
it as pre-routed rather than rerouting it.

The acceptance criterion gets its own test: **the same test body, run against a ladder
network and a mesh network, differing only in the channel data handed to the discovery
adapter.** Parameterising topology as test input rather than writing two test suites is what
keeps the "one router" claim honest — if a topology-specific branch ever creeps into the
router, this test is what catches it.

## Implementation status

Built in `common/route/clock_network.{h,cc}` and `common/route/clock_router.{h,cc}`, with tests
in `generic/tests/clock_router.cc` parameterised over ladder and mesh.

Done:

- The channel model and its derived reach index.
- Assignment as a minimum channel-set cover, found by breadth-first search over channels. The
  search handles transit channels — a mesh trunk covers no sink itself — which a
  coverage-scoring greedy cannot. Ladder and mesh differ only in data.
- Binding at `STRENGTH_LOCKED`, and the router2 pre-routed contract, confirmed end to end on
  `4d235150`: router2 leaves the bound tree untouched and completes the remaining sinks.
- Per-sink granularity: a net whose sinks are not all reachable keeps the network for the ones
  that are.
- Diagnostics: status, reason, and the unreached sinks by cell and port.

Not yet built, all of it M4 detail rather than structure:

- **Region budgets.** `ClockChannel::region` is carried but no budget is enforced, so the ISPD
  rectangle rule is not in the code. The mesh tests pass without ever binding a budget.
- **Root selection by sink centroid.** Roots are currently whichever the chain search reaches
  first. The test mesh has symmetric roots, so it cannot tell the two apart.
- **Discovery from a chipdb.** Channels are hand-constructed by the caller.
- **Tap-mux reservation** (pipeline step 4). Locked binding already keeps the general router off
  bound wires; explicit reservation of unused tap muxes only matters once a real fabric puts
  general pips next to taps, and is untestable before then.
- **The invocation point**, below.

## Open questions

- Where the module physically lives, and its invocation point. Not `preRoute()` — that runs
  *inside* `Arch::route()` (`himbaechel/arch.cc:294`), so it is "the first thing the router
  does", not a peer stage. A true stage would sit between `ctx->place()` and `ctx->route()`
  (`common/kernel/command.cc:655-668`), which is a core change. Deferred; the module will be
  self-contained with a clean interface so the call site can move.
- Ranking policy when a clock and a very high-fanout reset compete for the last channel.
- Whether greedy cover suffices at M4, or an ILP is warranted. Deliberately left to
  measurement rather than decided up front.
