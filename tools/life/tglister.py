import os
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')
import struct,re
from capstone import *
d=open(ROOT+'/data/KNIGHTS/320/TG320.EX_','rb').read()
CODE_VA,CODE_RAW=0x401000,0x400
DATA_VA,DATA_RAW=0x43b000,0x39e00
def off(v): return v-CODE_VA+CODE_RAW
def cstr(v):
    o=v-DATA_VA+DATA_RAW; j=d.index(b'\0',o); return d[o:j].decode('latin1')
md=Cs(CS_ARCH_X86,CS_MODE_32)
DBG='byte ptr [0x43c263]'; EVAL=0x403c0e
def insn_at(a):
    return next(md.disasm(d[off(a):off(a)+16],a))
def walk(start,maxsteps=400):
    a=start; seq=[]; text=[]; steps=0; last_read=False
    while steps<maxsteps:
        steps+=1
        i=insn_at(a); m,o=i.mnemonic,i.op_str
        nxt=a+i.size
        if m=='ret': break
        if m=='cmp' and DBG in o: flag=True
        if m in('jne','je') and steps>0:
            prev=prev_ins
            if prev and prev.mnemonic=='cmp' and DBG in prev.op_str:
                # debug == 1 : jne not taken, je taken
                a = int(o,16) if m=='je' else nxt; prev_ins=i; continue
            if prev and prev.mnemonic=='test' and prev.op_str=='bx, bx':
                a = int(o,16) if m=='je' else nxt; prev_ins=i; continue
            # other branch: record and fall through
            seq.append('?'+m); a=nxt; prev_ins=i; continue
        if m=='jmp':
            if o.startswith('0x'): a=int(o,16); prev_ins=i; continue
            break
        if m in('movsx','mov','movzx') and '[edx]' in o and o.split(',')[0].strip() in ('eax','ax','ecx','cx','ebx','bx'):
            seq.append('w'); 
        if m=='mov' and re.match(r'(e?ax|e?cx|e?bx), word ptr \[edx \+ (\d+)\]',o): seq.append('w+')
        if m=='add' and o.startswith('edx, '):
            seq.append('+%s'%o.split(',')[1].strip())
        if m=='call' and o.startswith('0x') and int(o,16)==EVAL: seq.append('E')
        if m=='push' and o.startswith('0x'):
            v=int(o,16)
            if DATA_VA<=v<DATA_VA+0x1000: text.append(cstr(v))
        if m=='mov' and re.match(r'eax, 0x43b[0-9a-f]{3}$',o): text.append(cstr(int(o.split(',')[1],16)))
        if m=='call' and o.startswith('0x') and int(o,16)==0x4059b7: break
        prev_ins=i; a=nxt
        if a in (0x4059d1,0x4059b7): break
    return seq,text
if __name__=='__main__':
    tbl=0x403d42
    t=[struct.unpack_from('<I',d,off(tbl)+4*i)[0] for i in range(121)]
    for op in range(121):
        prev_ins=None
        seq,text=walk(t[op])
        print(op,hex(t[op]),' '.join(seq),'|',' '.join(x.replace('\n','').replace('\r','') for x in text[:6]))
