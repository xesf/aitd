"""Time Gate life scripts: operand shapes read from the TG320.EX_ interpreter
(jump table 0x418583, evaluator 0x417fb7) by tgsym.py, checked by parsing
every LISTLIFE script of data/TIMEGATE.  Run: python3 tgcheck.py"""
import os
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')
import lifecheck,struct,collections,tgsym,tglister as T
tbl=0x418583
t=[struct.unpack_from('<I',T.d,T.off(tbl)+4*i)[0] for i in range(123)]
shapes={op:tgsym.run(t[op]) for op in range(123)}
# Shapes the symbolic walk of TG320.EX_ cannot see (jumps, counted lists,
# handlers the scripts still give a word to).
special={10:'j',25:'wj',27:'M',97:'T',4:'eej',5:'eej',6:'eej',7:'eej',8:'eej',9:'eej',
         102:'eeej',103:'eeej',20:'w',28:'w',61:'w',121:'L'}
# Evaluator variables with operands in Time Gate.
TG_VARS={19:'E',34:1,42:'E'}
def check(over={},ve_over={},show=8):
    spec={}
    for op in range(123):
        s=over.get(op,special.get(op,shapes[op])); spec[op]=('OP%d'%op,s)
    lifes=[d for d in lifecheck.entries(ROOT+'/data/TIMEGATE/LISTLIFE.PAK') if d]
    ve=dict(lifecheck.VAREXTRA['A2']); ve.update(ve_over)
    ok=0; fails=collections.Counter(); ex=[]
    for n,d in enumerate(lifes):
        r=lifecheck.parse(d,spec,ve)
        if r[0]=='ok': ok+=1
        else: fails[(r[0],r[1])]+=1; ex.append((n,r[:3],r[3][-5:]))
    print('ok',ok,'/',len(lifes)); print(fails.most_common(10)); print(ex[:show])
    return ok
if __name__=='__main__': check({},TG_VARS)
