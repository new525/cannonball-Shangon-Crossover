#!/usr/bin/env python3
"""gen.py: 68000 per-instruction timing tests, for MAME 0.264 (mame_run.lua) and Musashi (mtime.c).

usage: gen.py <out dir>   writes <out>/batches.bin and <out>/tests.json

Every legal 68000 opcode word (and the illegal ones, line A and line F, which take an exception) is
run at least once, with every addressing mode its bits select, plus data-dependent variants (shift
and rotate counts, bit numbers, MUL/DIV operands, conditions, DBcc cases, MOVEM register counts,
CHK bounds, TRAPV). The tests run on Hang-On's sub CPU in MAME, whose 19-bit address bus puts its
16 KB of RAM at $7C000-$7FFFF and also at every abs.W address from $C000 up.

RAM layout of a batch (one batch fills the RAM and runs in about a frame):
  $7C000-$7EBFF  test code: per test [pre-writes][continuation][MOVEM template][overrides][SR][test]
  $7EE00         register template (D0-D7, A0-A7), loaded by MOVEM before every test
  $7EE40         continuation address for the exception handler stub
  $7EE50         done flag (1 = batch finished, 2 = an unexpected exception)
  $7EE80         exception handler stub: every vector points here; it jumps to the continuation
  $7EEA0         abort routine (continuation when no exception was expected)
  $7F000-$7FFFF  data: every memory operand lands here; stack at $7FC00
A batch ends with an address error with an odd stack pointer: MAME's 68000 double-faults and
stops tracing until the next batch is loaded.
"""
import json, os, random, struct, sys

RAM_LO, RAM_HI = 0x7c000, 0x80000
CODE_HI = 0x7ec00
TEMPLATE, CONT, DONE, HND, ABORT = 0x7ee00, 0x7ee40, 0x7ee50, 0x7ee80, 0x7eea0
ABORT_FAULT = ABORT + 10
DATA_LO = 0x7f000
SP = 0xfffffc00
TPL = [0x10 * (i + 1) for i in range(8)] + [0xfffff800 + 0x40 * i for i in range(7)] + [SP]

M32 = 0xffffffff
def phys(a): return a & 0x7ffff
def s8(v): v &= 0xff; return v - 0x100 if v & 0x80 else v
def s16(v): v &= 0xffff; return v - 0x10000 if v & 0x8000 else v

SZ = {1: 'b', 2: 'w', 4: 'l'}

# ---------------------------------------------------------------------------------------------
# decoder: returns a dict or None (illegal). Operands are (mode, reg) pairs with mode 7 split
# into 7..11 for abs.W, abs.L, (d16,PC), (d8,PC,Xn), #imm.
# ---------------------------------------------------------------------------------------------
def ea(op):
    m, r = (op >> 3) & 7, op & 7
    if m == 7:
        if r > 4: return None
        return (7 + r, 0)
    return (m, r)

def decode(op):
    hi = op >> 12
    d = dict(op=op, size=0, src=None, dst=None, pre=[], kind='plain', name='?')
    e = ea(op)
    if hi == 0:
        if op & 0x100:
            if ((op >> 3) & 7) == 1:
                om = (op >> 6) & 7
                d.update(name='movep', size=4 if om & 1 else 2, kind='movep', dir=om & 2, dx=(op >> 9) & 7, ay=op & 7)
                d['pre'] = [('disp16', 0x10)]
                return d
            t = (op >> 6) & 3
            d.update(name=['btst', 'bchg', 'bclr', 'bset'][t], kind='bitdyn', bitreg=(op >> 9) & 7, dst=e)
            d['size'] = 4 if e and e[0] == 0 else 1
            return d
        t = (op >> 9) & 7
        if t == 4:
            tt = (op >> 6) & 3
            d.update(name=['btst', 'bchg', 'bclr', 'bset'][tt], kind='bitimm', dst=e)
            d['size'] = 4 if e and e[0] == 0 else 1
            return d
        name = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}.get(t)
        s = (op >> 6) & 3
        if (op & 0x3f) == 0x3c and t in (0, 1, 5) and s < 2:
            d.update(name=name + ('_ccr' if s == 0 else '_sr'), kind='srimm', size=1 if s == 0 else 2)
            return d
        size = {0: 1, 1: 2, 2: 4}.get(s)
        d.update(name=name, size=size, dst=e, kind='imm')
        return d
    if hi in (1, 2, 3):
        size = {1: 1, 3: 2, 2: 4}[hi]
        dm, dr = (op >> 6) & 7, (op >> 9) & 7
        dst = (7 + dr, 0) if dm == 7 else (dm, dr)
        d.update(name='movea' if dm == 1 else 'move', size=size, src=e, dst=dst)
        return d
    if hi == 4:
        if op & 0x100:
            if ((op >> 6) & 7) == 7:
                d.update(name='lea', size=4, src=e, dst=(1, (op >> 9) & 7), kind='lea')
                return d
            d.update(name='chk', size=2, src=e, dst=(0, (op >> 9) & 7), kind='chk')
            return d
        sub = (op >> 8) & 0xf
        s = (op >> 6) & 3
        if sub in (0, 2, 4, 6):
            if s == 3:
                if sub == 0: d.update(name='move_fr_sr', size=2, dst=e); return d
                if sub == 4: d.update(name='move_to_ccr', size=2, src=e, kind='toccr'); return d
                if sub == 6: d.update(name='move_to_sr', size=2, src=e, kind='tosr'); return d
                return None
            d.update(name={0: 'negx', 2: 'clr', 4: 'neg', 6: 'not'}[sub], size={0: 1, 1: 2, 2: 4}[s], dst=e)
            return d
        if sub == 8:
            if s == 0: d.update(name='nbcd', size=1, dst=e); return d
            if s == 1:
                if e[0] == 0: d.update(name='swap', size=4, dst=e); return d
                d.update(name='pea', size=4, src=e, kind='lea'); return d
            if e[0] == 0: d.update(name='ext', size=2 if s == 2 else 4, dst=e); return d
            d.update(name='movem_re', size=2 if s == 2 else 4, dst=e, kind='movem'); return d
        if sub == 0xa:
            if op == 0x4afc: return None
            if s == 3: d.update(name='tas', size=1, dst=e); return d
            d.update(name='tst', size={0: 1, 1: 2, 2: 4}[s], src=e); return d
        if sub == 0xc:
            d.update(name='movem_er', size=2 if s == 2 else 4, src=e, kind='movem'); return d
        if sub == 0xe:
            if s == 1:
                m, r = (op >> 3) & 7, op & 7
                if m < 2: d.update(name='trap', kind='trap', vec=op & 15); return d
                if m == 2: d.update(name='link', kind='link', ar=r); d['pre'] = [('w', 0xfff8)]; return d
                if m == 3: d.update(name='unlk', ar=r); return d
                if m == 4: d.update(name='move_to_usp', ar=r, kind='priv'); return d
                if m == 5: d.update(name='move_fr_usp', ar=r, kind='priv'); return d
                if m == 6:
                    nm = ['reset', 'nop', 'stop', 'rte', 'rtd', 'rts', 'trapv', 'rtr'][r]
                    d.update(name=nm, kind={'reset': 'priv', 'stop': 'stop', 'rte': 'rte', 'rts': 'rts', 'trapv': 'trapv', 'rtr': 'rtr'}.get(nm, 'plain'))
                    return d
                return None
            if s == 2: d.update(name='jsr', src=e, kind='jump'); return d
            if s == 3: d.update(name='jmp', src=e, kind='jump'); return d
        return None
    if hi == 5:
        if ((op >> 6) & 3) == 3:
            c = (op >> 8) & 15
            if ((op >> 3) & 7) == 1:
                d.update(name='dbcc', kind='dbcc', cond=c, dn=op & 7, size=2)
                return d
            d.update(name='scc', kind='scc', cond=c, size=1, dst=e)
            return d
        d.update(name='subq' if op & 0x100 else 'addq', size={0: 1, 1: 2, 2: 4}[(op >> 6) & 3], dst=e)
        return d
    if hi == 6:
        c, d8 = (op >> 8) & 15, op & 0xff
        d.update(name='bra' if c == 0 else 'bsr' if c == 1 else 'bcc', kind='bcc', cond=c, d8=d8)
        if d8 == 0: d['pre'] = [('w', 2)]
        return d
    if hi == 7:
        d.update(name='moveq', size=4, dst=(0, (op >> 9) & 7))
        return d
    om, dn = (op >> 6) & 7, (op >> 9) & 7
    mode = (op >> 3) & 7
    if hi == 8:
        if om == 3: d.update(name='divu', size=2, src=e, dst=(0, dn), kind='div'); return d
        if om == 7: d.update(name='divs', size=2, src=e, dst=(0, dn), kind='div'); return d
        if om == 4 and mode < 2:
            d.update(name='sbcd', size=1, src=(mode and 4, op & 7), dst=(mode and 4, dn)); return d
        if om < 3: d.update(name='or', size=1 << om, src=e, dst=(0, dn)); return d
        d.update(name='or', size=1 << (om - 4), src=(0, dn), dst=e); return d
    if hi in (9, 0xd):
        nm = 'sub' if hi == 9 else 'add'
        if om in (3, 7): d.update(name=nm + 'a', size=2 if om == 3 else 4, src=e, dst=(1, dn)); return d
        if om >= 4 and mode < 2:
            d.update(name=nm + 'x', size=1 << (om - 4), src=(mode and 4, op & 7), dst=(mode and 4, dn)); return d
        if om < 3: d.update(name=nm, size=1 << om, src=e, dst=(0, dn)); return d
        d.update(name=nm, size=1 << (om - 4), src=(0, dn), dst=e); return d
    if hi == 0xb:
        if om < 3: d.update(name='cmp', size=1 << om, src=e, dst=(0, dn)); return d
        if om in (3, 7): d.update(name='cmpa', size=2 if om == 3 else 4, src=e, dst=(1, dn)); return d
        if mode == 1: d.update(name='cmpm', size=1 << (om - 4), src=(3, op & 7), dst=(3, dn)); return d
        d.update(name='eor', size=1 << (om - 4), src=(0, dn), dst=e); return d
    if hi == 0xc:
        if om == 3: d.update(name='mulu', size=2, src=e, dst=(0, dn), kind='mul'); return d
        if om == 7: d.update(name='muls', size=2, src=e, dst=(0, dn), kind='mul'); return d
        if om == 4 and mode < 2:
            d.update(name='abcd', size=1, src=(mode and 4, op & 7), dst=(mode and 4, dn)); return d
        if om == 5 and mode == 0: d.update(name='exg', size=4, src=(0, op & 7), dst=(0, dn)); return d
        if om == 5 and mode == 1: d.update(name='exg', size=4, src=(1, op & 7), dst=(1, dn)); return d
        if om == 6 and mode == 1: d.update(name='exg', size=4, src=(1, op & 7), dst=(0, dn)); return d
        if om < 3: d.update(name='and', size=1 << om, src=e, dst=(0, dn)); return d
        d.update(name='and', size=1 << (om - 4), src=(0, dn), dst=e); return d
    if hi == 0xe:
        s = (op >> 6) & 3
        if s == 3:
            t = (op >> 9) & 3
            d.update(name=['as', 'ls', 'rox', 'ro'][t] + ('l' if op & 0x100 else 'r'), size=2, dst=e, kind='shiftmem')
            return d
        t = (op >> 3) & 3
        d.update(name=['as', 'ls', 'rox', 'ro'][t] + ('l' if op & 0x100 else 'r'), size={0: 1, 1: 2, 2: 4}[s],
                 dst=(0, op & 7), kind='shiftreg' if op & 0x20 else 'shiftimm', cnt=(op >> 9) & 7)
        return d
    return None

MODES = ['Dn', 'An', '(An)', '(An)+', '-(An)', '(d16,An)', '(d8,An,Xn)', 'abs.W', 'abs.L', '(d16,PC)', '(d8,PC,Xn)', '#imm']
def ext_words(mode): return {5: 1, 6: 1, 7: 1, 8: 2, 9: 1, 10: 1}.get(mode, 0)

def regs_of(d):
    """registers the instruction names in its opcode (D0-D7 = 0-7, A0-A7 = 8-15)"""
    u = set()
    for o in (d['src'], d['dst']):
        if o and o[0] < 7: u.add(o[1] + (8 if o[0] >= 1 else 0))
    for k, base in (('bitreg', 0), ('dx', 0), ('dn', 0), ('ay', 8), ('ar', 8)):
        if k in d: u.add(d[k] + base)
    if d['kind'] == 'shiftreg': u.add(d['cnt'])
    return u

# ---------------------------------------------------------------------------------------------
# test cases
# ---------------------------------------------------------------------------------------------
class Case:
    def __init__(self, d, tag='', sr=0x2700, ovr=None, mem=None, expect='next', layout='plain', pre=None, immval=None, srcval=None):
        self.d, self.tag, self.sr = d, tag, sr
        self.ovr = dict(ovr or {})
        self.mem = list(mem or [])        # (operand index 0 src / 1 dst, size, value): written at that operand's address
        self.stack = []                   # (offset from SP, size, value or None for the continuation)
        self.expect, self.layout = expect, layout
        self.pre = pre if pre is not None else list(d['pre'])
        self.immval = immval              # immediate source operand value
        self.target = None                # for jumps: 'next'

def imm_words(size, v):
    if size == 4: return [(v >> 16) & 0xffff, v & 0xffff]
    if size == 1: return [v & 0xff]
    return [v & 0xffff]

def cc_true(c, ccr):
    C, V, Z, N = ccr & 1, (ccr >> 1) & 1, (ccr >> 2) & 1, (ccr >> 3) & 1
    return [1, 0, not C and not Z, C or Z, not C, C, not Z, Z, not V, V, not N, N, N == V, N != V, (N == V) and not Z, Z or (N != V)][c]

def ccr_for(c, want):
    for ccr in range(16):
        if bool(cc_true(c, ccr)) == want: return ccr
    return None

def build(case, start, rnd):
    """returns (words, test_pc, successor, end) for a case placed at start"""
    d = case.d
    op = d['op']
    w = []
    # setup length first
    n_pre = sum(5 if s == 4 else 4 for (_, s, _) in case.mem) + sum(5 if s == 4 else 4 for (_, s, _) in case.stack)
    exc = case.expect == 'exc'
    used = regs_of(d) | set(case.ovr)
    ovr = dict(case.ovr)
    # pick index registers for (d8,An,Xn) / (d8,PC,Xn) operands; they get the extension words later
    operands = []
    if d['src']: operands.append((0, d['src']))
    if d['dst']: operands.append((1, d['dst']))
    idx_choice = {}
    rot = (op * 7 + len(case.tag)) % 15
    cands = [(rot + i) % 15 for i in range(15)]
    for (i, (m, r)) in operands:
        if m in (6, 10):
            for c in cands:
                if c in used or c in idx_choice.values(): continue
                if m == 6 and c == r + 8: continue
                idx_choice[i] = c; break
            if m == 10 or case.target: used.add(idx_choice[i])
    # overrides that addressing needs (jump targets through An, PC-indexed data reads)
    need_ovr = set(ovr)
    for (i, (m, r)) in operands:
        if m == 10: need_ovr.add(idx_choice[i])
        # JSR pushes below A7: with A7 as the base, keep A7 on the stack and reach the target
        # through d16 or the index register instead
        if case.target and (m == 2 or (m in (5, 6) and r != 7)): need_ovr.add(r + 8)
        if case.target and m == 6 and r == 7: need_ovr.add(idx_choice[i])
    setup_len = n_pre * 2 + (8 if exc else 0) + 6 + 6 * len(need_ovr) + 4
    if case.layout == 'bpad': setup_len += 4
    test_pc = start + setup_len + (126 if case.layout == 'bpad' else 0)
    regs = list(TPL)
    for k, v in ovr.items(): regs[k] = v & M32
    # instruction words
    words = [op]
    for kind, v in case.pre:
        if kind == 'imm': words += imm_words(v[0], v[1])
        else: words.append(v & 0xffff)
    addrs = {}
    tgt = None
    for (i, (m, r)) in operands:
        pcx = test_pc + 2 * len(words)
        size = d['size'] or 2
        if m == 11:
            v = case.immval if case.immval is not None else rnd.getrandbits(32)
            words += imm_words(size, v)
            continue
        if m < 2: continue
        if case.target:
            tgt = test_pc + 2 * (len(words) + ext_words(m))
            if m == 2: ovr[r + 8] = tgt
            elif m == 5 and r == 7: words.append((tgt - phys(SP)) & 0xffff)
            elif m == 5: words.append(0x0020); ovr[r + 8] = (tgt - 0x20) & M32
            elif m == 6:
                x = idx_choice[i]; words.append((x << 12) | 0x800 | 0x04)
                if r == 7: ovr[x] = (tgt - 4 - regs[15]) & M32
                else: ovr[r + 8] = (tgt - 4 - regs[x]) & M32
            elif m == 7: words.append(tgt & 0xffff)
            elif m == 8: words += [tgt >> 16, tgt & 0xffff]
            elif m == 9: words.append((tgt - pcx) & 0xffff)
            elif m == 10:
                x = idx_choice[i]; words.append((x << 12) | 0x800 | 0x04)
                ovr[x] = (tgt - pcx - 4) & M32
            regs = list(TPL)
            for k, v in ovr.items(): regs[k] = v & M32
            continue
        off = 0x100 * (i + 1) + 0x10 * (len(case.tag) % 8)
        if m in (2, 3): a = regs[r + 8]
        elif m == 4: a = regs[r + 8] - (2 if (r == 7 and size == 1) else size)
        elif m == 5: words.append(0x0020 + i * 0x40); a = regs[r + 8] + 0x20 + i * 0x40
        elif m == 6:
            x = idx_choice[i]; wl = (op >> 4) & 1
            words.append((x << 12) | (wl << 11) | 0x04)
            xv = regs[x] if wl else s16(regs[x])
            a = regs[r + 8] + 4 + xv
        elif m == 7: a = 0xffff0000 | (0xf000 + off); words.append(a & 0xffff)
        elif m == 8: a = DATA_LO + off; words += [a >> 16, a & 0xffff]
        elif m == 9: a = DATA_LO + 0x400 + off; words.append((a - pcx) & 0xffff)
        elif m == 10:
            x = idx_choice[i]; a = DATA_LO + 0x500 + off
            words.append((x << 12) | 0x800 | 0x04)
            ovr[x] = (a - pcx - 4) & M32; regs[x] = ovr[x]
        addrs[i] = phys(a)
        if not (DATA_LO <= phys(a) < RAM_HI - 0x20) and d['kind'] not in ('lea',):
            raise ValueError('operand outside data area: %04x %s %x' % (op, MODES[m], phys(a)))
    test_len = 2 * len(words)
    if case.layout == 'bpad':
        end = test_pc + 130
    else:
        end = test_pc + test_len
    # setup code
    for (i, s, v) in case.mem:
        a = addrs[i]
        w += [{1: 0x13fc, 2: 0x33fc, 4: 0x23fc}[s]] + imm_words(s if s != 1 else 2, v) + [a >> 16, a & 0xffff]
    for (o, s, v) in case.stack:
        a = phys(SP) + o
        if v is None: v = end
        w += [{2: 0x33fc, 4: 0x23fc}[s]] + imm_words(s, v) + [a >> 16, a & 0xffff]
    if exc: w += [0x21fc, end >> 16, end & 0xffff, CONT & 0xffff]
    w += [0x4cf8, 0xffff, TEMPLATE & 0xffff]
    assert set(ovr) == need_ovr, (hex(op), ovr, need_ovr)
    for k in sorted(ovr):
        v = ovr[k] & M32
        w += [(0x203c if k < 8 else 0x207c) | ((k & 7) << 9), v >> 16, v & 0xffff]
    w += [0x46fc, case.sr]
    if case.layout == 'bpad':
        X = test_pc
        bra_pc = start + 2 * len(w)
        w += [0x6000, (X - (bra_pc + 2)) & 0xffff]
        assert start + 2 * len(w) == X - 126
        for p in range(X - 126, X, 2): w.append(0x6000 | (X - p))
    assert start + 2 * len(w) == test_pc, (hex(op), case.tag, hex(start), len(w), hex(test_pc))
    w += words
    if case.layout == 'bpad':
        for p in range(X + 2, X + 128, 2): w.append(0x6000 | (X + 128 - p))
        w.append(0x4e71)
    assert start + 2 * len(w) == end
    # successor
    if exc: succ = HND
    elif case.layout == 'bpad': succ = test_pc + 2 + s8(d['d8'])
    elif case.expect == 'next': succ = test_pc + test_len
    else: succ = case.expect
    return w, test_pc, succ, end

# ---------------------------------------------------------------------------------------------
# variants per opcode
# ---------------------------------------------------------------------------------------------
REP = set()     # (name, size, kind) already given a full sweep
def first(key):
    if key in REP: return False
    REP.add(key); return True

def cases_for(op, d, rnd):
    k = d['kind']; nm = d['name']
    out = []
    src, dst = d['src'], d['dst']
    srcmode = src[0] if src else None
    def C(tag='', **kw): out.append(Case(d, tag, **kw))
    if k in ('plain', 'lea', 'movep'):
        if nm == 'move_fr_sr' or k == 'lea' or k == 'movep': C(); return out
        C(); return out
    if k == 'imm':
        C(pre=[('imm', (d['size'], rnd.getrandbits(32)))]); return out
    if k == 'srimm':
        if nm.endswith('_ccr'):
            C(pre=[('w', 0x001f if nm != 'andi_ccr' else 0x00f0)])
        else:
            v = {'ori_sr': 0x0015, 'andi_sr': 0xffff, 'eori_sr': 0x001f}[nm]
            C(pre=[('w', v)])
            C('user', pre=[('w', v)], sr=0x0700, expect='exc')
        return out
    if k == 'toccr':
        v = 0x0015
        if srcmode == 0: C(ovr={src[1]: v})
        elif srcmode == 11: C(immval=v)
        elif srcmode == 1: C()
        else: C(mem=[(0, 2, v)])
        return out
    if k == 'tosr':
        v = 0x2715
        if srcmode == 0: C(ovr={src[1]: v}); C('user', ovr={src[1]: v}, sr=0x0700, expect='exc')
        elif srcmode == 11: C(immval=v); C('user', immval=v, sr=0x0700, expect='exc')
        else: C(mem=[(0, 2, v)])
        return out
    if k == 'priv':
        C()
        C('user', sr=0x0700, expect='exc')
        return out
    if k == 'stop':
        return [Case(d, 'stop', pre=[('w', 0x2700)])]
    if k == 'trap':
        C(expect='exc'); return out
    if k == 'trapv':
        C('V=0'); C('V=1', sr=0x2702, expect='exc'); return out
    if k == 'link':
        C(); return out
    if k == 'rts':
        c = Case(d); c.stack = [(0, 4, None)]; out.append(c); return out
    if k == 'rtr':
        c = Case(d); c.stack = [(0, 2, 0x0015), (2, 4, None)]; out.append(c); return out
    if k == 'rte':
        c = Case(d); c.stack = [(0, 2, 0x2715), (2, 4, None)]; out.append(c)
        out.append(Case(d, 'user', sr=0x0700, expect='exc'))
        return out
    if k == 'jump':
        c = Case(d); c.target = 'next'; out.append(c); return out
    if k == 'movem':
        masks = [0x0000, 0x0001, 0x8001, 0x0f0f, 0xffff, 0x7fff]
        if first(('movem', op & 0xfff8 if (op & 0x38) < 0x38 else op)):
            for n in range(17):
                masks.append((1 << n) - 1 if n < 16 else 0xffff)
        # MOVEM mem->reg into A7 is fine: the next test's setup reloads every register
        for mk in dict.fromkeys(masks):
            if k == 'movem' and nm == 'movem_re' and dst[0] == 4:
                pass
            out.append(Case(d, 'mask=%04x' % mk, pre=[('w', mk)]))
        return out
    if k == 'scc':
        c = d['cond']
        ccrs = range(16) if first(('scc', c, dst[0])) else sorted({ccr_for(c, True), ccr_for(c, False)} - {None})
        for ccr in ccrs:
            C('ccr=%x %s' % (ccr, 'T' if cc_true(c, ccr) else 'F'), sr=0x2700 | ccr)
        return out
    if k == 'dbcc':
        c = d['cond']; dn = d['dn']
        t = ccr_for(c, True); f = ccr_for(c, False)
        if t is not None: C('cc true', sr=0x2700 | t, ovr={dn: 5}, pre=[('w', 2)])
        if f is not None:
            C('cc false, loop', sr=0x2700 | f, ovr={dn: 5}, pre=[('w', 2)])
            C('cc false, expire', sr=0x2700 | f, ovr={dn: 0x12340000}, pre=[('w', 2)])
        return out
    if k == 'bcc':
        c, d8 = d['cond'], d['d8']
        if c == 1: tk = [True]
        elif c == 0: tk = [True]
        else: tk = [True, False]
        for take in tk:
            ccr = 0 if c < 2 else ccr_for(c, take)
            if ccr is None: continue
            if d8 == 0:
                C('W %s' % ('taken' if take else 'not taken'), sr=0x2700 | ccr)
                continue
            if take:
                if d8 & 1 or d8 == 0xfe: continue      # odd target: address error; -2: branches to itself
                cs = Case(d, 'taken', sr=0x2700 | ccr, layout='bpad')
                out.append(cs)
            else:
                C('not taken', sr=0x2700 | ccr)
        return out
    if k == 'chk':
        dn = dst[1]
        def mk(tag, dv, bound, exc):
            ex = 'exc' if exc else 'next'
            if srcmode == 0:
                if src[1] == dn:
                    if dv != bound: return
                    C(tag, ovr={dn: dv & M32}, expect=ex)
                else: C(tag, ovr={dn: dv & M32, src[1]: bound & M32}, expect=ex)
            elif srcmode == 11: C(tag, ovr={dn: dv & M32}, immval=bound, expect=ex)
            else: C(tag, ovr={dn: dv & M32}, mem=[(0, 2, bound & 0xffff)], expect=ex)
        mk('in bounds', 5, 100, False); mk('in bounds', 5, 5, False)
        mk('Dn<0', -3, 100, True); mk('Dn<0', -3, -3, True); mk('Dn>bound', 200, 100, True)
        mk('Dn<0, Dn>bound', -3, -5, True); mk('Dn<0, Dn<bound', -10, -5, True); mk('Dn>bound, bound<0', 200, -5, True)
        mk('Dn=0, bound<0', 0, -5, True); mk('Dn=bound', 100, 100, False); mk('Dn=0', 0, 0, False)
        return out
    if k == 'mul':
        dn = dst[1]
        vals = [0x0000, 0xffff, 0x5555, 0xaaaa, 0x8000, 0x7fff, rnd.getrandbits(16)]
        if first(('mul', nm, srcmode)):
            vals += [rnd.getrandbits(16) for _ in range(60)] + [(1 << i) - 1 for i in range(17)] + [0x10000 - (1 << i) for i in range(16)]
        for v in dict.fromkeys(vals):
            dv = rnd.getrandbits(32)
            if srcmode == 0:
                if src[1] == dn: C('src=%04x' % v, ovr={dn: (dv & 0xffff0000) | v})
                else: C('src=%04x' % v, ovr={dn: dv, src[1]: v})
            elif srcmode == 11: C('src=%04x' % v, ovr={dn: dv}, immval=v)
            elif srcmode == 1: C('src=An')
            else: C('src=%04x' % v, ovr={dn: dv}, mem=[(0, 2, v)])
        return out
    if k == 'div':
        dn = dst[1]; signed = nm == 'divs'
        pairs = [(1000000, 1234), (0x7fffffff, 3), (0xffffffff, 0xffff), (100, 7), (0x12345678, 0x0002), (0, 1), (5, 0)]
        if signed: pairs += [((-1000000) & M32, 1234), (1000000, (-1234) & 0xffff), ((-1000000) & M32, (-1234) & 0xffff), (0x80000000, 0xffff), (0x7fff0000, 0x7fff)]
        if first(('div', nm, srcmode)):
            for _ in range(250):
                dv = rnd.getrandbits(rnd.choice([8, 16, 20, 24, 31, 32])); sv = rnd.getrandbits(rnd.choice([4, 8, 12, 16])) or 1
                pairs.append((dv, sv))
        for dv, sv in pairs:
            ex = 'exc' if sv == 0 else 'next'
            tag = '%08x/%04x' % (dv, sv)
            if srcmode == 0:
                if src[1] == dn:
                    C(tag, ovr={dn: (dv & 0xffff0000) | sv}, expect=ex)
                else: C(tag, ovr={dn: dv, src[1]: sv}, expect=ex)
            elif srcmode == 11: C(tag, ovr={dn: dv}, immval=sv, expect=ex)
            elif srcmode == 1: pass
            else: C(tag, ovr={dn: dv}, mem=[(0, 2, sv)], expect=ex)
        return out
    if k == 'shiftreg':
        cr, dr = d['cnt'], dst[1]
        counts = [0, 1, 5, 17, 33, 63]
        if first(('shift', nm, d['size'])): counts = list(range(70)) + [100, 255]
        for n in counts:
            if cr == dr: C('count=%d' % n, ovr={cr: 0xa5a5a500 | n})
            else: C('count=%d' % n, ovr={cr: n | 0x5a5a00 if n < 64 else n, dr: rnd.getrandbits(32)})
        return out
    if k == 'shiftimm':
        C(ovr={dst[1]: rnd.getrandbits(32)}); return out
    if k == 'shiftmem':
        C(); return out
    if k == 'bitdyn':
        br = d['bitreg']
        if dst[0] == 0:
            nums = [3, 20]
            if first(('bitdyn', nm)): nums = list(range(64))
            for n in nums:
                if br == dst[1]: C('bit=%d' % n, ovr={br: (rnd.getrandbits(32) & ~63) | n})
                else: C('bit=%d' % n, ovr={br: n, dst[1]: rnd.getrandbits(32)})
        else:
            C('bit=5', ovr={br: 5}, immval=0x5a)
        return out
    if k == 'bitimm':
        if dst[0] == 0:
            nums = [3, 20]
            if first(('bitimm', nm)): nums = list(range(64)) + [0x80, 0xff]
            for n in nums: C('bit=%d' % n, pre=[('w', n)])
        else:
            C('bit=5', pre=[('w', 5)])
        return out
    raise ValueError('no cases for %04x %s' % (op, nm))

# ---------------------------------------------------------------------------------------------
def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    here = os.path.dirname(os.path.abspath(__file__))
    valid = {}
    vf = sys.argv[2] if len(sys.argv) > 2 else os.path.join(out, 'valid.txt')
    for line in open(vf):
        a, b = line.split(); valid[int(a, 16)] = b == '1'
    rnd = random.Random(68000)
    allcases = []
    for op in range(0x10000):
        hi = op >> 12
        if hi in (0xa, 0xf):
            d = dict(op=op, size=0, src=None, dst=None, pre=[], kind='exc', name='line' + ('a' if hi == 0xa else 'f'))
            allcases.append(Case(d, expect='exc')); continue
        d = decode(op) if (valid[op] or (hi == 6 and (op & 0xff) == 0xff)) else None
        if d is None:
            if valid[op]: raise ValueError('decoder misses %04x' % op)
            d = dict(op=op, size=0, src=None, dst=None, pre=[], kind='exc', name='illegal')
            allcases.append(Case(d, expect='exc')); continue
        for c in cases_for(op, d, rnd): allcases.append(c)
    stop = [c for c in allcases if c.d['kind'] == 'stop']
    allcases = [c for c in allcases if c.d['kind'] != 'stop']
    # layout into batches
    batches = []
    tests = []
    cur, curtests, pc = [], [], RAM_LO
    def flush(extra_end=True):
        nonlocal cur, curtests, pc
        if not curtests: return
        endseq = [0x31fc, 0x0001, DONE & 0xffff, 0x2e7c, 0x0007, 0xee01]
        end_pc = pc + 2 * len(endseq)
        words = cur + endseq + [0x4a57]
        batches.append((RAM_LO, end_pc, words))
        tests.append(curtests)
        cur, curtests, pc = [], [], RAM_LO
    for c in allcases:
        crnd = random.Random(c.d['op'] * 1000 + len(c.tag))
        w, tpc, succ, end = build(c, pc, crnd)
        if end > CODE_HI - 16 or len(curtests) >= 420:
            flush()
            crnd = random.Random(c.d['op'] * 1000 + len(c.tag))
            w, tpc, succ, end = build(c, pc, crnd)
        cur += w
        curtests.append(dict(pc=tpc, op=c.d['op'], name=c.d['name'], tag=c.tag, succ=succ,
                             src=MODES[c.d['src'][0]] if c.d['src'] else {'bitdyn': 'Dn', 'bitimm': '#n'}.get(c.d['kind']),
                             dst=MODES[c.d['dst'][0]] if c.d['dst'] else None,
                             size=SZ.get(c.d['size'], '')))
        pc = end
    flush()
    for c in stop:   # STOP last, alone: MAME's STOP never ends, so this batch times out
        w, tpc, succ, end = build(c, RAM_LO, rnd)
        batches.append((RAM_LO, end, w))
        tests.append([dict(pc=tpc, op=c.d['op'], name='stop', tag='stop', succ=succ, src=None, dst=None, size='')])
    # images
    with open(os.path.join(out, 'batches.bin'), 'wb') as f:
        for bi, (start, endpc, words) in enumerate(batches):
            img = bytearray(RAM_HI - RAM_LO)
            code = struct.pack('>%dH' % len(words), *words)
            assert RAM_LO + len(code) <= CODE_HI, len(code)
            img[0:len(code)] = code
            img[TEMPLATE - RAM_LO:TEMPLATE - RAM_LO + 64] = struct.pack('>16I', *TPL)
            img[CONT - RAM_LO:CONT - RAM_LO + 4] = struct.pack('>I', ABORT)
            stub = [0x2c78, CONT & 0xffff, 0x21fc, ABORT >> 16, ABORT & 0xffff, CONT & 0xffff, 0x4ed6]
            img[HND - RAM_LO:HND - RAM_LO + 14] = struct.pack('>7H', *stub)
            ab = [0x31fc, 0x0002, DONE & 0xffff, 0x2e7c, 0x0007, 0xee01, 0x4a57]
            img[ABORT - RAM_LO:ABORT - RAM_LO + 14] = struct.pack('>7H', *ab)
            for i in range(DATA_LO - RAM_LO, RAM_HI - RAM_LO, 2):
                struct.pack_into('>H', img, i, (i * 0x9e37) & 0xffff)
            f.write(struct.pack('>4I', 0x4d544231, bi, start, endpc) + bytes(img))
    json.dump(tests, open(os.path.join(out, 'tests.json'), 'w'))
    print('%d tests in %d batches' % (sum(len(t) for t in tests), len(batches)))

if __name__ == '__main__':
    main()
