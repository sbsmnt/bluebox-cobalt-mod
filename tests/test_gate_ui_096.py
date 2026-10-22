"""Native channel defaults/XML and real MIXER constructor, row and value controls."""
from firmware_harness import *
import firmware_harness as H
S=M['symbols']['4'];APP=0x300173d0;P=0x24010000;TMP=0x24008000
u=machine();heap=[0xc0010000]
def alloc(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0);call(u,0x8120abc)
def cs(a):return bytes(u.mem_read(a,64)).split(b'\0')[0]
a=0xc0010000+28*0x1ae
check(struct.unpack('<HB',u.mem_read(a,3))==(0x1ae,7) and struct.unpack('<ii',u.mem_read(a+16,8))==(-96000,-12000),'Gate is a native dB threshold, -96 sentinel through -12dB')
check(cs(u32(u,a+4))==b'Gate:' and cs(u32(u,a+24))==b'ngthreshold','Gate label and native project key register')
for ch in range(12):
 u.mem_write(TMP,b'\1');SET=APP+0xc08+ch*0x30;call(u,0x81202b8,SET,TMP)
 n,cap=struct.unpack('<HH',u.mem_read(SET+10,4))
 check(n==52 and cap==68 and call(u,0x8120234,SET,0x1ae)==((-96000)&0xffffffff),'track'+str(ch+1)+' defaults to Off in existing capacity')
w32(u,APP+0xe78+4,0x24058000);u.mem_write(APP+0xe78+10,struct.pack('<HH',1,68));u.mem_write(0x24058000,struct.pack('<H2xi',0x18c,0))
# Full native panel allocation/tree; no widget or drawing math is mocked.
call(u,S['gate_mix_construct'],P)
check(struct.unpack('<HH',u.mem_read(P+0xc804+0x34,4))==(5,5),'original first mixer page retains five native controls')
check(struct.unpack('<HH',u.mem_read(P+0xcb7c+0x34,4))==(6,6),'sends page registers sixth Gate button')
check(struct.unpack('<HH',u.mem_read(P+0xcef4+0x34,4))==(7,7),'Quad sends page keeps six outputs plus Gate')
for quad in [0,1]:
 call(u,0x8120264,APP+0xe78,0x18c,1-quad)
 call(u,S['gate_mix_setup'],P,1)
 base,count=(0xc444,7) if quad else (0xc084,6)
 for i in range(count):
  w=P+(0xbfe4 if i==6 else base+i*0xa0);rect=struct.unpack('<4i',u.mem_read(w+4,16))
  check(rect[0]>=1 and rect[0]+rect[2]<=320 and rect[2]>=44 and rect[3]==39,'sends button fits native screen '+str((quad,i)))
 gate=P+(0xbfe4 if quad else 0xc3a4)
 check(bytes(u.mem_read(gate+0x30,1))==b'\0','sends Gate button is visible for native hit testing '+str(quad))
 check(cs(gate+0x65)==b'Gate','Gate label belongs to sends page '+str(quad))
 # Actual native child release dispatch and patched range/selection calls.
 e=0x24062000;u.mem_write(e,struct.pack('<6I',2,0,0,0x163 if quad else 0x162,0,0))
 call(u,0x812e468,P,e)
 check(struct.unpack('<H',u.mem_read(P+0xbca0,2))[0]==0x1ae,'native sends button opens Gate '+str(quad))
 if quad:
  call(u,S['gate_mix_select'],P,5)
  check(struct.unpack('<H',u.mem_read(P+0xbca0,2))[0]==0x15f,'Quad OUT3 remains available on MIXER')
  call(u,S['gate_mix_select'],P,6)
# First page is unchanged; Gate belongs only to sends selection.
call(u,S['gate_mix_setup'],P,0);call(u,S['gate_mix_select'],P,0)
check(struct.unpack('<H',u.mem_read(P+0xbca0,2))[0]==4,'first page Vol selection retains native map')
call(u,S['gate_mix_setup'],P,1);call(u,S['gate_mix_select'],P,6)
for ch in range(12):
 w=P+0xbe0+ch*0xd98;k=w+0x8f0
 check(struct.unpack('<2f',u.mem_read(w+0x400,8))==(-96.,-12.) and struct.unpack('<H2xi',u.mem_read(k+0x94,8))==(0x1ae,-96000),'native Gate fader/knob binding for track'+str(ch+1))
 check(cs(k+0xd1) in [b'OFF',b'GATE'], 'Gate preserves native label/value presentation, track'+str(ch+1))
w=P+0xbe0+12*0xd98;k=w+0x8f0
check(bytes(u.mem_read(k+0x2d4,1))==b'\1','Main has no editable per-track gate')
EV=0x24062000;u.mem_write(EV,struct.pack('<6I',0x6e,0x8148664,12,0x1ae,(-40000)&0xffffffff,0))
seen=[];stub(u,0x812e754,lambda u:(seen.append(1),u.reg_write(UC_ARM_REG_R0,9)))
check(call(u,S['gate_mix_event'],P,EV)==0 and not seen,'Main gate edits are rejected without reaching native model')
u.mem_write(EV+8,struct.pack('<I',0));check(call(u,S['gate_mix_event'],P,EV)==9 and seen==[1],'channel gate edit retains native event path')
# Switching away must restore Main and track faders through the native binder.
call(u,S['gate_mix_setup'],P,0);call(u,S['gate_mix_select'],P,0)
main=P+0xbe0+12*0xd98
check(bytes(u.mem_read(main+0x8f0+0x2d4,1))==b'\0','returning to Vol re-enables the Main knob')
check(struct.unpack('<2f',u.mem_read(P+0xbe0+0x400,8))!=(-96.,-12.),'returning to Vol restores the native fader bounds')
call(u,S['gate_mix_setup'],P,1);call(u,S['gate_mix_select'],P,6)

# Use a standalone numeric knob fixture to check native dB travel and Off formatting.
W=0x24060000;u.mem_write(W+0x32,b'\0\0');u.mem_write(W+0x3c8,b'\1');u.mem_write(W+0x3c9,b'\1')
for value,expected in [(-96000,b'OFF'),(-60000,b'-60.0'),(-12000,b'-12.0')]:
 call(u,0x8127b84,W,0x1ae,value&0xffffffff,1)
 check(cs(W+0xd1)==expected,'Gate native dB label '+str(value))
call(u,0x8127b84,W,0x1ae,(-60000)&0xffffffff,1)
messages=[];stub(u,0x813a414,lambda u:messages.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
for i in range(100):
 w32(u,0x3001eb3c,10000+i*1000);call(u,0x8127d50,W,1)
v=struct.unpack('<i',u.mem_read(W+0x98,4))[0]
check(-60000<v<=-58000 and any(struct.unpack_from('<I',m,12)[0]==0x1ae for m in messages),'native encoder emits a Gate edit with native fine dB travel over 100 encoder units')
# Full Track page: only the file-name lookup boundary is substituted.
T=0x24030000;INFO=0x24008010
call(u,0x81323b8,T);call(u,0x8132ed4,T)
stub(u,0x8121f4c,lambda u:u.reg_write(UC_ARM_REG_R0,0))
for quad in [0,1]:
 call(u,0x8120264,APP+0xe78,0x18c,1-quad)
 for ch in range(12):
  call(u,0x8120264,APP+0xc08+ch*0x30,0x1ae,(-70000+ch*1000)&0xffffffff)
  u.mem_write(INFO,struct.pack('<IH2x',0,ch));call(u,S['gate_track_bind'],T,INFO)
  w=T+0xde48
  check(struct.unpack('<H2xi',u.mem_read(w+0x94,8))==(0x1ae,-70000+ch*1000) and struct.unpack('<H',u.mem_read(w+0x32,2))[0]==ch,'Track Gate binds selected threshold and track '+str((quad,ch)))
 call(u,0x81327cc,T,1)
 fx1,fx2,out2,gate=[struct.unpack('<4i',u.mem_read(T+off+4,16)) for off in [0xcf08,0xd2d8,0xda78,0xde48]]
 check(fx1[0]==out2[0] and fx2[0]==gate[0] and fx1[1]==fx2[1] and out2[1]==gate[1] and gate[0]==out2[0]+48,'Track right group preserves FX1/FX2 and OUT2/GATE geometry '+str(quad))
 check(bytes(u.mem_read(w+0x30,1))==b'\0' and bytes(u.mem_read(w+0x2d4,1))==b'\0','Track Gate remains editable in both output modes '+str(quad))
 messages.clear()
 for i in range(100):w32(u,0x3001eb3c,1000000+i*1000);call(u,0x8127d50,w,1)
 check(any(struct.unpack_from('<H',m,8)[0]==11 and struct.unpack_from('<I',m,12)[0]==0x1ae for m in messages),'Track right-hand Gate encoder emits correct parameter/track '+str(quad))
# Native XML writer and reader round-trip twelve independent threshold values.
XML=0x24050000;VTY=0x24058000;VXML=0x24058100
w32(u,VTY,0x80dfe01);stub(u,0x80dfe00,lambda u:u.reg_write(UC_ARM_REG_R0,1))
w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
saved={};last=[''];current=[0]
stub(u,0x8107b16,lambda u:u.reg_write(UC_ARM_REG_R0,XML));stub(u,0x8106c92,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def key(u):last[0]=cs(u.reg_read(UC_ARM_REG_R1)).decode();u.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,key);stub(u,0x8106dc6,lambda u:saved.setdefault(current[0],{}).update({last[0]:u.reg_read(UC_ARM_REG_R1)}));stub(u,0x8106dba,lambda u:None)
for ch in range(12):
 current[0]=ch;SET=APP+0xc08+ch*0x30;value=-30000-ch*2000
 w32(u,SET,VTY);call(u,0x8120264,SET,0x1ae,value&0xffffffff);call(u,0x8120b84,XML,XML,SET)
 check(saved[ch].get('ngthreshold')==(value&0xffffffff),'native XML writer preserves threshold track'+str(ch+1))
 call(u,0x8120264,SET,0x1ae,(-96000)&0xffffffff)
stub(u,0x81074a6,lambda u:u.reg_write(UC_ARM_REG_R0,0));stub(u,0x8106ae8,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
def find(u):
 last[0]=cs(u.reg_read(UC_ARM_REG_R1)).decode();u.reg_write(UC_ARM_REG_R0,XML if last[0] in saved[current[0]] else 0)
stub(u,0x810747c,find)
def read(u):w32(u,u.reg_read(UC_ARM_REG_R1),saved[current[0]][last[0]]);u.reg_write(UC_ARM_REG_R0,0)
stub(u,0x8106d9e,read)
for ch in range(12):
 current[0]=ch;SET=APP+0xc08+ch*0x30;call(u,0x8120c3c,XML,SET)
 check(call(u,0x8120234,SET,0x1ae)==((-30000-ch*2000)&0xffffffff),'native XML reader restores threshold track'+str(ch+1))
 saved[ch].pop('ngthreshold');u.mem_write(TMP,b'\1');call(u,0x81202b8,SET,TMP);w32(u,SET,VTY);call(u,0x8120c3c,XML,SET)
 check(call(u,0x8120234,SET,0x1ae)==((-96000)&0xffffffff),'older project leaves Gate Off, track'+str(ch+1))
print(str(H.checks)+' v1.0.0-alpha.4 native gate UI/persistence checks passed; physical touch/rendering still requires hardware tests.')
