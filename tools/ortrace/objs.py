# objs.py trace frame: the object list dispatched by $11F3E (jsr [4[R20]]) in that frame: object, routine, instructions until return
import sys
L=[l for l in open(sys.argv[1]) if l.startswith(sys.argv[2]+' ')]
i=0
while i < len(L):
    p=L[i].split(None,2)
    if p[1]=='011f3e:':
        r20=L[i].split('|')[1].split()[20]
        tgt=L[i+1].split()[1].rstrip(':')
        # count until we come back to 011f41
        j=i+1
        while j < len(L) and L[j].split()[1] != '011f41:': j+=1
        print('obj', r20, 'routine', tgt, 'instr', j-i-1)
        i=j
    i+=1
