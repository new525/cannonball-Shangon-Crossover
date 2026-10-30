#!/usr/bin/env python3
"""iplscan.py: where MAME 0.264's 68000 samples the interrupt level, per instruction, from its
generated microcode (src/devices/cpu/m68000/m68000-sdf.cpp).

usage: iplscan.py <m68000-sdf.cpp> <m68000-decode.cpp> <out.json> [m68kipl.h]

MAME's 68000 can stop between bus cycles when its time slice runs out; an interrupt line that
changes then is seen by the next "m_next_state = m_int_next_state" the instruction executes (the
last one executed decides). So an interrupt raised while instruction I runs is taken after I if
it arrives no later than the end of the last bus cycle before I's final sample, otherwise after
the next instruction. This walks every path through every instruction handler (both ways at each
data-dependent branch, loops up to a bound) and records, per path, the cycles the instruction
takes and that deadline (cycles from the instruction's start; 0 = no bus cycle before the sample).

Output: {opcode handler name: [[cycles, deadline, sample time], ...]} plus the opcode -> handler map;
with a fourth argument, also the C table Musashi uses (src/main/crossover/m68k/m68kipl.h).
"""
import json, re, sys

def handlers(src):
    out = {}
    for m in re.finditer(r'^void m68000_device::(\w+_df)\(\)[^\n]*\n\{\n(.*?)^\}\n', src, re.S | re.M):
        out[m.group(1)] = m.group(2).split('\n')
    return out

def block_end(lines, i):
    """index after the block opened at line i (which ends with '{')"""
    depth = 0
    for j in range(i, len(lines)):
        depth += lines[j].count('{') - lines[j].count('}')
        if depth == 0: return j + 1
    raise ValueError('unbalanced')

def walk(lines, maxvisit=40):
    labels = {}
    for i, l in enumerate(lines):
        m = re.match(r'^(\w+):$', l.strip())
        if m: labels[m.group(1)] = i + 1
    results = set()
    # state: (line, t, last_access_end, sample_t, deadline, visits)
    stack = [(0, 0, 0, None, None, ())]
    npaths = 0
    while stack:
        i, t, acc, smp, dl, vis = stack.pop()
        while True:
            if i >= len(lines): results.add((t, dl, smp)); break
            l = lines[i].strip()
            if l.startswith('//') or not l: i += 1; continue
            m = re.match(r'^m_icount -= (\d+);$', l)
            if m:
                t += int(m.group(1)); i += 1
                # a bus access ends here when a pause check follows
                if i < len(lines) and lines[i].strip() == 'if(m_icount <= 0) {':
                    acc = t; i = block_end(lines, i)
                continue
            if 'm_next_state = m_int_next_state' in l:
                smp = t; dl = acc; i += 1; continue
            if l == 'return;':
                results.add((t, dl, smp)); break
            if l.startswith('if(') and l.endswith('{'):
                i = block_end(lines, i); continue        # pause, address error, privilege
            m = re.match(r'^goto (\w+);$', l)
            if m:
                lab = m.group(1)
                if vis.count(lab) > maxvisit: break
                vis = vis + (lab,); i = labels[lab]; continue
            if l.startswith('if('):
                nx = lines[i + 1].strip()
                if not nx.startswith('goto '):
                    i += 2; continue                      # one conditional statement, no timing
                # if(..) goto a; [else if(..) goto b;] [else goto c;]
                targets, k, has_else = [], i, False
                while k < len(lines):
                    s0 = lines[k].strip()
                    if s0.startswith('if(') or s0.startswith('else if(') or s0 == 'else':
                        g = re.match(r'^goto (\w+);$', lines[k + 1].strip())
                        if not g: break
                        targets.append(g.group(1)); k += 2
                        if s0 == 'else': has_else = True; break
                    else: break
                alts = [(labels[lab], vis + (lab,)) for lab in targets if vis.count(lab) <= maxvisit]
                if not has_else: alts.append((k, vis))
                if not alts: break
                for (li, vv) in alts[1:]: stack.append((li, t, acc, smp, dl, vv))
                i, vis = alts[0]
                continue
            if 'm_inst_state = S_' in l:
                results.add((t, -1, smp)); break    # leaves for an exception state
            i += 1
        npaths += 1
        if npaths > 20000: break
    return sorted(results, key=lambda x: (x[0], x[1] if x[1] is not None else -9))

def main():
    src = open(sys.argv[1]).read()
    dec = open(sys.argv[2]).read()
    hs = handlers(src)
    i = src.index('s_handlers_df[] = {')
    names = re.findall(r'&m68000_device::(\w+)', src[i:])
    ent = re.findall(r'\{ 0x([0-9a-f]+), 0x([0-9a-f]+),\s*(\w+) \}', dec)
    tab = [None] * 65536
    for v, m, st in ent:
        v, m = int(v, 16), int(m, 16)
        if not st.isdigit(): continue
        nm = names[int(st)]
        c = 0
        while True:
            if tab[v | c] is None: tab[v | c] = nm
            c = ((c | m) + 1) & ~m & 0xffff
            if c == 0: break
    res = {}
    for nm in sorted(set(x for x in tab if x)):
        res[nm] = walk(hs[nm])
    json.dump({'handlers': res, 'opcodes': tab}, open(sys.argv[3], 'w'))
    if len(sys.argv) > 4:
        pr = lambda w: [(c, q) for c, q, t in w]
        exc = rule(pr(walk(hs['state_illegal_df'])))            # illegal, line A, line F
        assert rule(pr(walk(hs['state_linea_df']))) == exc == rule(pr(walk(hs['state_linef_df'])))
        rules, index = [], []
        for op in range(65536):
            r = rule(pr(res[tab[op]])) if tab[op] else exc
            key = (r[0], r[1], tuple(sorted(r[2].items())))
            assert len(key[2]) <= 1 and r[1] < 256
            if key not in rules: rules.append(key)
            index.append(rules.index(key))
        assert len(rules) < 256
        with open(sys.argv[4], 'w') as f:
            f.write('/*\n * Crossover: where MAME 0.264\'s 68000 samples the interrupt level, per opcode, generated by\n'
                    ' * tools/m68ktiming/iplscan.py from MAME\'s microcode (src/devices/cpu/m68000/m68000-sdf.cpp).\n'
                    ' * An interrupt that arrives no later than the deadline (cycles from the start of the instruction)\n'
                    ' * is taken after the instruction, a later one after the next instruction. The deadline is\n'
                    ' * "val" (const) or the instruction\'s cycles minus "val" (diff), except at "exc_cycles".\n */\n')
            f.write('typedef struct { unsigned char diff, val; unsigned short exc_cycles; unsigned char exc_deadline; } m68ki_ipl_rule_t;\n\n')
            f.write('static const m68ki_ipl_rule_t m68ki_ipl_rules[%d] =\n{\n' % len(rules))
            for kind, v, ex in rules:
                ec, eq = ex[0] if ex else (0, 0)
                f.write('\t{ %d, %3d, %3d, %3d },\n' % (kind == 'diff', v, ec, eq))
            f.write('};\n\nstatic const unsigned char m68ki_ipl_rule_of[65536] =\n{\n')
            for i in range(0, 65536, 32):
                f.write('\t' + ','.join('%d' % x for x in index[i:i + 32]) + ',\n')
            f.write('};\n')
    multi = {k: v for k, v in res.items() if len({c for c, d, s in v if d != -1}) > 1}
    print(len(res), 'handlers;', len(multi), 'with more than one cycle count')


def rule(pairs):
    """compact deadline rule for one handler from its (cycles, deadline) pairs:
    ('const', q, {c: q exceptions}) or ('diff', k, {...}) meaning deadline = cycles - k"""
    pairs = {c: q for c, q in pairs if q is not None and q >= 0}
    if not pairs: return ('const', 0, {})
    from collections import Counter
    qc = Counter(pairs.values()); dc = Counter(c - q for c, q in pairs.items())
    q0, nq = qc.most_common(1)[0]; d0, nd = dc.most_common(1)[0]
    if nq >= nd: return ('const', q0, {c: q for c, q in pairs.items() if q != q0})
    return ('diff', d0, {c: q for c, q in pairs.items() if c - q != d0})

def deadline(r, c):
    kind, v, exc = r
    if c in exc: return exc[c]
    return v if kind == 'const' else c - v

if __name__ == '__main__':
    main()
