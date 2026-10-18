import os
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')
import struct,sys
class Bits:
    def __init__(s,d): s.d=d; s.p=0; s.b=0; s.n=0
    def get(s,k):
        while s.n<k:
            s.b|=(s.d[s.p] if s.p<len(s.d) else 0)<<s.n; s.p+=1; s.n+=8
        v=s.b&((1<<k)-1); s.b>>=k; s.n-=k; return v
def read_tree(bits,n):
    cnt=bits.get(8)+1; lens=[]
    for _ in range(cnt):
        b=bits.get(8); lens+= [(b&15)+1]*((b>>4)+1)
    assert len(lens)==n,(len(lens),n)
    # canonical tables
    count=[0]*17
    for l in lens: count[l]+=1
    offs=[0]*17
    for i in range(1,16): offs[i+1]=offs[i]+count[i]
    sym=[0]*n
    for s_,l in enumerate(lens):
        sym[offs[l]]=s_; offs[l]+=1
    return count,sym
def decode(bits,t):
    count,sym=t; code=first=index=0
    for l in range(1,17):
        code|=(bits.get(1)^1)
        c=count[l]
        if code-c<first: return sym[index+(code-first)]
        index+=c; first+=c; first<<=1; code<<=1
    raise ValueError
def explode(data,size,flags):
    bits=Bits(data)
    lit = read_tree(bits,256) if flags&4 else None
    ln=read_tree(bits,64); ds=read_tree(bits,64)
    out=bytearray(); dbits=7 if flags&2 else 6; minm=3 if lit else 2
    while len(out)<size:
        if bits.get(1):
            out.append(decode(bits,lit) if lit else bits.get(8))
        else:
            low=bits.get(dbits); d=((decode(bits,ds)<<dbits)|low)+1
            l=decode(bits,ln)
            if l==63: l+=bits.get(8)
            l+=minm
            for _ in range(l):
                if len(out)>=size: break
                out.append(out[-d] if d<=len(out) else 0)
    return bytes(out)
def load(path):
    d=open(path,'rb').read(); n=struct.unpack_from('<I',d,4)[0]//4; res=[]
    for i in range(1,n):
        o=struct.unpack_from('<I',d,4*i)[0]
        if o==0 or o+16>len(d): res.append(None); continue
        r,ps,us,m,b5,sk,_=struct.unpack_from('<IIIBBBB',d,o)
        p=d[o+16+sk:o+16+sk+ps]
        res.append((m,b5,us,p))
    return res
if __name__=='__main__':
    ok=bad=0
    for path in sys.argv[1:]:
        for e in load(path):
            if not e: continue
            m,b5,us,p=e
            if m!=1: continue
            try:
                o=explode(p,us,b5); ok+=1
            except Exception as ex: bad+=1; print(path,'fail',ex)
    print('ok',ok,'bad',bad)
