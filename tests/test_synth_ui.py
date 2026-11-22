"""Physical TRACK/A/B, real Sidechain list row edits and independent controls."""
from gate_ui_fixture import *
import firmware_harness as H
IU=0x388001e0;EV=TMP+0x500;SC=0xc0100000
NORMAL_OFFS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48]
OFFS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xda78,0xd2d8,0xde48]
BUTTON=T+0xe814
NORMAL=[4,3,0x63,0x15d,0xda,0xdb,0x15e,0x1ae]
SYN=[0x1bf,0x1c3,0x1c2,0x1c4,0x1c5,0x1c9,0x1c6,0x1dd] # S18: Reso in Drone's place
PAGE2=[0x1ca,0x1d7,0x1cc,0x1d9] # S13: LFO Rate, LFO2 Rate, LFO Amp, LFO2 Amp (even slots top: LFO top, LFO2 bottom)
setup(0,7,-96000);u.mem_write(APP+0x7398,struct.pack('<H',7));u.mem_write(APP+0x739a,b'\x05')
original_captions=[bytes(u.mem_read(T+off+0xd1,32)).split(b'\0')[0] for off in NORMAL_OFFS]
stub(u,0x8102a4c,lambda q:None);stub(u,0x8108950,lambda q:None)
def input(kind,index=0,delta=0):
 notifications.clear()
 u.mem_write(EV,struct.pack('<6I',kind,0x8148664,0,index,delta&0xffffffff,0))
 call(u,0x8123862,APP,EV)
 return notifications[:]
def refresh(ch=7):
 u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,ch,0,0,0));call(u,u32(u,0x814bb70),T,EV)
def bind(i):return struct.unpack('<H2xi',u.mem_read(T+OFFS[i]+0x94,8))
def hidden(i):return u.mem_read(T+OFFS[i]+0x30,1)==b'\1'
def turn(enc,delta):
 commands.clear();notifications.clear()
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,enc,delta&0xffffffff,0))
 call(u,u32(u,0x814bb70),T,EV);flush()
def a():
 input(12)
 group=u.mem_read(APP+0x739a,1)[0]==6
 call(u,S['insert_track_group'],T,int(group))
 # Real root routing sends 0x67 after the focus observer, which must rebind
 # the page immediately without a selected-track change or a knob turn.
 u.mem_write(EV,struct.pack('<6I',0x67,0x8148664,0,0,0,0));call(u,u32(u,0x814bb70),T,EV)
def tap_synth():
 commands.clear();notifications.clear()
 call(u,u32(u,u32(u,BUTTON)+0x18),BUTTON,u32(u,BUTTON+4)+10,u32(u,BUTTON+8)+10)
 flush()
def label_present(text):return text in bytes(u.mem_read(BUTTON,0xf8))
def rect(i):return struct.unpack('<4i',u.mem_read(T+OFFS[i]+4,16))

e=input(0xf9,1)
check(u.mem_read(APP+0x739a,1)==b'\x16' and u32(u,IU+4)==0,'hardware TRACK from normal opens original Sidechain')
input(0xf9,1);refresh()
check(u32(u,IU+4)==2 and all(hidden(i) for i in range(8)) and u.mem_read(BUTTON+0x30,1)==b'\0' and label_present(b'SYN OFF') and u.mem_read(u32(u,0x38800c08)+0x30,1)==b'\1','Synth Off shows a native button and hides all synth knobs and the insert group button')
turn(0,1000)
check(call(u,0x8120234,APP+0xc08+7*0x30,0x1bf)==0 and not commands,'hidden old On knob cannot change source from an encoder')
k=rect(0);sb=tuple(struct.unpack('<4i',u.mem_read(BUTTON+4,16)))
check(sb==(k[0],k[1]+1,k[2]-1,k[3]-1),'SYN ON/OFF button uses the stock upper-row border inset in the Level cell '+str((k,sb)))
tap_synth()
check(commands[:1]==[(7,0x1bf,1)] and [c[1] for c in commands[1:]]==[0x112] and label_present(b'SYN ON') and u.mem_read(BUTTON+0x96,1)==b'\1','native button activation toggles Synth On once through saved model/IPC')
check([bind(i)[0] for i in range(1,8)]==SYN[1:] and hidden(0) and all(not hidden(i) for i in range(1,8)),'Synth On shows Cutoff instead of Level and the existing macros/AR')
check(all(struct.unpack('<H',u.mem_read(T+off+0x32,2))[0]==7 for off in OFFS),'all synth controls target selected track')
for encoder in range(4):
 left=rect(encoder);right=rect(encoder+4)
 check(right==(left[0]+96,left[1],left[2],left[3]),'right encoder position matches the left bank geometry '+str(encoder))
for enc in (1,2,3):
 before=bind(enc)[1];turn(enc,80)
 check(bind(enc)[1]!=before and commands[-1][:2]==(7,SYN[enc]),'left encoder edits correct synth macro '+str(enc))
# Exact four-group cycle and physical encoder order.
a()
check(u32(u,IU+12)==1 and u32(u,IU+28)==0,'first A reaches Attack/Cutoff/Release/Drone on the right')
for enc in (0,1,2):
 before=bind(enc+4)[1];turn(enc,-80)
 check(bind(enc+4)[1]!=before and commands[-1][:2]==(7,SYN[enc+4]),'page1 right encoder edits requested Attack/Cutoff/Release position '+str(enc))
a()
check(u32(u,IU+12)==0 and u32(u,IU+28)==1 and [bind(i)[0] for i in range(4)]==PAGE2,'second A reaches the left LFO and LFO2 controls')
check(all(not hidden(i) for i in range(7)) and hidden(7) and [bind(i)[0] for i in (4,5,6)]==[0x1ce,0x1d0,0x1cf] and u.mem_read(BUTTON+0x30,1)==b'\1','page2 shows the four LFO controls left and Env Attack / Env Amount / Env Decay right (S12)')
# S15: Seed sits in the last right slot, shown only while the engine is Cluster.
engine0=call(u,0x8120234,APP+0xc08+7*0x30,0x1c0)
call(u,0x8120264,APP+0xc08+7*0x30,0x1c0,3);refresh()
check(not hidden(7) and bind(7)[0]==0x1d4,'with Cluster, page 2 shows Seed in the last right slot')
# 1.3 dev: Seed reads SEED at rest, its number while turning, SEED again ~1 s later.
seedtext=lambda:bytes(u.mem_read(T+OFFS[7]+0xd1,8)).split(b'\0')[0]
CLOCK=0x3001eb3c;w32(u,CLOCK,20000);PAGES=TMP+0x800;u.mem_write(PAGES,bytes(0x40))
check(seedtext()==b'SEED','Seed shows SEED at rest '+str(seedtext()))
call(u,0x8120264,APP+0xc08+7*0x30,0x1d4,6);refresh();call(u,S['insert_knob_text'],T+OFFS[7],0)
check(seedtext()==b'7','turning Seed shows its number (seed 7) '+str(seedtext()))
refresh();check(seedtext()==b'7','a rebinding within the second keeps the number')
w32(u,CLOCK,u32(u,CLOCK)+1100);call(u,S['insert_caption_tick'],PAGES)
check(seedtext()==b'SEED' and u.mem_read(PAGES+0x38,1)==b'\1','~1 s later the UI tick brings SEED back and asks for a redraw')
call(u,0x8120264,APP+0xc08+7*0x30,0x1d4,0)
call(u,0x8120264,APP+0xc08+7*0x30,0x1c0,engine0);refresh()
check(hidden(7),'with another engine, the Seed slot stays empty')
caption=lambda i:bytes(u.mem_read(T+OFFS[i]+0xf8,8)).split(b'\0')[0]
shown=lambda i:bytes(u.mem_read(T+OFFS[i]+0xd1,8)).split(b'\0')[0]
check([caption(i) for i in range(4)]==[b'LFO S',b'LFO2 S',b'',b'LFO2 A'] and [shown(i) for i in (0,1,3)]==[b'LFO S',b'LFO2 S',b'LFO2 A'],'S14: the rate knobs read LFO S / LFO2 S, the LFO2 amount LFO2 A')
for enc in range(4):
 before=bind(enc)[1];turn(enc,80)
 check(bind(enc)[1]!=before and commands[-1][:2]==(7,PAGE2[enc]),'page2 left encoder edits exact requested parameter '+str(enc))
# S14: LFO amounts accelerate on fast turns; a slow turn stays one step.
def amount_step(enc,delta):
 call(u,0x8127b84,T+OFFS[enc],PAGE2[enc],500,1);before=bind(enc)[1];turn(enc,delta);return bind(enc)[1]-before
check(amount_step(2,40)==1 and amount_step(3,40)==1,'a slow turn moves LFO A / LFO2 A by one step')
check(amount_step(2,320)==8 and amount_step(3,320)==8,'S23: LFO A / LFO2 A step like HARM (native only, no extra scaling) (%d)'%amount_step(2,320))
a()
check(u32(u,IU+12)==1 and u32(u,IU+28)==1,'third A reaches right Env Attack / Env Amount / Env Decay')
turn(0,-400)
check(bind(4)[1]<0 and commands[-1][:2]==(7,0x1ce),'right encoder 1: modulation envelope amount supports negative as well as positive depth')
turn(1,1000)
check(bind(5)[0]==0x1d0 and bind(5)[1]>0 and commands[-1][:2]==(7,0x1d0),'right encoder 2 controls the separate modulation envelope attack')
turn(2,400);check(commands and commands[-1][:2]==(7,0x1cf),'right encoder 3 controls Env Decay')
turn(3,400);check(not commands,'the unused right-side encoder cannot change hidden values')
# Ring travel must match the logarithmic musical sweep, rather than linear Hz.
# Use an isolated real numeric widget so saved project values remain unchanged.
RING=0xc0300000
for value,expected in [(20,0.),(200,1./3.),(2000,2./3.),(20000,1.)]:
 u.mem_write(RING+0x32,struct.pack('<H',7));u.mem_write(RING+0x94,struct.pack('<H2xi',0x1c9,value))
 call(u,S['dly_knob_fraction'],RING)
 fraction=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]
 check(abs(fraction-expected)<.0001,'cutoff ring follows logarithmic frequency '+str(value))
for value in (0,1,10,100,1000,5000):
 u.mem_write(RING+0x94,struct.pack('<H2xi',0x1d0,value));call(u,S['dly_knob_fraction'],RING)
 fraction=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]
 check(abs(fraction-math.log((value+200)/200,26))<.0001,'Env Attack ring matches 200 ms-offset logarithmic travel '+str(value))
for value,expected in [(1,0.),(5,0.),(50,1./3.),(500,2./3.),(5000,1.),(9000,1.)]:
 u.mem_write(RING+0x94,struct.pack('<H2xi',0x1cf,value));call(u,S['dly_knob_fraction'],RING)
 fraction=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]
 check(abs(fraction-expected)<.0001,'Env Decay ring follows the logarithmic 5-5000 ms travel like Cutoff (S20) '+str(value))
for pid,lo,hi,off in ((0x1c5,1,2000,50),(0x1c6,5,5000,0)):   # S25: Attack over ms+50
 for value in (lo,(lo*hi)**.5,hi):
  u.mem_write(RING+0x94,struct.pack('<H2xi',pid,int(round(value))));call(u,S['dly_knob_fraction'],RING)
  fraction=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]
  check(abs(fraction-math.log((round(value)+off)/(lo+off),(hi+off)/(lo+off)))<.002,'S23: %s ring on its log travel at %d ms'%('Attack' if pid==0x1c5 else 'Release',round(value)))
# B opens the real Sidechain style menu rather than another knob page.
e=input(7)
check(u32(u,IU+4)==3 and u.mem_read(APP+0x739a,1)==b'\x17' and struct.unpack_from('<H',e[0])[0]==0x8f and struct.unpack_from('<I',e[0],12)[0]==1,'physical B opens native Sidechain settings mode for synth')
call(u,0x81284f0,SC);call(u,0x81285ec,SC,1)
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,7,0,0,0));call(u,0x8128ff4,SC,EV)
call(u,S['synth_menu_populate'],SC,EV)
LIST=SC+0xbd38
check(u32(u,LIST+0x99e8)==10,'native list contains ten settings rows (S15: Seed to page 2; S18: Drone from page 1)')
# Native list row IDs are in each row's parameter widget; test real turns and
# exact saved model/IPC changes for all four rows, including all destinations.
expected=[0x1c0,0x1c1,0x1c8,0x1cd,0x1cb,0x1d1,0x1d3,0x1d8,0x1da,0x1dc]
for row,pid in enumerate(expected):
 w32(u,LIST+0x99ec,row)
 commands.clear();notifications.clear()
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,1,400,0));call(u,0x8128ff4,SC,EV)
 check(commands and commands[-1][:2]==(7,pid),'native selected row encoder saves expected synth setting '+str(row))
for row in (0,2,3,4,5,6,7,8,9):
 w32(u,LIST+0x99ec,row)
 for target in ((2,3,2,1,0) if row in (5,8) else (1,0,1,0) if row in (0,2,6,9) else (2,3,4,3,2,1,0)):
  current=call(u,0x8120234,APP+0xc08+7*0x30,expected[row]);delta=400 if target>current else -400
  for repeat in range(20):
   if call(u,0x8120234,APP+0xc08+7*0x30,expected[row])==target:break
   u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,1,delta&0xffffffff,0));call(u,0x8128ff4,SC,EV)
  check(call(u,0x8120234,APP+0xc08+7*0x30,expected[row])==target,'native destination enum reaches '+str(target)+' row '+str(row))
# Run the actual parent redraw poll repeatedly; a broken snapshot causes
# list regeneration and jumps away from a later selected row.
w32(u,LIST+0x99ec,3);u.mem_write(SC+0xbcbc,b'\0\0')
stub(u,0x813a1f8,lambda q:None) # display child traversal boundary
u.mem_write(EV,bytes(24))
for frame in range(20):call(u,0x8128ebc,SC,EV)
check(u32(u,LIST+0x99ec)==3,'actual native redraw poll keeps later menu row selected')
check(u32(u,SC+0xbcc4)==call(u,0x8120234,APP+0xc08+7*0x30,0x23),'native Sidechain poll snapshot cannot spuriously reset synth menu selection')
# Title substitution still uses native rendering boundary.
titles=[]
def title(q):titles.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R0),32)).split(b'\0')[0])
stub(u,0x8136aa4,title);call(u,S['synth_menu_title'],0x8146a84,TMP,3,0)
check(titles[-1]==b'Synth','menu displays Synth title')
a_before=(u32(u,IU+12),u32(u,IU+28));input(12)
check((u32(u,IU+12),u32(u,IU+28))==a_before,'A does not change hidden synth banks while menu is open')
input(7);refresh()
check(u32(u,IU+4)==2 and u32(u,IU+28)==1 and u.mem_read(APP+0x739a,1)==b'\x06','B closes native settings menu to the same synth page and focus')
a();check(u32(u,IU+12)==0 and u32(u,IU+28)==0,'fourth A returns to first-page left synth controls')
# Every bank survives a B-menu round trip with its own focus/page intact.
for index in range(4):
 expected_group=index%2;expected_page=index//2
 check((u32(u,IU+12),u32(u,IU+28))==(expected_group,expected_page),'four-group cycle before menu '+str(index))
 input(7);input(7);refresh()
 check((u32(u,IU+12),u32(u,IU+28))==(expected_group,expected_page) and u.mem_read(APP+0x739a,1)==bytes([6 if expected_group else 5]),'B restores focus and page for group '+str(index))
 a()
# B's LFO Sync row is Free/Sync; in Sync the speed encoder edits divisions.
input(7)
call(u,0x8120264,APP+0xc08+7*0x30,0x1d2,0)
u.mem_write(EV,struct.pack('<6I',0x3d,0x8148664,7,1,0x1d3,0));call(u,S['synth_menu_local'],SC,EV)
input(7);refresh();a();a()
check(u32(u,IU+12)==0 and u32(u,IU+28)==1 and bind(0)==(0x1d2,9),'Sync replaces free LFO speed with the 1/4 division')
turn(0,-400);check(commands[-1]==(7,0x1d2,8),'speed encoder changes the beat division while sync is active')
for repeat in range(20):turn(0,-400)
check(call(u,0x8120234,APP+0xc08+7*0x30,0x1d2)==1 and bind(0)[0]==0x1d2,'turning the division all the way down stops at 1/64 and stays in Sync')
turn(0,400);check(commands[-1]==(7,0x1d2,2),'division encoder turns back up from 1/64')
input(7)
u.mem_write(EV,struct.pack('<6I',0x3d,0x8148664,7,0,0x1d3,0));call(u,S['synth_menu_local'],SC,EV)
input(7);refresh()
check(bind(0)[0]==0x1ca and call(u,0x8120234,APP+0xc08+7*0x30,0x1d2)==14,'Free restores the separate LFO speed and remembers the 1/32 division')
# S10: LFO2 has its own B-menu Free/Sync row; its knob (S13: left group, second
# encoder) then walks LFO2 divisions and never touches LFO1's.
input(7)
call(u,0x8120264,APP+0xc08+7*0x30,0x1db,0)
u.mem_write(EV,struct.pack('<6I',0x3d,0x8148664,7,1,0x1dc,0));call(u,S['synth_menu_local'],SC,EV)
check(call(u,0x8120234,APP+0xc08+7*0x30,0x1db)==9 and call(u,0x8120234,APP+0xc08+7*0x30,0x1dc)==1 and call(u,0x8120234,APP+0xc08+7*0x30,0x1d2)==14,'LFO2 Sync stores the 1/4 division in LFO2 Div only')
input(7);refresh()
check(u32(u,IU+12)==0 and u32(u,IU+28)==1 and bind(1)==(0x1db,9) and bind(0)[0]==0x1ca,'LFO2 Sync replaces the LFO2 Rate knob with its division; LFO1 Rate stays')
check(bytes(u.mem_read(T+OFFS[1]+0xf8,8)).split(b'\0')[0]==b'LFO2 D','the LFO2 division knob is captioned LFO2 D')
turn(1,-400);check(commands[-1]==(7,0x1db,8) and call(u,0x8120234,APP+0xc08+7*0x30,0x1d2)==14,'the LFO2 knob changes the LFO2 division, not LFO1')
input(7)
u.mem_write(EV,struct.pack('<6I',0x3d,0x8148664,7,0,0x1dc,0));call(u,S['synth_menu_local'],SC,EV)
input(7);refresh()
check(bind(1)[0]==0x1d7 and call(u,0x8120234,APP+0xc08+7*0x30,0x1db)==20,'LFO2 Free restores LFO2 Rate and remembers its 1/8 division')
a();a();a();a()
check(u32(u,IU+12)==0 and u32(u,IU+28)==1,'back on page 2 left')
# Env Attack: logarithmic travel with a 200 ms offset; the first detent moves
# a few ms, and every detent advances the ring by a similar amount.
a()
check(bind(5)[0]==0x1d0,'second-page right second encoder is Env Attack')
for repeat in range(40):turn(1,-1000)
check(call(u,0x8120234,APP+0xc08+7*0x30,0x1d0)==0,'Env Attack reaches 0 ms')
values=[0]
for repeat in range(12):
 turn(1,400);values.append(call(u,0x8120234,APP+0xc08+7*0x30,0x1d0))
check(5<=values[1]<=9 and all(b>a for a,b in zip(values,values[1:])) and 80<=values[-1]<=130,'Env Attack moves steadily from 0 ms '+str(values))
for repeat in range(60):turn(1,-1000)
check(call(u,0x8120234,APP+0xc08+7*0x30,0x1d0)==0,'Env Attack returns to 0 ms')
# S21: Env Dec gets the native knob acceleration HARM has: slow detents one
# log step each, a fast spin up to the native multiplier (as HARM: 30 / 53).
TICK=0x3001eb3c
def decay_spin(ms):
 call(u,0x8120264,APP+0xc08+7*0x30,0x1cf,500);refresh();w32(u,TICK,u32(u,TICK)+100000);before=bind(6)[1]
 for i in range(30):w32(u,TICK,u32(u,TICK)+ms);turn(2,40)
 return round(1000*math.log(bind(6)[1]/before,1000))
check(bind(6)[0]==0x1cf and decay_spin(200)==31 and decay_spin(20)==53,'Env Dec: slow detents step once, a fast spin accelerates like HARM (S21)')
def attack_spin(ms):
 call(u,0x8120264,APP+0xc08+7*0x30,0x1d0,500);refresh();w32(u,TICK,u32(u,TICK)+100000);before=bind(5)[1]
 for i in range(30):w32(u,TICK,u32(u,TICK)+ms);turn(1,40)
 return round(1000*math.log((bind(5)[1]+200)/(before+200),26))
slow,fast=attack_spin(200),attack_spin(20)
check(bind(5)[0]==0x1d0 and 24<=slow<=31 and fast>=1.7*slow,'Env Attack: the same native acceleration (S22; also Cutoff and LFO speeds) %d/%d'%(slow,fast))
w32(u,TICK,0)
a() # return first-page left for the original enable-button tests
refresh(2);check(all(hidden(i) for i in range(8)) and label_present(b'SYN OFF'),'changing track shows independent Off button')
refresh(7);check(label_present(b'SYN ON'),'returning to track restores Synth On button')
tap_synth();check(commands[:1]==[(7,0x1bf,0)] and [c[1] for c in commands[1:]]==[0x112] and all(hidden(i) for i in range(8)),'button toggles Synth Off and hides parameters')
tap_synth();check(commands[:1]==[(7,0x1bf,1)] and [c[1] for c in commands[1:]]==[0x112] and all(not hidden(i) for i in range(1,8)),'button re-enables Synth and restores controls')
input(0xf9,1);call(u,S['insert_track_group'],T,0);refresh()
check(u32(u,IU+4)==0 and [struct.unpack('<H',u.mem_read(T+off+0x94,2))[0] for off in NORMAL_OFFS]==NORMAL,'TRACK returns from Synth to normal controls')
check([bytes(u.mem_read(T+off+0xd1,32)).split(b'\0')[0] for off in NORMAL_OFFS]==original_captions,'all normal captions restore including Gate')
check(struct.unpack('<H',u.mem_read(BUTTON+0xfa,2))[0]==0x173 and label_present(b'GAIN') and u.mem_read(BUTTON+0x30,1)==b'\1' and tuple(struct.unpack('<4i',u.mem_read(BUTTON+4,16)))==rect(3),'normal GAIN button binding, label, hidden state and geometry restore')
e=input(7);check(struct.unpack_from('<H',e[0])[0]==0x8d,'normal B retains input configuration')
# Sidechain list path remains original outside synth mode.
call(u,S['synth_menu_populate'],SC,EV);check(u32(u,LIST+0x99e8)!=7,'normal Sidechain populates its original source-dependent list')
# F2: two engines on the native definition; labels past the count keep older
# projects readable (2 VA, 3 Cluster, 4 Swarm before S14 = Cluster).
d=machine();dheap=[0xc0010000]
def dalloc(q):
 a=dheap[0];dheap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(d,0x8103128,dalloc);call(d,0x8134cb8,0)
meta=0xc0010000+28*0x1c0;choices=u32(d,meta+8)
names=[bytes(d.mem_read(u32(d,choices+4*i),20)).split(b'\0')[0].decode() for i in range(5)]
check(u32(d,meta+12)==2 and names==['VA','Cluster','VA','Cluster','Cluster'],'Engine offers VA and Cluster; older ordinals show what plays '+str(names))
print(H.checks,'native synth menu, navigation and encoder checks passed; LCD/touch need hardware testing.')
