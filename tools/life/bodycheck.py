import lifecheck,struct,collections,sys
def prims(b):
    fl=struct.unpack_from('<H',b,0)[0]
    p=14; e=struct.unpack_from('<H',b,p)[0]; p+=2+e; n=struct.unpack_from('<H',b,p)[0]; p+=2+n*6
    if fl&2:
        ng=struct.unpack_from('<H',b,p)[0]; p+=2+2*ng+ng*(0x18 if fl&8 else 0x10)
    np_=struct.unpack_from('<H',b,p)[0]; p+=2
    types=collections.Counter()
    for k in range(np_):
        t=b[p]; p+=1; types[t]+=1
        if t==0: p+=3+4
        elif t in (1,8): c=b[p]; p+=3+2*c
        elif t in (9,10): c=b[p]; p+=3+2*c+2*c
        elif t in (2,6,7): p+=3+2
        elif t==3: p+=3+2+2
        else: return fl,types,'bad type %d at prim %d'%(t,k),p,len(b)
    return fl,types,'ok',p,len(b)
if __name__=='__main__':
    g=sys.argv[1]
    B=lifecheck.entries(lifecheck.ROOT+'/data/%s/LISTBODY.PAK'%g)
    res=collections.Counter(); tt=collections.Counter(); rem=collections.Counter(); ex=[]
    for i,b in enumerate(B):
        if len(b)<20: continue
        fl,types,st,p,L=prims(b); tt.update(types)
        res[(hex(fl),st if st=='ok' else 'bad')]+=1
        if st=='ok': rem[(hex(fl),L-p)]+=1
        elif len(ex)<4: ex.append((i,hex(fl),st))
    print(res.most_common(10)); print('types',sorted(tt.items())); print('remaining',rem.most_common(10)); print(ex)
