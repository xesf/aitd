import os
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')
import struct,re
import tglister as T
PCG='[0x45c4d8]'; EVAL=0x417fb7; EXIT=0x41ac94
R32=('eax','ebx','ecx','edx','esi','edi','ebp')
def run(start,maxsteps=400,branch_taken=None):
    """branch_taken: set of addresses of conditional jumps to take"""
    branch_taken=branch_taken or set()
    regs={}; pc=[0,0]; segs=[]; reads=[]; a=start; steps=0; seen=set()
    epoch=0
    def close():
        segs.append(pc[1])
    while steps<maxsteps:
        steps+=1
        if a==EXIT: break
        if a in seen: break
        seen.add(a)
        i=T.insn_at(a); m,o=i.mnemonic,i.op_str; nxt=a+i.size
        ops=[x.strip() for x in o.split(',')] if o else []
        if m=='ret': break
        if m=='jmp':
            if o.startswith('0x'): a=int(o,16); continue
            break
        if m.startswith('j'):
            a=int(o,16) if a in branch_taken else nxt; continue
        if m=='call' and o.startswith('0x') and int(o,16)==EVAL:
            segs.append(pc[1]); epoch+=1; pc=[epoch,0]; regs={}; a=nxt; continue
        if m=='call':
            for r in ('eax','ecx','edx','ebx'): regs.pop(r,None)
        if m=='mov' and len(ops)==2 and ops[0] in R32 and ops[1]=='dword ptr '+PCG:
            regs[ops[0]]=tuple(pc)
        elif m=='mov' and len(ops)==2 and ops[0]=='dword ptr '+PCG and ops[1] in R32:
            if ops[1] in regs and regs[ops[1]][0]==epoch: pc=[epoch,regs[ops[1]][1]]
        elif m=='add' and len(ops)==2 and ops[0]=='dword ptr '+PCG and ops[1].startswith(('0x','1','2','3','4','5','6','7','8','9')):
            pc[1]+=int(ops[1],0)
        elif m=='lea' and len(ops)==2 and ops[0] in R32:
            mm=re.match(r'\[(\w+)(?: \+ (0x[0-9a-f]+|\d+))?\]',ops[1])
            if mm and mm.group(1) in regs:
                e,k=regs[mm.group(1)]; regs[ops[0]]=(e,k+int(mm.group(2) or '0',0))
            else: regs.pop(ops[0],None)
        elif m=='mov' and len(ops)==2 and ops[0] in R32 and ops[1] in R32:
            if ops[1] in regs: regs[ops[0]]=regs[ops[1]]
            else: regs.pop(ops[0],None)
        elif m=='add' and len(ops)==2 and ops[0] in R32 and re.match(r'^(0x[0-9a-f]+|\d+)$',ops[1]):
            if ops[0] in regs: e,k=regs[ops[0]]; regs[ops[0]]=(e,k+int(ops[1],0))
        else:
            # memory reads through tracked regs
            for x in ops[1:] if ops else []:
                mm=re.search(r'word ptr \[(\w+)(?: \+ (0x[0-9a-f]+|\d+))?\]',x)
                if mm and mm.group(1) in regs:
                    e,k=regs[mm.group(1)]; reads.append((e,k+int(mm.group(2) or '0',0)))
            if ops and ops[0] in R32 and m not in('cmp','test','push'):
                regs.pop(ops[0],None)
        a=nxt
    segs.append(pc[1])
    # shape: words in epoch 0, then e, words in epoch1, ...
    out=''
    for ei,n in enumerate(segs):
        out+='w'*(n//2) + ('' if n%2==0 else '?odd')
        if ei<len(segs)-1: out+='e'
    return out
if __name__=='__main__':
    tbl=0x418583
    t=[struct.unpack_from('<I',T.d,T.off(tbl)+4*i)[0] for i in range(123)]
    for op in range(123):
        print(op,run(t[op]))
