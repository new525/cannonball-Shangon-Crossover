#!/usr/bin/env python3
"""irqcheck.py: interrupt latency, MAME 0.264 (mame_irq.lua trace) against Musashi (mtime irq).

usage: irqcheck.py <mame trace> <musashi trace> <first interrupt cycle> <period> [stop]

Compares the cycle each interrupt handler ($20C800) starts at, the latency from the interrupt
to the handler, and (without 'stop') every instruction's start cycle. With 'stop' the program
waits in STOP, which MAME's trace logs every 4 cycles, so only the handler starts are compared.
"""
import re, sys

def lines(fn):
    for line in open(fn, errors='replace'):
        m = re.match(r'([0-9A-F]+) ([0-9A-F]+):', line)
        if m: yield int(m.group(1), 16), int(m.group(2), 16)

def main():
    mame, mus, t0, period = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), int(sys.argv[4], 0)
    stop = len(sys.argv) > 5 and sys.argv[5] == 'stop'
    hm = [c for c, pc in lines(mame) if pc == 0x20c800]
    hu = [c for c, pc in lines(mus) if pc == 0x20c800]
    n = min(len(hm), len(hu))
    same = sum(1 for i in range(n) if hm[i] == hu[i])
    lat = lambda h: [h[k] - (t0 + k * period) for k in range(n)]
    lm, lu = lat(hm), lat(hu)
    print('interrupts %d, handler start identical in %d' % (n, same))
    print('latency interrupt -> handler: MAME mean %.2f (%d..%d), Musashi mean %.2f (%d..%d)' %
          (sum(lm) / n, min(lm), max(lm), sum(lu) / n, min(lu), max(lu)))
    if not stop:
        k = 0
        for x, y in zip(lines(mame), lines(mus)):
            if x != y:
                print('first instruction difference at %d: MAME %x %06x, Musashi %x %06x' % (k, x[0], x[1], y[0], y[1]))
                break
            k += 1
        else:
            print('every instruction starts at the same cycle (%d instructions)' % k)

if __name__ == '__main__':
    main()
