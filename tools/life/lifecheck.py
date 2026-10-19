"""Parses every LISTLIFE script of a title with per-title operand shapes
(w word, e expression, j jump, M multi-case, T sample table, L counted
expression list); a shape error shows up as a script that does not end
exactly at its length.  Run: python3 lifecheck.py AITD1 JACK AITD2 AITD3"""
import os
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')
"""Parse every life script of a game with a per-game operand spec.
w=word e=expression j=jump M=multi-case T=sample table"""
import sys,struct,zlib,collections
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import explode
BASE={'DO_MOVE':'','ANIM_ONCE':'ww','ANIM_ALL_ONCE':'ww','BODY':'e','IF_EQUAL':'eej','IF_DIFFERENT':'eej','IF_SUP_EQUAL':'eej','IF_SUP':'eej','IF_INF_EQUAL':'eej','IF_INF':'eej',
'GOTO':'j','RETURN':'','END':'','ANIM_REPEAT':'w','ANIM_MOVE':'wwwwwww','MOVE':'ww','HIT':'wwwwew','MESSAGE':'w','VAR':'we','INC':'w','DEC':'w','ADD':'we','SUB':'we','LIFE_MODE':'w',
'SWITCH':'e','CASE':'wj','START_CHRONO':'','MULTI_CASE':'M','FOUND':'w','LIFE':'w','DELETE':'e','TAKE':'w','IN_HAND':'e','READ':'ww','ANIM_SAMPLE':'eww','SPECIAL':'w','DO_REAL_ZV':'',
'SAMPLE':'w','TYPE':'w','GAME_OVER':'','MANUAL_ROT':'','RND_FREQ':'w','MUSIC':'w','SET_BETA':'ww','DO_ROT_ZV':'','STAGE':'wwwww','FOUND_NAME':'w','FOUND_FLAG':'w','FOUND_LIFE':'w',
'CAMERA_TARGET':'w','DROP':'ew','FIRE':'ewwwwwe','TEST_COL':'w','FOUND_BODY':'w','SET_ALPHA':'ww','DO_MAX_ZV':'','PUT':'wwwwwwwww','DO_NORMAL_ZV':'','DO_CARRE_ZV':'','SAMPLE_THEN':'ww',
'LIGHT':'w','SHAKING':'w','INVENTORY':'w','FOUND_WEIGHT':'w','PUT_AT':'ww','DEF_ZV':'wwwwww','HIT_OBJECT':'ww','ANGLE':'www','REP_SAMPLE':'ww','THROW':'wwwwwww','WATER':'w','PICTURE':'www',
'STOP_SAMPLE':'','NEXT_MUSIC':'w','FADE_MUSIC':'w','STOP_HIT_OBJECT':'','COPY_ANGLE':'w','SAMPLE_THEN_REPEAT':'ww','WAIT_GAME_OVER':'','GET_MATRICE':'','STAGE_LIFE':'w','CONTINUE_TRACK':'',
'ANIM_RESET':'ww','RESET_MOVE_MANUAL':'','PLUIE':'w','ANIM_HYBRIDE_ONCE':'ww','ANIM_HYBRIDE_REPEAT':'ww','MODIF_C_VAR':'we','CALL_INVENTORY':'','BODY_RESET':'ee','DEL_INVENTORY':'','SET_INVENTORY':'w',
'PLAY_SEQUENCE':'www','2D_ANIM_SAMPLE':'eww','SET_GROUND':'w','PROTECT':'','DEF_ABS_ZV':'wwwwww','DEF_SEQUENCE_SAMPLE':'T','READ_ON_PICTURE':'wwwwwwww','FIRE_UP_DOWN':'ewwwwwwe',
'DO_ROT_CLUT':'www','STOP_CLUT':'',
'MESSAGE_VALUE':'ww','CAMERA':'w','STOP_BETA':'','C_VAR':'we','UP_COOR_Y':'','SPEED':'w','GET_HARD_CLIP':'','END_SEQUENCE':''}
A2=['DO_MOVE','ANIM_ONCE','ANIM_ALL_ONCE','BODY','IF_EQUAL','IF_DIFFERENT','IF_SUP_EQUAL','IF_SUP','IF_INF_EQUAL','IF_INF','GOTO','RETURN','END','ANIM_REPEAT','ANIM_MOVE','MOVE','HIT','MESSAGE','VAR','INC','DEC','ADD','SUB','LIFE_MODE','SWITCH','CASE','START_CHRONO','MULTI_CASE','FOUND','LIFE','DELETE','TAKE','IN_HAND','READ','ANIM_SAMPLE','SPECIAL','DO_REAL_ZV','SAMPLE','TYPE','GAME_OVER','MANUAL_ROT','RND_FREQ','MUSIC','SET_BETA','DO_ROT_ZV','STAGE','FOUND_NAME','FOUND_FLAG','FOUND_LIFE','CAMERA_TARGET','DROP','FIRE','TEST_COL','FOUND_BODY','SET_ALPHA','DO_MAX_ZV','PUT','DO_NORMAL_ZV','DO_CARRE_ZV','SAMPLE_THEN','LIGHT','SHAKING','INVENTORY','FOUND_WEIGHT','PUT_AT','DEF_ZV','HIT_OBJECT','ANGLE','REP_SAMPLE','THROW','WATER','PICTURE','STOP_SAMPLE','NEXT_MUSIC','FADE_MUSIC','STOP_HIT_OBJECT','COPY_ANGLE','SAMPLE_THEN_REPEAT','WAIT_GAME_OVER','GET_MATRICE','STAGE_LIFE','CONTINUE_TRACK','ANIM_RESET','RESET_MOVE_MANUAL','PLUIE','ANIM_HYBRIDE_ONCE','ANIM_HYBRIDE_REPEAT','MODIF_C_VAR','CALL_INVENTORY','BODY_RESET','DEL_INVENTORY','SET_INVENTORY','PLAY_SEQUENCE','2D_ANIM_SAMPLE','SET_GROUND','PROTECT','DEF_ABS_ZV','DEF_SEQUENCE_SAMPLE','READ_ON_PICTURE','FIRE_UP_DOWN','DO_ROT_CLUT','STOP_CLUT']
A1=['DO_MOVE','ANIM_ONCE','ANIM_ALL_ONCE','BODY','IF_EQUAL','IF_DIFFERENT','IF_SUP_EQUAL','IF_SUP','IF_INF_EQUAL','IF_INF','GOTO','RETURN','END','ANIM_REPEAT','ANIM_MOVE','MOVE','HIT','MESSAGE','MESSAGE_VALUE','VAR','INC','DEC','ADD','SUB','LIFE_MODE','SWITCH','CASE','CAMERA','START_CHRONO','MULTI_CASE','FOUND','LIFE','DELETE','TAKE','IN_HAND','READ','ANIM_SAMPLE','SPECIAL','DO_REAL_ZV','SAMPLE','TYPE','GAME_OVER','MANUAL_ROT','RND_FREQ','MUSIC','SET_BETA','DO_ROT_ZV','STAGE','FOUND_NAME','FOUND_FLAG','FOUND_LIFE','CAMERA_TARGET','DROP','FIRE','TEST_COL','FOUND_BODY','SET_ALPHA','STOP_BETA','DO_MAX_ZV','PUT','C_VAR','DO_NORMAL_ZV','DO_CARRE_ZV','SAMPLE_THEN','LIGHT','SHAKING','INVENTORY','FOUND_WEIGHT','UP_COOR_Y','SPEED','PUT_AT','DEF_ZV','HIT_OBJECT','GET_HARD_CLIP','ANGLE','REP_SAMPLE','THROW','WATER','PICTURE','STOP_SAMPLE','NEXT_MUSIC','FADE_MUSIC','STOP_HIT_OBJECT','COPY_ANGLE','END_SEQUENCE','SAMPLE_THEN_REPEAT','WAIT_GAME_OVER']
# per game operand overrides
OVR={
 'AITD1':{'FIRE':'wwwwww','SAMPLE':'e','REP_SAMPLE':'ew','SAMPLE_THEN':'ee','SAMPLE_THEN_REPEAT':'ee','IN_HAND':'w','DELETE':'w','READ':'www'},
 'JACK':{'PLAY_SEQUENCE':'w','SAMPLE':'e','SAMPLE_THEN':'ee','IN_HAND':'w','DELETE':'w'},
 'AITD2':{},
 'AITD3':{},
 'TIMEGATE':{'SAMPLE':'ew','REP_SAMPLE':'ew','STOP_SAMPLE':'w','ANIM_SAMPLE':'ewww','PICTURE':'wwew','TAKE':'ewww'},
}
GAMEDIR={'AITD1':'INDARK','JACK':'JACK','AITD2':'INDARK2','AITD3':'INDARK3','TIMEGATE':'TIMEGATE'}
EXTRA={}  # TG ops >= 102 filled by caller
def entries(path):
    out=[]
    for x in explode.load(path):
        if not x: out.append(b''); continue
        m,b5,us,p=x
        out.append(explode.explode(p,us,b5) if m==1 else (p[:us] if m==0 else zlib.decompress(p,-15)))
    return out
def spec_for(game):
    names=A1 if game=='AITD1' else A2
    t={}
    for i,n in enumerate(names): t[i]=(n,OVR[game].get(n,BASE[n]))
    if game=='TIMEGATE': t.update(EXTRA)
    return t
class Over(Exception): pass
def parse(d,spec,var_extra):
    try:
        return _parse(d,spec,var_extra)
    except (IndexError,RecursionError):
        return ('overrun','idx',0,[])
def _parse(d,spec,var_extra):
    w=struct.unpack('<%dh'%(len(d)//2),d[:len(d)//2*2])+(0,)*64; L=len(d)//2; i=0; ops=[]
    def ev():
        nonlocal i
        op=w[i]; i+=1
        if op==-1 or op==0: i+=1; return
        if op&0x8000 or op<0: i+=1
        k=op&0x7fff
        n=var_extra.get(k,0)
        if n=='E': ev()
        else: i+=n
    while i<L:
        op=w[i]; i+=1
        if op<0: i+=1
        k=op&0x7fff
        if k not in spec: return ('unknown',k,i-1,ops)
        name,s=spec[k]; ops.append(name)
        for c in s:
            if c in 'wj': i+=1
            elif c=='e': ev()
            elif c=='M': n=w[i]; i+=1+n+1
            elif c=='T': n=w[i]; i+=1+2*n
            elif c=='L':
                n=w[i]; i+=1
                for _ in range(n): ev()
        if i>L: return ('overrun',name,i,ops)
    return ('ok',None,i,ops)
VAREXTRA={'AITD1':{15:1,17:'E',19:1,29:1,33:1,35:2,37:1,39:1},'A2':{15:1,17:'E',19:1,29:1,33:1,35:1,37:1,38:2}}
def check(game,limit=None,show=5):
    spec=spec_for(game); ve=VAREXTRA['AITD1' if game=='AITD1' else 'A2']
    lifes=entries(ROOT+'/data/%s/LISTLIFE.PAK'%GAMEDIR[game])
    res=collections.Counter(); bad=[]; used=collections.Counter()
    for n,d in enumerate(lifes):
        if not d: continue
        r=parse(d,spec,ve); res[r[0]]+=1; used.update(r[3])
        if r[0]!='ok': bad.append((n,r[:3],r[3][-4:]))
    print(game,dict(res),'scripts',len(lifes))
    for b in bad[:show]: print('  ',b)
    return used,bad
if __name__=='__main__':
    for g in sys.argv[1:]: check(g)
