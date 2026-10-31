# calltree.py trace [maxdepth] [frame]: call tree from an ortrace trace (calls: bsr/call/jsr; returns: rsr/ret/retis)
import sys, re
path = sys.argv[1]; maxd = int(sys.argv[2]) if len(sys.argv) > 2 else 3
fsel = sys.argv[3] if len(sys.argv) > 3 else None
depth = 0; pending = None; counts = []
out = []
stack = []
for line in open(path):
    parts = line.split(None, 2)
    if len(parts) < 3: continue
    f, pc, rest = parts[0], parts[1].rstrip(':'), parts[2]
    if fsel and f != fsel: continue
    ins = rest.split('|')[0].strip()
    op = ins.split()[0] if ins else ''
    if pending is not None:
        # first instruction after a call
        stack.append(pc)
        if len(stack) <= maxd:
            out.append(("  " * (len(stack) - 1)) + "%s %s (from %s)" % (f, pc, pending))
        pending = None
    if op in ('bsr', 'call', 'jsr'):
        pending = pc
    elif op in ('rsr', 'ret') :
        if stack: stack.pop()
    elif op in ('retis', 'retiu'):
        pass
print("\n".join(out))
