from os import path
import sys

import importlib.resources
import pickle
import gzip
import re
import argparse

sys.path.append(path.join(path.dirname(__file__), "../.."))
from himbaechel_dbgen.chip import *
from apycula import chipdb

# Bel flags
BEL_FLAG_SIMPLE_IO = 0x100

# Z of the bels
# sync with C++ part!
LUT0_Z  = 0       # z(DFFx) = z(LUTx) + 1
LUT7_Z  = 14
MUX20_Z = 16
MUX21_Z = 18
MUX23_Z = 22
MUX27_Z = 29
ALU0_Z  = 30 # : 35, 6 ALUs
RAMW_Z  = 36 # RAM16SDP4

IOBA_Z  = 50
IOBB_Z  = 51

PLL_Z   = 275
GSR_Z   = 276
VCC_Z   = 277
GND_Z   = 278

# =======================================
# Chipdb additional info
# =======================================
@dataclass
class TileExtraData(BBAStruct):
    tile_class: IdString # The general functionality of the slightly different tiles,
                         # let's say the behavior of LUT+DFF in the tiles are completely identical,
                         # but one of them also contains clock-wire switches,
                         # then we assign them to the same LOGIC class.
    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.tile_class.index)

@dataclass
class BottomIOCnd(BBAStruct):
    wire_a_net: IdString
    wire_b_net: IdString

    def serialise_lists(self, context: str, bba: BBAWriter):
        pass
    def serialise(self, context: str, bba: BBAWriter):
        bba.u32(self.wire_a_net.index)
        bba.u32(self.wire_b_net.index)

@dataclass
class BottomIO(BBAStruct):
    conditions: list[BottomIOCnd] = field(default_factory = list)

    def serialise_lists(self, context: str, bba: BBAWriter):
        bba.label(f"{context}_conditions")
        for i, cnd in enumerate(self.conditions):
            cnd.serialise(f"{context}_cnd{i}", bba)

    def serialise(self, context: str, bba: BBAWriter):
        bba.slice(f"{context}_conditions", len(self.conditions))

@dataclass
class ChipExtraData(BBAStruct):
    strs: StringPool
    bottom_io: BottomIO

    def create_bottom_io(self):
        self.bottom_io = BottomIO()

    def add_bottom_io_cnd(self, net_a: str, net_b: str):
        self.bottom_io.conditions.append(BottomIOCnd(self.strs.id(net_a), self.strs.id(net_b)))

    def serialise_lists(self, context: str, bba: BBAWriter):
        self.bottom_io.serialise_lists(f"{context}_bottom_io", bba)
    def serialise(self, context: str, bba: BBAWriter):
        self.bottom_io.serialise(f"{context}_bottom_io", bba)

created_tiletypes = set()

# u-turn at the rim
uturnlut = {'N': 'S', 'S': 'N', 'E': 'W', 'W': 'E'}
def uturn(db: chipdb, x: int, y: int, wire: str):
    m = re.match(r"([NESW])([128]\d)(\d)", wire)
    if m:
        direction, num, segment = m.groups()
        # wires wrap around the edges
        # assumes 0-based indexes
        if y < 0:
            y = -1 - y
            direction = uturnlut[direction]
        if x < 0:
            x = -1 - x
            direction = uturnlut[direction]
        if y > db.rows - 1:
            y = 2 * db.rows - 1 - y
            direction = uturnlut[direction]
        if x > db.cols - 1:
            x = 2 * db.cols - 1 - x
            direction = uturnlut[direction]
        wire = f'{direction}{num}{segment}'
    return (x, y, wire)

def create_nodes(chip: Chip, db: chipdb):
    # : (x, y)
    dirs = { 'N': (0, -1), 'S': (0, 1), 'W': (-1, 0), 'E': (1, 0) }
    X = db.cols
    Y = db.rows
    global_nodes = {}
    for y in range(Y):
        for x in range(X):
            nodes = []
            tt = chip.tile_type_at(x, y)
            extra_tile_data = tt.extra_data
            # SN and EW
            for i in [1, 2]:
                nodes.append([NodeWire(x, y, f'SN{i}0'),
                        NodeWire(*uturn(db, x, y - 1, f'N1{i}1')),
                        NodeWire(*uturn(db, x, y + 1, f'S1{i}1'))])
                nodes.append([NodeWire(x, y, f'EW{i}0'),
                        NodeWire(*uturn(db, x - 1, y, f'W1{i}1')),
                        NodeWire(*uturn(db, x + 1, y, f'E1{i}1'))])
            for d, offs in dirs.items():
                # 1-hop
                for i in [0, 3]:
                    nodes.append([NodeWire(x, y, f'{d}1{i}0'),
                        NodeWire(*uturn(db, x + offs[0], y + offs[1], f'{d}1{i}1'))])
                # 2-hop
                for i in range(8):
                    nodes.append([NodeWire(x, y, f'{d}2{i}0'),
                        NodeWire(*uturn(db, x + offs[0], y + offs[1], f'{d}2{i}1')),
                        NodeWire(*uturn(db, x + offs[0] * 2, y + offs[1] * 2, f'{d}2{i}2'))])
                # 4-hop
                for i in range(4):
                    nodes.append([NodeWire(x, y, f'{d}8{i}0'),
                        NodeWire(*uturn(db, x + offs[0] * 4, y + offs[1] * 4, f'{d}8{i}4')),
                        NodeWire(*uturn(db, x + offs[0] * 8, y + offs[1] * 8, f'{d}8{i}8'))])
            # I0 for MUX2_LUT8
            if (x < X - 1 and extra_tile_data.tile_class == chip.strs.id('LOGIC')
                    and  chip.tile_type_at(x + 1, y).extra_data.tile_class == chip.strs.id('LOGIC')):
                nodes.append([NodeWire(x, y, 'OF30'),
                             NodeWire(x + 1, y, 'OF3')])

            # ALU
            if extra_tile_data.tile_class == chip.strs.id('LOGIC'):
                # local carry chain
                for i in range(5):
                    nodes.append([NodeWire(x, y, f'COUT{i}'),
                                  NodeWire(x, y, f'CIN{i + 1}')]);
                # gobal carry chain
                if x > 1 and chip.tile_type_at(x - 1, y).extra_data.tile_class == chip.strs.id('LOGIC'):
                    nodes.append([NodeWire(x, y, f'CIN0'),
                                  NodeWire(x - 1, y, f'COUT5')])

            for node in nodes:
                chip.add_node(node)

            # VCC and VSS sources in the all tiles
            global_nodes.setdefault('GND', []).append(NodeWire(x, y, 'VSS'))
            global_nodes.setdefault('VCC', []).append(NodeWire(x, y, 'VCC'))

    # add nodes from the apicula db
    for node_name, node_hdr in db.nodes.items():
        wire_type, node = node_hdr
        for y, x, wire in node:
            if wire_type:
                if not chip.tile_type_at(x, y).has_wire(wire):
                    chip.tile_type_at(x, y).create_wire(wire, wire_type)
                else:
                    chip.tile_type_at(x, y).set_wire_type(wire, wire_type)
            new_node = NodeWire(x, y, wire)
            gl_nodes = global_nodes.setdefault(node_name, [])
            if new_node not in gl_nodes:
                gl_nodes.append(NodeWire(x, y, wire))

    for name, node in global_nodes.items():
        chip.add_node(node)


# About X and Y as parameters - in some cases, the type of manufacturer's tile
# is not different, but some wires are not physically present, that is, routing
# depends on the location of otherwise identical tiles. There are many options
# for taking this into account, but for now we make a distinction here, by
# coordinates.
def create_switch_matrix(tt: TileType, db: chipdb, x: int, y: int):
    def get_wire_type(name):
        if name in {'XD0', 'XD1', 'XD2', 'XD3', 'XD4', 'XD5',}:
            return "X0"
        return ""

    for dst, srcs in db.grid[y][x].pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, get_wire_type(dst))
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, get_wire_type(src))
            tt.create_pip(src, dst)
    # clock wires
    for dst, srcs in db.grid[y][x].pure_clock_pips.items():
        if not tt.has_wire(dst):
            tt.create_wire(dst, "GLOBAL_CLK")
        for src in srcs.keys():
            if not tt.has_wire(src):
                tt.create_wire(src, "GLOBAL_CLK")
            tt.create_pip(src, dst)

def create_null_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "NULL"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))
    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# responsible nodes, there will be IO banks, configuration, etc.
def create_corner_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "CORNER"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    if x == 0 and y == 0:
        # GND is the logic low level generator
        tt.create_wire('VSS', 'GND')
        gnd = tt.create_bel('GND', 'GND', z = GND_Z)
        tt.add_bel_pin(gnd, "G", "VSS", PinType.OUTPUT)
        # VCC is the logic high level generator
        tt.create_wire('VCC', 'VCC')
        gnd = tt.create_bel('VCC', 'VCC', z = VCC_Z)
        tt.add_bel_pin(gnd, "V", "VCC", PinType.OUTPUT)
        # also here may be GSR
        if 'GSR' in db.grid[y][x].bels.keys():
            portmap = db.grid[y][x].bels['GSR'].portmap
            tt.create_wire(portmap['GSRI'], "GSRI")
            io = tt.create_bel("GSR", "GSR", z = GSR_Z)
            tt.add_bel_pin(io, "GSRI", portmap['GSRI'], PinType.INPUT)

    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# Global set/reset. GW2A series has special cell for it
def create_gsr_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "GSR"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    portmap = db.grid[y][x].bels['GSR'].portmap
    tt.create_wire(portmap['GSRI'], "GSRI")
    io = tt.create_bel("GSR", "GSR", z = GSR_Z)
    tt.add_bel_pin(io, "GSRI", portmap['GSRI'], PinType.INPUT)

    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# simple IO - only A and B
def create_io_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "IO"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    simple_io = y in db.simplio_rows and chip.name in {'GW1N-1', 'GW1NZ-1'}
    if simple_io:
        rng = 10
    else:
        rng = 2
    for i in range(rng):
        name = 'IOB' + 'ABCDEFGHIJ'[i]
        # XXX some IOBs excluded from generic chipdb for some reason
        if name not in db.grid[y][x].bels.keys():
            continue
        # wires
        portmap = db.grid[y][x].bels[name].portmap
        tt.create_wire(portmap['I'], "IO_I")
        tt.create_wire(portmap['O'], "IO_O")
        tt.create_wire(portmap['OE'], "IO_OE")
        # bels
        io = tt.create_bel(name, "IOB", z = IOBA_Z + i)
        if simple_io:
            io.flags |= BEL_FLAG_SIMPLE_IO
        tt.add_bel_pin(io, "I", portmap['I'], PinType.INPUT)
        tt.add_bel_pin(io, "OE", portmap['OE'], PinType.INPUT)
        tt.add_bel_pin(io, "O", portmap['O'], PinType.OUTPUT)
        # bottom io
        if 'BOTTOM_IO_PORT_A' in portmap.keys():
            if not tt.has_wire(portmap['BOTTOM_IO_PORT_A']):
                tt.create_wire(portmap['BOTTOM_IO_PORT_A'], "IO_I")
                tt.create_wire(portmap['BOTTOM_IO_PORT_B'], "IO_I")
            tt.add_bel_pin(io, "BOTTOM_IO_PORT_A", portmap['BOTTOM_IO_PORT_A'], PinType.INPUT)
            tt.add_bel_pin(io, "BOTTOM_IO_PORT_B", portmap['BOTTOM_IO_PORT_B'], PinType.INPUT)

    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# logic: luts, dffs, alu etc
def create_logic_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "LOGIC"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    lut_inputs = ['A', 'B', 'C', 'D']
    # setup LUT wires
    for i in range(8):
        for inp_name in lut_inputs:
            tt.create_wire(f"{inp_name}{i}", "LUT_INPUT")
        tt.create_wire(f"F{i}", "LUT_OUT")
        # experimental. the wire is false - it is assumed that DFF is always
        # connected to the LUT's output F{i}, but we can place primitives
        # arbitrarily and create a pass-through LUT afterwards.
        # just out of curiosity
        tt.create_wire(f"XD{i}", "FF_INPUT")
        tt.create_wire(f"Q{i}", "FF_OUT")
    # setup DFF wires
    for j in range(3):
        tt.create_wire(f"CLK{j}", "TILE_CLK")
        tt.create_wire(f"LSR{j}", "TILE_LSR")
        tt.create_wire(f"CE{j}",  "TILE_CE")
    # setup MUX2 wires
    for j in range(8):
        tt.create_wire(f"OF{j}", "MUX_OUT")
        tt.create_wire(f"SEL{j}", "MUX_SEL")
    tt.create_wire("OF30", "MUX_OUT")
    # setup ALU wires
    for j in range(6):
        tt.create_wire(f"CIN{j}", "ALU_CIN")
        tt.create_wire(f"COUT{j}", "ALU_COUT")

    # create logic cells
    for i in range(8):
        # LUT
        lut = tt.create_bel(f"LUT{i}", "LUT4", z = (i * 2 + 0))
        for j, inp_name in enumerate(lut_inputs):
            tt.add_bel_pin(lut, f"I{j}", f"{inp_name}{i}", PinType.INPUT)
        tt.add_bel_pin(lut, "F", f"F{i}", PinType.OUTPUT)
        if i < 6:
            # FF data can come from LUT output, but we pretend that we can use
            # any LUT input
            tt.create_pip(f"F{i}", f"XD{i}")
            for inp_name in lut_inputs:
                tt.create_pip(f"{inp_name}{i}", f"XD{i}")
            # FF
            ff = tt.create_bel(f"DFF{i}", "DFF", z =(i * 2 + 1))
            tt.add_bel_pin(ff, "D", f"XD{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "CLK", f"CLK{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "Q", f"Q{i}", PinType.OUTPUT)
            tt.add_bel_pin(ff, "SET", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "RESET", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "PRESET", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "CLEAR", f"LSR{i // 2}", PinType.INPUT)
            tt.add_bel_pin(ff, "CE", f"CE{i // 2}", PinType.INPUT)

            # ALU
            ff = tt.create_bel(f"ALU{i}", "ALU", z = i + ALU0_Z)
            tt.add_bel_pin(ff, "SUM", f"F{i}", PinType.OUTPUT)
            tt.add_bel_pin(ff, "COUT", f"COUT{i}", PinType.OUTPUT)
            tt.add_bel_pin(ff, "CIN", f"CIN{i}", PinType.INPUT)
            # pinout for the ADDSUB ALU mode
            tt.add_bel_pin(ff, "I0", f"A{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "I1", f"B{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "I2", f"C{i}", PinType.INPUT)
            tt.add_bel_pin(ff, "I3", f"D{i}", PinType.INPUT)

    # wide luts
    for i in range(4):
        ff = tt.create_bel(f"MUX{i * 2}", "MUX2_LUT5", z = MUX20_Z + i * 4)
        tt.add_bel_pin(ff, "I0", f"F{i * 2}", PinType.INPUT)
        tt.add_bel_pin(ff, "I1", f"F{i * 2 + 1}", PinType.INPUT)
        tt.add_bel_pin(ff, "O",  f"OF{i * 2}", PinType.OUTPUT)
        tt.add_bel_pin(ff, "S0", f"SEL{i * 2}", PinType.INPUT)
    for i in range(2):
        ff = tt.create_bel(f"MUX{i * 4 + 1}", "MUX2_LUT6", z = MUX21_Z + i * 8)
        tt.add_bel_pin(ff, "I0", f"OF{i * 4 + 2}", PinType.INPUT)
        tt.add_bel_pin(ff, "I1", f"OF{i * 4}", PinType.INPUT)
        tt.add_bel_pin(ff, "O",  f"OF{i * 4 + 1}", PinType.OUTPUT)
        tt.add_bel_pin(ff, "S0", f"SEL{i * 4 + 1}", PinType.INPUT)
    ff = tt.create_bel(f"MUX3", "MUX2_LUT7", z = MUX23_Z)
    tt.add_bel_pin(ff, "I0", f"OF5", PinType.INPUT)
    tt.add_bel_pin(ff, "I1", f"OF1", PinType.INPUT)
    tt.add_bel_pin(ff, "O",  f"OF3", PinType.OUTPUT)
    tt.add_bel_pin(ff, "S0", f"SEL3", PinType.INPUT)
    ff = tt.create_bel(f"MUX7", "MUX2_LUT8", z = MUX27_Z)
    tt.add_bel_pin(ff, "I0", f"OF30", PinType.INPUT)
    tt.add_bel_pin(ff, "I1", f"OF3", PinType.INPUT)
    tt.add_bel_pin(ff, "O",  f"OF7", PinType.OUTPUT)
    tt.add_bel_pin(ff, "S0", f"SEL7", PinType.INPUT)

    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

def create_ssram_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    # SSRAM is LUT based, so it's logic-like
    ttyp, tt = create_logic_tiletype(chip, db, x, y, ttyp)

    lut_inputs = ['A', 'B', 'C', 'D']
    ff = tt.create_bel(f"RAM16SDP4", "RAM16SDP4", z = RAMW_Z)
    for i in range(4):
        tt.add_bel_pin(ff, f"DI[{i}]", f"{lut_inputs[i]}5", PinType.INPUT)
        tt.add_bel_pin(ff, f"WAD[{i}]", f"{lut_inputs[i]}4", PinType.INPUT)
        # RAD[0] is assumed to be connected to A3, A2, A1 and A0. But
        # for now we connect it only to A0, the others will be connected
        # directly during packing. RAD[1...3] - similarly.
        tt.add_bel_pin(ff, f"RAD[{i}]", f"{lut_inputs[i]}0", PinType.INPUT)
        tt.add_bel_pin(ff, f"DO[{i}]", f"F{i}", PinType.OUTPUT)

    tt.add_bel_pin(ff, f"CLK", "CLK2", PinType.INPUT)
    tt.add_bel_pin(ff, f"CE",  "CE2", PinType.INPUT)
    tt.add_bel_pin(ff, f"WRE", "LSR2", PinType.INPUT)
    return (ttyp, tt)

# PLL main tile
_pll_inputs = {'CLKFB', 'FBDSEL0', 'FBDSEL1', 'FBDSEL2', 'FBDSEL3',
        'FBDSEL4', 'FBDSEL5', 'IDSEL0', 'IDSEL1', 'IDSEL2', 'IDSEL3',
        'IDSEL4', 'IDSEL5', 'ODSEL0', 'ODSEL1', 'ODSEL2', 'ODSEL3',
        'ODSEL4', 'ODSEL5', 'RESET', 'RESET_P', 'PSDA0', 'PSDA1',
        'PSDA2', 'PSDA3', 'DUTYDA0', 'DUTYDA1', 'DUTYDA2', 'DUTYDA3',
        'FDLY0', 'FDLY1', 'FDLY2', 'FDLY3', 'CLKIN'}
_pll_outputs = {'CLKOUT', 'LOCK', 'CLKOUTP', 'CLKOUTD', 'CLKOUTD3'}
def create_pll_tiletype(chip: Chip, db: chipdb, x: int, y: int, ttyp: int):
    if ttyp in created_tiletypes:
        return ttyp, None
    typename = "PLL"
    tt = chip.create_tile_type(f"{typename}_{ttyp}")
    tt.extra_data = TileExtraData(chip.strs.id(typename))

    # wires
    if chip.name == 'GW1NS-4':
        pll_name = 'PLLVR'
        bel_type = 'PLLVR'
    else:
        pll_name = 'RPLLA'
        bel_type = 'rPLL'
    portmap = db.grid[y][x].bels[pll_name].portmap
    pll = tt.create_bel("PLL", bel_type, z = PLL_Z)
    # Not sure how this will affect routing - PLLs are fixed and their outputs
    # will be handled by a dedicated router
    #pll.flags = BEL_FLAG_GLOBAL
    for pin, wire in portmap.items():
        if pin in _pll_inputs:
            tt.create_wire(wire, "PLL_I")
            tt.add_bel_pin(pll, pin, wire, PinType.INPUT)
        else:
            assert pin in _pll_outputs, f"Unknown PLL pin{pin}"
            tt.create_wire(wire, "PLL_O")
            tt.add_bel_pin(pll, pin, wire, PinType.OUTPUT)

    create_switch_matrix(tt, db, x, y)
    return (ttyp, tt)

# pinouts, packages...
_tbrlre = re.compile(r"IO([TBRL])(\d+)(\w)")
def create_packages(chip: Chip, db: chipdb):
    def ioloc_to_tile_bel(ioloc):
        side, num, bel_idx = _tbrlre.match(ioloc).groups()
        if side == 'T':
            row = 0
            col = int(num) - 1
        elif side == 'B':
            row = db.rows - 1
            col = int(num) - 1
        elif side == 'L':
            row = int(num) - 1
            col = 0
        elif side == 'R':
            row = int(num) - 1
            col = db.cols - 1
        return (f'X{col}Y{row}', f'IOB{bel_idx}')

    created_pkgs = set()
    for partno_spd, partdata in db.packages.items():
        pkgname, variant, spd = partdata
        partno = partno_spd.removesuffix(spd) # drop SPEED like 'C7/I6'
        if partno in created_pkgs:
            continue
        created_pkgs.add(partno)
        pkg = chip.create_package(partno)
        for pinno, pininfo in db.pinout[variant][pkgname].items():
            io_loc, cfgs = pininfo
            tile, bel = ioloc_to_tile_bel(io_loc)
            pad_func = ""
            for cfg in cfgs:
                pad_func += cfg + "/"
            pad_func = pad_func.rstrip('/')
            bank = int(db.pin_bank[io_loc])
            pad = pkg.create_pad(pinno, tile, bel, pad_func, bank)

# Extra chip data
def create_extra_data(chip: Chip, db: chipdb):
    chip.extra_data = ChipExtraData(chip.strs, None)
    chip.extra_data.create_bottom_io()
    for net_a, net_b in db.bottom_io[2]:
        chip.extra_data.add_bottom_io_cnd(net_a, net_b)

def main():
    parser = argparse.ArgumentParser(description='Make Gowin BBA')
    parser.add_argument('-d', '--device', required=True)
    parser.add_argument('-o', '--output', default="out.bba")

    args = parser.parse_args()

    device = args.device
    with gzip.open(importlib.resources.files("apycula").joinpath(f"{device}.pickle"), 'rb') as f:
        db = pickle.load(f)

    X = db.cols;
    Y = db.rows;

    ch = Chip("gowin", device, X, Y)

    # Init constant ids
    ch.strs.read_constids(path.join(path.dirname(__file__), "constids.inc"))

    # packages from parntnumbers
    create_packages(ch, db)

    # The manufacturer distinguishes by externally identical tiles, so keep
    # these differences (in case it turns out later that there is a slightly
    # different routing or something like that).
    logic_tiletypes = db.tile_types['C']
    io_tiletypes = db.tile_types['I']
    ssram_tiletypes = {17, 18, 19}
    gsr_tiletypes = {1}
    pll_tiletypes = db.tile_types['P']
    # Setup tile grid
    for x in range(X):
        for y in range(Y):
            ttyp = db.grid[y][x].ttyp
            if (x == 0 or x == X - 1) and (y == 0 or y == Y - 1):
                assert ttyp not in created_tiletypes, "Duplication of corner types"
                ttyp, _ = create_corner_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"CORNER_{ttyp}")
                continue
            if ttyp in gsr_tiletypes:
                ttyp, _ = create_gsr_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"GSR_{ttyp}")
            elif ttyp in logic_tiletypes:
                ttyp, _ = create_logic_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"LOGIC_{ttyp}")
            elif ttyp in ssram_tiletypes:
                ttyp, _ = create_ssram_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"LOGIC_{ttyp}")
            elif ttyp in io_tiletypes:
                ttyp, _ = create_io_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"IO_{ttyp}")
            elif ttyp in pll_tiletypes:
                ttyp, _ = create_pll_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"PLL_{ttyp}")
            else:
                ttyp, _ = create_null_tiletype(ch, db, x, y, ttyp)
                created_tiletypes.add(ttyp)
                ch.set_tile_type(x, y, f"NULL_{ttyp}")

    # Create nodes between tiles
    create_nodes(ch, db)
    create_extra_data(ch, db)
    ch.write_bba(args.output)
if __name__ == '__main__':
    main()