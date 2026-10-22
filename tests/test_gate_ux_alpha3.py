"""Gate UX on actual native bindings, actual M7 reports and bounded pixels."""
from gate_ui_fixture import *
from PIL import Image,ImageDraw
D=0x38800680;TEXT=0x24062000;FB=0xc0100000;OUTPUT=0x24064000
UIROOT=0x24068000;PAGES=0x24069000

def control(fmt,bpp,w):
 h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:w32(q,q.reg_read(UC_ARM_REG_SP),w),begin=S['gate_ux_control'],end=S['gate_ux_control'])
 call(u,S['gate_ux_control'],FB,320,fmt,bpp);u.hook_del(h)
def telemetry(ch,threshold,gain,left,right,seq=2):
 u.mem_write(D+64*ch,struct.pack('<IIi4fI',0x47444131,seq,threshold,left,right,max(left,right),gain,1))
def state(ch,v):return call(u,S['gate_ux_state'],ch,v&0xffffffff,OUTPUT)
def turn(w,delta):
 clock[0]+=1000;w32(u,0x3001eb3c,clock[0]);return call(u,S['gate_turn'],w,delta&0xffffffff)
setup(1,0,-60000);k=P+0xbe0+0x8f0;w=T+0xde48
# Fine travel is fractional, reversible and shared without editing a threshold.
start=val(k);turn(k,1)
check(val(k)==start and u32(u,0x38800a40+8)==1,'one slow unit is retained fractionally without a threshold jump')
turn(k,-1);check(u32(u,0x38800a40+8)==0xffffffff and val(k)==start,'direction reversal discards the old fraction')
turn(k,1);turn(w,1)
check(u32(u,0x38800a40+8)==0,'MIXER and TRACK share the same per-track fractional travel')
# A known slow native increment takes twice as much travel below -48dB.
setup(1,0,-60000);w32(u,0x38800a40+8,0)
turn(k,80);check(val(k)==-60000,'quiet threshold does not move after half the native increment')
turn(k,80);flush();check(val(k)==-59900 and val(w)==-59900,'quiet threshold moves 0.1dB after twice the native travel and syncs TRACK')
setup(1,0,-20000);turn(k,80);flush()
check(val(k)==-19900 and val(w)==-19900,'upper threshold keeps native encoder travel')
setup(1,0,-96000);turn(k,-1000);turn(k,160);flush()
check(val(k)==-95900 and val(w)==-95900,'Off is a single endpoint; next full fine step enables -95.9dB')
# Real TRACK global encoder dispatch must call our wrapper, other knobs native.
setup(1,2,-60000);call(u,0x81327cc,T,1);before=val(w)
clock[0]+=1000;w32(u,0x3001eb3c,clock[0]);commands.clear()
u.mem_write(TMP,struct.pack('<6I',0x32,0x8148664,0,3,160,0));call(u,0x8133194,T,TMP);flush()
check(val(w)==before+100 and commands==[(2,0x1ae,(before+100)&0xffffffff)],'native TRACK right encoder gets fine travel and one shared Gate commit')
# Non-Gate parameters retain original turn function/return.
seen=[]
h=stub(u,0x8127d50,lambda u:(seen.append((u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1))),u.reg_write(UC_ARM_REG_R0,37)))
old=struct.unpack('<H',u.mem_read(k+0x94,2))[0];u.mem_write(k+0x94,struct.pack('<H',4))
check(call(u,S['gate_turn'],k,17)==37 and seen==[(k,17)],'non-Gate encoder input delegates unchanged to native')
u.hook_del(h);u.mem_write(k+0x94,struct.pack('<H',old))
# Actual M7 reports, not a UI echo: open, closing, closed, bypass, pending/stale.
ch=2;setup(1,ch,-60000);amp=10**(-56/20)
a,_,_=gsetup(ch=ch);gblock(a,[amp]*32,[amp/2]*32,[(0x1ae,-60000)],ch=ch)
u.mem_write(D+64*ch,bytes(a.mem_read(D+64*ch,32)))
check(state(ch,-60000)==2,'open indication comes from actual audio-core gain')
gblock(a,[amp]*32,[amp/2]*32,[(0x1ae,-48000)],ch=ch)
for i in range(75):gblock(a,[amp]*32,[amp/2]*32,ch=ch)
u.mem_write(D+64*ch,bytes(a.mem_read(D+64*ch,32)))
check(state(ch,-48000)==3,'adaptive release produces a real partial-gain FADE indication')
for i in range(4200):gblock(a,[amp]*32,[amp/2]*32,ch=ch)
u.mem_write(D+64*ch,bytes(a.mem_read(D+64*ch,32)))
check(state(ch,-48000)==4,'closed indication follows actual zero gain after release')
check(state(ch,-47000)==0,'an unacknowledged requested threshold is WAIT rather than fabricated closed')
clock[0]+=501;w32(u,0x3001eb3c,clock[0]);check(state(ch,-48000)==0,'stalled audio snapshot becomes WAIT after 500ms')
telemetry(ch,-96000,1.,-1.,-1.,100);check(state(ch,-96000)==1,'settled bypass displays OFF and no invented input level')
telemetry(ch,-60000,0.,amp,amp,101);check(state(ch,-60000)==0,'in-progress odd-sequence snapshot is rejected')
# Both ruler positions are the same function in the same detector reference.
for db,pos in [(-120000,0),(-96000,0),(-72000,24),(-48000,48),(-12000,84),(0,96),(6000,96)]:
 check(call(u,S['gate_ux_pos'],db&0xffffffff,97)==pos,'common -96..0 ruler at '+str(db))
check(call(u,S['gate_ux_pos'],0,0)==0,'empty ruler cannot underflow')
# Native layouts establish the actual available control sizes.
setup(1,ch,-60000)
for panel in (P,T):
 u.mem_write(TMP,struct.pack('<4i',0,0,320,220))
 call(u,u32(u,u32(u,panel)+0x20),panel,TMP)
k=P+0xbe0+ch*0xd98+0x8f0
check(struct.unpack('<2i',u.mem_read(k+12,8))==(71,19),'native MIXER layout supplies a 71x19 threshold field')
check(struct.unpack('<2i',u.mem_read(T+0xde48+12,8))==(48,44),'native TRACK layout supplies a 48x44 Gate control')
# Pixel scope checks for both native rectangle sizes and every supported format.
setup(1,ch,-60000);k=P+0xbe0+ch*0xd98+0x8f0
for fmt,bpp in [(1,4),(2,3),(3,2),(4,2)]:
 for width,height in [(48,44),(80,80)]:
  x,bottom=40,60;top=240-bottom-height
  u.mem_write(k+4,struct.pack('<4i',x,bottom,width,height));telemetry(ch,-60000,.5,amp,amp,102)
  u.mem_write(FB,b'\x55'*(320*240*bpp));control(fmt,bpp,k)
  raw=bytes(u.mem_read(FB,320*240*bpp));changed=False;outside=False
  for yy in range(240):
   for xx in range(320):
    p=raw[(yy*320+xx)*bpp:(yy*320+xx+1)*bpp]
    if p!=b'\x55'*bpp:
     changed=True;outside|=not(x<=xx<x+width and top<=yy<top+height)
  check(changed and not outside,'Gate tile writes only its native widget rectangle '+str((fmt,width,height)))
# Hidden, disabled, invalid rectangles, Main and unrelated pages draw nothing.
u.mem_write(k+4,struct.pack('<4i',40,60,48,44))
for offset,value in [(0x30,1),(0x2d4,1)]:
 u.mem_write(k+offset,bytes([value]));u.mem_write(FB,bytes(320*240*4));control(1,4,k)
 check(not any(u.mem_read(FB,320*240*4)),'hidden/disabled Gate controls are not overwritten '+hex(offset));u.mem_write(k+offset,b'\0')
u.mem_write(k+0x32,struct.pack('<H',12));u.mem_write(FB,bytes(320*240*4));control(1,4,k)
check(not any(u.mem_read(FB,320*240*4)),'Main remains outside Gate UX');u.mem_write(k+0x32,struct.pack('<H',ch))
w32(u,0x3000d9fc,UIROOT);w32(u,UIROOT+0x2b4,T+0x100)
u.mem_write(FB,bytes(320*240*4));call(u,S['gate_ux_draw'],FB,320,1,4)
check(not any(u.mem_read(FB,320*240*4)),'no Gate drawing or diagnostic footer on unrelated menus')
w32(u,UIROOT+0x2b4,P);call(u,S['gate_ux_draw'],FB,320,1,4)
check(not any(u.mem_read(FB,320*240*4)),'MIXER Gate page has no custom overlay; native headers and values remain visible')
call(u,S['gate_mix_setup'],P,0);call(u,S['gate_mix_select'],P,0)
u.mem_write(FB,bytes(320*240*4));call(u,S['gate_ux_draw'],FB,320,1,4)
check(not any(u.mem_read(FB,320*240*4)),'native VOL page drawing is unchanged')
# Bounded redraw even after audio/Vol meter stops changing.
w32(u,UIROOT+0x2b4,T);u.mem_write(PAGES,bytes(64))
call(u,S['gate_ux_tick'],PAGES);check(bytes(u.mem_read(PAGES+0x38,2))==b'\1\1','opening Gate view requests a redraw')
u.mem_write(PAGES+0x38,b'\0\0');call(u,S['gate_ux_tick'],PAGES)
check(bytes(u.mem_read(PAGES+0x38,2))==b'\0\0','Gate redraw is rate limited within 50ms')
clock[0]+=50;w32(u,0x3001eb3c,clock[0]);call(u,S['gate_ux_tick'],PAGES)
check(bytes(u.mem_read(PAGES+0x38,2))==b'\1\1','Gate indicator redraws at 20Hz even on steady silence')
w32(u,UIROOT+0x2b4,T+0x100);u.mem_write(PAGES+0x38,b'\0\0');call(u,S['gate_ux_tick'],PAGES)
check(bytes(u.mem_read(PAGES+0x38,2))==b'\0\0','unrelated menus get no Gate redraw work')
# Full presentation integration executes Gate draw through cpu_present's C call.
setup(1,ch,-60000);w=T+0xde48;w32(u,UIROOT+0x2b4,T)
u.mem_write(w+4,struct.pack('<4i',170,1,48,44));telemetry(ch,-60000,1.,amp,amp,120)
DESC=0x24065000;u.mem_write(DESC,struct.pack('<IHHBBH',FB,320,240,1,1,4))
flips=[];hb=stub(u,0x810139a,lambda q:q.reg_write(UC_ARM_REG_R0,DESC))
hf=stub(u,0x810134c,lambda q:flips.append(1))
u.mem_write(FB,bytes(320*240*4));call(u,S['cpu_present'],UIROOT,0,0,0)
raw=bytes(u.mem_read(FB,320*240*4));u.hook_del(hb);u.hook_del(hf)
check(flips==[1] and any(raw),'screen-present hook draws Gate then preserves exactly one native flip')
check(not any(raw[:195*320*4]) and not any(raw[239*320*4:]),'full presentation writes only the native Gate footprint; no diagnostic footer')
# Internal QA image: pixels come directly from the compiled ARM widget draw.
setup(1,ch,-60000);w=T+0xde48;u.mem_write(FB,bytes(320*240*4))
for col,(gain,th,left,label) in enumerate([(1.,-60000,amp,'OPEN'),(.4,-48000,amp,'FADE'),(0.,-48000,amp,'CLOSED'),(1.,-96000,-1.,'OFF')]):
 call(u,0x8127b84,w,0x1ae,th&0xffffffff,1);u.mem_write(w+4,struct.pack('<4i',15+col*76,150,48,44))
 telemetry(ch,th,gain,left,left,104+col*2);control(1,4,w)
 k=P+0xbe0+ch*0xd98+0x8f0;call(u,0x8127b84,k,0x1ae,th&0xffffffff,1)
 u.mem_write(k+4,struct.pack('<4i',3+col*79,90,71,19));control(1,4,k)
img=Image.frombytes('RGBA',(320,240),bytes(u.mem_read(FB,320*240*4)),'raw','BGRA').convert('RGB')
img.resize((960,720),Image.Resampling.NEAREST).save(ROOT/'tests/gate-ui-preview.png')
print(str(H.checks)+' Gate UX/fine-travel/native-routing/audio-readback/ruler/pixel-scope checks passed; physical retest remains required.')
