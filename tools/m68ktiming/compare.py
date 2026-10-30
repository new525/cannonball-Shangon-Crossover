#!/usr/bin/env python3
"""compare.py: per-instruction cycles of MAME 0.264 (trace from mame_run.lua) against Musashi (mtime).

usage: compare.py <gen dir> <musashi out.txt> <mame trace> [report.txt]

Checks that both ran every test with the expected successor (next instruction, branch target or
the exception handler stub) and lists every test whose cycles differ, grouped by instruction,
size and addressing modes.
"""
import json, re, sys
from collections import defaultdict

RAM_LO = 0x7c000

def load_mus(fn):
    batches, cur = {}, None
    for line in open(fn):
        if line[0] == 'B': cur = []; batches[int(line.split()[1])] = cur
        elif line[0] == 'E': pass
        else:
            a, c = line.split(); cur.append((int(a, 16) & 0x7ffff, int(c)))
    return batches

def load_mame(fn, nb):
    seq = []
    for line in open(fn, errors='replace'):
        m = re.match(r'([0-9A-F]+) ([0-9A-F]+): ', line)
        if m: seq.append((int(m.group(2), 16) & 0x7ffff, int(m.group(1), 16)))
    # split into batches at each batch start; the batch order is the file order
    # an instruction that ends in a privilege violation is hooked twice at the same PC and cycle
    seq = [x for i, x in enumerate(seq) if i == 0 or x != seq[i - 1]]
    out, cur = [], None
    for pc, cyc in seq:
        if pc == RAM_LO: cur = []; out.append(cur)
        if cur is not None: cur.append((pc, cyc))
    res = {}
    for bi, b in enumerate(out):
        res[bi] = [(b[i][0], b[i + 1][1] - b[i][1]) for i in range(len(b) - 1)] + [(b[-1][0], None)]
    return res

def per_test(tests, run):
    """{(batch, index): (cycles, successor ok)}"""
    r = {}
    for bi, tl in enumerate(tests):
        seq = run.get(bi)
        if seq is None: continue
        pos = {}
        for i, (pc, c) in enumerate(seq):
            pos.setdefault(pc, i)
        for ti, t in enumerate(tl):
            i = pos.get(t['pc'])
            if i is None: continue
            nxt = seq[i + 1][0] if i + 1 < len(seq) else None
            r[(bi, ti)] = (seq[i][1], nxt == (t['succ'] & 0x7ffff), nxt)
    return r

def main():
    gdir, musf, mamef = sys.argv[1:4]
    rep = open(sys.argv[4], 'w') if len(sys.argv) > 4 else sys.stdout
    tests = json.load(open(gdir + '/tests.json'))
    mus = per_test(tests, load_mus(musf))
    mame = per_test(tests, load_mame(mamef, len(tests)))
    total = sum(len(t) for t in tests)
    groups = defaultdict(list)
    missing_mus = missing_mame = bad_mus = bad_mame = same = 0
    badlist = []
    for bi, tl in enumerate(tests):
        for ti, t in enumerate(tl):
            a, b = mus.get((bi, ti)), mame.get((bi, ti))
            if a is None: missing_mus += 1
            if b is None: missing_mame += 1
            if a is None or b is None: badlist.append((bi, ti, t, a, b)); continue
            if not a[1]: bad_mus += 1
            if not b[1]: bad_mame += 1
            if not a[1] or not b[1]: badlist.append((bi, ti, t, a, b)); continue
            if a[0] == b[0]: same += 1; continue
            key = (t['name'], t['size'], t['src'] or '', t['dst'] or '')
            groups[key].append((t, a[0], b[0]))
    print('tests %d, compared %d, equal %d, different %d' % (total, total - len(badlist), same, total - len(badlist) - same), file=rep)
    print('not run: musashi %d, mame %d; wrong successor: musashi %d, mame %d' % (missing_mus, missing_mame, bad_mus, bad_mame), file=rep)
    for bi, ti, t, a, b in badlist[:200]:
        print('  unchecked b%d t%d %04x %s %s pc=%x succ=%x musashi=%s mame=%s' % (bi, ti, t['op'], t['name'], t['tag'], t['pc'], t['succ'],
              a and (a[0], hex(a[2] or 0)), b and (b[0], hex(b[2] or 0))), file=rep)
    print('\ndifferences (musashi -> mame), grouped by instruction, size, source, destination:', file=rep)
    for key in sorted(groups):
        lst = groups[key]
        pairs = defaultdict(list)
        for t, a, b in lst: pairs[(a, b)].append(t)
        desc = ', '.join('%d->%d x%d' % (a, b, len(v)) for (a, b), v in sorted(pairs.items()))
        ex = lst[0][0]
        print('%-12s %-2s %-12s %-12s %s   e.g. %04x %s' % (key + (desc, ex['op'], ex['tag'])), file=rep)
        if len(pairs) > 1 or any(t['tag'] for t, _, _ in lst):
            for (a, b), v in sorted(pairs.items()):
                tags = sorted({t['tag'] for t in v})
                print('      %d->%d: %s' % (a, b, ' | '.join(tags[:12]) + (' ...' if len(tags) > 12 else '')), file=rep)

if __name__ == '__main__':
    main()
