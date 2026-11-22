"""S24 Tools "Track Knob 8" (settings tag 0x1e8): Gate (default) keeps Gate
on TRACK's eighth knob; OUT3 gives it back to the native OUT3 send while
OUT2 Mode is Mono (the only mode with OUT3). Real MIXER/TRACK constructors
and track binding (gate_ui_fixture); only the settings store is built here."""
from gate_ui_fixture import *
import firmware_harness as H
SETS=APP+0x737c;KNOB8=T+0xde48;GD_TRACK=0x38800a40
def get(s,i):return call(u,0x8120234,s,i)
def put(s,i,v):call(u,0x8120264,s,i,v&0xffffffff)
w32(u,SETS+4,0x24059000);u.mem_write(SETS+10,struct.pack('<HH',1,8));u.mem_write(0x24059000,struct.pack('<H2xi',0x1e8,0))
d=M['symbols']['4']
def knob8(mono,option,ch=3):
 put(SETS,0x1e8,option);setup(0 if mono else 1,ch,-40000)
 return struct.unpack('<H2xi',u.mem_read(KNOB8+0x94,8)),u.mem_read(KNOB8+0x30,1)[0]
(i,v),hidden=knob8(1,0)
check(i==0x1ae and v==-40000 and not hidden,'default Gate: Mono TRACK eighth knob is Gate (as before)')
(i,v),hidden=knob8(1,1)
check(i==0x15f and not hidden,'Track Knob 8 = OUT3, Mono: the eighth knob is the native OUT3 send')
(i,v),hidden=knob8(0,1)
check(i==0x1ae and v==-40000,'Track Knob 8 = OUT3, Stereo (no OUT3): Gate stays')
(i,v),hidden=knob8(1,0,5)
check(i==0x1ae and v==-40000,'back to Gate: the next track bind shows Gate again')
# S25: A cycles normal left -> normal right -> inserts left -> inserts right
# -> third page (Mono only: the eighth knob alone, with whichever of Gate /
# OUT3 page 1 does not show) -> normal left.
IU=0x388001e0;S4=M['symbols']['4'];pages=[]
def page_switch(u):pages.append(u.reg_read(UC_ARM_REG_R1))
stub(u,0x8122508,page_switch)
def refresh(ch=3):
 u.mem_write(TMP,struct.pack('<6I',0x63,0x8148664,ch,0,0,0));call(u,0x8133194,T,TMP)
def a_press():
 pages.clear();call(u,S4['insert_track_a_action'],APP,0,0,0)
 for pg in pages:call(u,S4['insert_track_group'],T,int(pg==6))
 refresh()
 return u32(u,IU+4),u32(u,IU+12)
def shown():return [i for i,off in enumerate([0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48]) if u.mem_read(T+off+0x30,1)==b'\0']
for mono,option,third in ((1,0,0x15f),(1,1,0x1ae)):
 put(SETS,0x1e8,option);setup(0 if mono else 1,3,-40000)
 call(u,S4['insert_track_group'],T,0);w32(u,IU+4,0);refresh()
 states=[a_press() for i in range(4)]
 check([s[0] for s in states]==[0,1,1,4] and states[3][1]==1,'Mono, Track Knob 8 = %s: A reaches the third page after inserts right %s'%(('Gate','OUT3')[option],states))
 i,v=struct.unpack('<H2xi',u.mem_read(KNOB8+0x94,8))
 check(shown()==[7] and i==third,'third page: only the eighth knob, showing %s'%('OUT3' if third==0x15f else 'Gate'))
 check(a_press()==(0,0),'A on the third page: back to normal, left')
 i,v=struct.unpack('<H2xi',u.mem_read(KNOB8+0x94,8))
 check(i==(0x1ae if option==0 else 0x15f) and 7 in shown(),'page 1 eighth knob back to %s'%('Gate' if option==0 else 'OUT3'))
put(SETS,0x1e8,0);setup(1,3,-40000);call(u,S4['insert_track_group'],T,0);w32(u,IU+4,0);refresh()
for i in range(3):a_press()
call(u,0x8120264,APP+0xe78,0x18c,1)   # switch to Stereo while on inserts right
check(a_press()[0]==0,'Stereo (no OUT3): no third page, inserts right -> normal')
w32(u,IU+4,4);refresh()
check(u32(u,IU+4)==0,'a third page left over in Stereo falls back to normal')

# S26: Clk Out R uses Out 2 right as a clock: TRACK treats OUT3 as absent
# (Gate on knob 8 whatever Track Knob 8 says, no third page), as in Stereo.
PSET=APP+0xe68;u.mem_write(TMP+0x700,b'\x08');call(u,0x81202b8,PSET,TMP+0x700)   # real kind-8 project set
put(PSET,0x111,3)
(i,v),hidden=knob8(1,1)
check(i==0x1ae and v==-40000,'Clk Out R on, Track Knob 8 = OUT3, Mono: knob 8 is Gate (OUT3 is the clock)')
call(u,S4['insert_track_group'],T,0);w32(u,IU+4,0);refresh()
check([a_press()[0] for i in range(4)]==[0,1,1,0],'Clk Out R on: inserts right -> page 1, no third page')
w32(u,IU+4,4);refresh()
check(u32(u,IU+4)==0,'Clk Out R turned on while on the third page: falls back to page 1')
put(PSET,0x111,0)
(i,v),hidden=knob8(1,1)
check(i==0x15f,'Clk Out R Off again: Track Knob 8 = OUT3 shows OUT3')
put(PSET,0x110,3);(i,v),hidden=knob8(1,1)
check(i==0x15f,'Clk Out L (Out 2 left) does not touch OUT3')
put(PSET,0x110,0);put(SETS,0x1e8,0)

# The definition: label, choices and key resolve through the stock tables.
def cstr(a):return bytes(u.mem_read(a,24)).split(b'\0')[0]
r=bytes(u.mem_read(0xc0010000+0x1e8*28,28));lab,lst,cnt=struct.unpack_from('<III',r,4)
check(struct.unpack_from('<H',r)[0]==0x1e8 and cstr(lab)==b'Track Knob 8:' and cnt==2 and [cstr(u32(u,lst)),cstr(u32(u,lst+4))]==[b'Gate',b'OUT3'] and cstr(u32(u,0xc0010000+0x1e8*28+24))==b'trkknob8','Tools row "Track Knob 8: Gate / OUT3", key trkknob8')
print('Track Knob 8 checks:',H.checks)
