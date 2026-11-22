"""S9c: MIDI Learn on the Gate threshold knob of the stock TRACK page (OUT3's
knob, rebound by gate_track_bind). Same boundaries as test_s9a_learn_insert:
real TRACK view, native learn info and overlay, SessionMgr arm and CC learn."""
from gate_ui_fixture import *
import firmware_harness as H
EV=TMP+0x400;MSG=TMP+0x700;MAP=0x24070000;GATE=T+0xde48;NG=0x1ae
global_fn=u32(u,0x814bb70)
stub(u,0x8102a4c,lambda q:None);stub(u,0x8108950,lambda q:None)
sent=[];stub(u,0x810884a,lambda q:sent.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
def refresh(ch=7):
 u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,ch,0,0,0));call(u,global_fn,T,EV)
def overlay(w):
 o=w+0x2f4
 return u.mem_read(o+0x30,1)[0]==0,u32(u,o+0xc0)==0x1b,bytes(u.mem_read(o+0x65,16)).split(b'\0')[0]
def learn_tap(w):
 parents.clear();u.mem_write(EV,struct.pack('<6I',0x13e,0x8148664,0,0,0,0))
 call(u,0x8127e98,w,EV)
 posted=[p for p in parents if struct.unpack_from('<H',p)[0]==0x13e]
 for p in posted:u.mem_write(EV,p);call(u,0x8125d44,APP,EV)
 refresh()
 return [(a&0xffff,b) for a,b in (struct.unpack_from('<HxxIII',p)[2:] for p in posted)] # cell is a u16 in that word
def learn_cc(cc,chan=0):
 u.mem_write(MSG,bytes(0x18));u.mem_write(MSG+0xc,struct.pack('<II',chan,0xb0));u.mem_write(MSG+0x14,struct.pack('<H',cc))
 call(u,0x8123e44,APP,MSG);refresh()
def items():
 return [struct.unpack_from('<HHHHH',bytes(u.mem_read(MAP+i*20,20))) for i in range(u32(u,APP+0x5efc))]
setup(0,7,-96000);w32(u,APP+0x5ef8,MAP);w32(u,APP+0x5efc,0)
refresh()
check(struct.unpack('<H',u.mem_read(GATE+0x94,2))[0]==NG and u.mem_read(GATE+0x30,1)[0]==0,'TRACK page: the eighth knob is the Gate threshold, shown')
check(overlay(GATE)==(False,False,b''),'Learn off: no learn overlay on Gate')
u.mem_write(APP+0x73b3,b'\1');refresh()
check(overlay(GATE)[0],'Learn on: Gate threshold is outlined on TRACK')
e=learn_tap(GATE)
check(e==[(7,NG)] and overlay(GATE)[1],'a tap arms Gate threshold of track 8 '+str(e))
learn_cc(35)
check((0,0xb0,35,NG,7) in items() and overlay(GATE)[2]==b'1:35','the next CC maps to it and shows "1:35"')
m=[x for x in sent if x[0]==0x7c]
check(m and struct.unpack_from('<IIHH',m[-1],12)==(0,0xb0,35,NG) and struct.unpack_from('<H',m[-1],2)[0]==7,'the M7 gets the map item (0x7c) for track 8, Gate threshold')
# S11: the Gate view (gate_ux_control) repaints the knob after the native
# widgets, so the native label never showed on the device. The view now
# draws the learn text itself, in place of the GATE title.
L=GATE+0x2f4+0x34
check(u32(u,L)==0x814cd14,'the native learn label keeps its stock class (S9d vtable copy removed)')
FB=0xc0100000;PAL=S['theme_knob_palette']
def gate_view():
 u.mem_write(FB,bytes(320*240*4))
 h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:w32(q,q.reg_read(UC_ARM_REG_SP),GATE),begin=S['gate_ux_control'],end=S['gate_ux_control'])
 call(u,S['gate_ux_control'],FB,320,1,4);u.hook_del(h)
 return bytes(u.mem_read(FB,320*240*4))
def px(fb,x,y):return struct.unpack_from('<I',fb,(y*320+x)*4)[0]&0xffffff
x,bottom,width,height=struct.unpack('<4i',u.mem_read(GATE+4,16));y=240-bottom-height+(height-44)//2
GLYPH={'1':(4,12,4,4,4,4,14),':':(0,6,6,0,6,6,0),'3':(30,1,1,14,1,1,30),'5':(31,16,16,30,1,1,30)}
text='1:35';tw=len(text)*6-1;bx=x+(width-tw)//2-2;tx=x+(width-tw)//2
black=u32(u,PAL+14*4)&0xffffff;active=u32(u,PAL+27*4)&0xffffff
want={}
for yy in range(y+2,y+13):
 for xx in range(bx,bx+tw+4):want[(xx,yy)]=black
for k,c in enumerate(text):
 for r,bits in enumerate(GLYPH[c]):
  for col in range(5):
   if bits&(16>>col):want[(tx+6*k+col,y+4+r)]=active
fb=gate_view()
bad=[k for k,v in want.items() if px(fb,*k)!=v]
check(not bad,'TRACK Gate view shows "1:35" in the control-active colour on a black box where GATE was (%d px, %d wrong)'%(len(want),len(bad)))
check(sum(1 for v in want.values() if v==active)==sum(bin(b).count('1') for c in text for b in GLYPH[c]),'every glyph pixel of "1:35" is drawn')
u.mem_write(APP+0x73b3,b'\0');refresh()
check(not overlay(GATE)[0],'Learn off: the outline goes')
fb=gate_view()
check(all(px(fb,xx,yy)!=black for yy in range(y+2,y+13) for xx in range(x+2,x+width-2)),'Learn off: no black learn box, the GATE title is back')
print(H.checks,'S9c Gate TRACK MIDI Learn checks passed; controller response needs hardware.')
