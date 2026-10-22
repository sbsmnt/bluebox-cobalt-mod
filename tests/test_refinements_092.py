"""v0.9.2 native cutoff registration/persistence and UI-time/encoder checks."""
from delay_fixture import *
import importlib.util,os
XS=0x38800200;GS=0x38800000;MODEL=0x300173d0;SET=MODEL+0xe48;S=M['symbols']['4'];P=0x24010000;W=P+0xc3b0;TICK=0x3001eb3c
u=machine();heap=[0xc0010000]
def alloc(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
a=0xc0010000+28*0x1ac
check(struct.unpack('<HB',u.mem_read(a,3))==(0x1ac,1) and struct.unpack('<ii',u.mem_read(a+16,8))==(500,16000),'Hi Cut registers a real integer Hz control, range500..16000')
check(bytes(u.mem_read(u32(u,a+24),32)).split(b'\0')[0]==b'tapehicut','Hi Cut has a distinct native project key')
u.mem_write(0x24008000,b'\4');call(u,0x81202b8,SET,0x24008000)
check(call(u,0x8120234,SET,0x1ac)==6000,'Hi Cut fresh/default value is6000Hz')
u.mem_write(P+0xc398,b'\1');u.mem_write(P+0xc3a4,struct.pack('<H',20));stub(u,0x812abe0,lambda u:None);stub(u,0x812afd8,lambda u:None);stub(u,0x813a414,lambda u:None)
slot=0x24008010;u.mem_write(slot,struct.pack('<H',20))
def bind(pid,value,w=W):
 u.mem_write(w,bytes(0x3d0));u.mem_write(w+0x32,struct.pack('<H',20));u.mem_write(w+0x3c9,b'\1')
 call(u,0x8127b84,w,pid,value,1);call(u,S['dly_populate'],P,slot)
def text(w=W):return bytes(u.mem_read(w+0xd1,32)).split(b'\0')[0].decode()
for mode in [5,0,2,3]:
 call(u,0x8120264,SET,0x1a0,mode)
 for value,expected in [(0,'5.3ms'),(1,'6ms'),(10,'60ms'),(333,'1998ms'),(400,'2400ms'),(1000,'6000ms')]:
  bind(0x33,value);call(u,0x81278b0,W,1);label=text();w32(u,TICK,100000)
  call(u,S['dly_turn'],W,0)
  check(text()==expected,'free time correctly converts native value to ms '+str((mode,value)))
  w32(u,TICK,102999);call(u,S['dly_tick'],P,0x24060040)
  check(text()==expected,'free-time label holds through2999ms '+str((mode,value)))
  w32(u,TICK,103000);call(u,S['dly_tick'],P,0x24060040)
  check(text()==label,'free-time label restores at3000ms '+str((mode,value)))
# Reverse remains literal milliseconds; Granular Size keeps its existing ms.
for mode,pid,value,expected in [(4,0x1aa,123,'123ms'),(1,0x1a2,80,'80ms'),(2,0x1ac,16000,'16000Hz')]:
 call(u,0x8120264,SET,0x1a0,mode);bind(pid,value);call(u,0x81278b0,W,1);label=text();w32(u,TICK,200000);call(u,S['dly_turn'],W,0)
 check(text()==expected,'ms/Hz display preserves actual units '+str(pid))
 w32(u,TICK,203000);call(u,S['dly_tick'],P,0x24060040)
 check(text()==label,'ms/Hz display restores its original label '+str(pid))
# Native free time parameter must stay unchanged, and map to the advertised time.
for value,frames in [(0,256),(1,288),(400,115200),(1000,288000)]:
 q,_,_=dsetup();dblock(q,ZERO,evs=INITIAL+[(0x33,value)])
 check(u32(q,DOBJ+0x24)==frames,'native sample target agrees with displayed ms '+str(value))
# Parameter events arrive in the audio path, clamp and update real state without
# allocating, while surrounding modes ignore the added state for their output.
q,_,_=dsetup();dblock(q,ZERO,evs=INITIAL+[(0x1a0,2),(0x1ac,1200)])
check(u32(q,XS+416)==1200,'actual queued cutoff event reaches M7 Tape state')
for value,expected in [(-1,500),(99999,16000)]:
 dblock(q,ZERO,evs=[(0x1ac,value)])
 check(u32(q,XS+416)==expected,'malformed cutoff events clamp safely '+str(value))
# Verify effect has a meaningful HF change in real native feedback processing.
a,_,_=dsetup();b,_,_=dsetup();ea=eb=0.
for n in range(650):
 ev=INITIAL+[(0x1a0,2),(0x1a7,700),(0x39,400)] if n==0 else []
 oa,_=dblock(a,signal(.4,n,6500),evs=ev+[(0x1ac,500)] if n==0 else ev)
 ob,_=dblock(b,signal(.4,n,6500),evs=ev+[(0x1ac,16000)] if n==0 else ev)
 if n==0:steady(a);steady(b)
 if n>200:ea+=sum(v*v for v in struct.unpack('<64f',oa));eb+=sum(v*v for v in struct.unpack('<64f',ob))
check(eb>0 and ea<eb*.12,'actual native Tape low Hi Cut materially attenuates high-frequency repeats')
# Quiet drive previously regenerated native tails; compare that exact case.
oldroot=(ROOT/'tests'/'references'/'v0.9.5')
spec=importlib.util.spec_from_file_location('previous',oldroot/'patch_bluebox.py');previous=importlib.util.module_from_spec(spec);spec.loader.exec_module(previous)
a,_,_=dsetup(previous.transform(stock));b,_,_=dsetup();early=[0.,0.];late=[0.,0.]
for n in range(1800):
 ev=INITIAL+[(0x1a0,2),(0x1a7,1000),(0x39,650)] if n==0 else []
 inp=[.00001]*32 if n<120 else ZERO
 for j,q in enumerate([a,b]):
  out,_=dblock(q,inp,evs=ev)
  if n==0:steady(q)
  if 120<n<500:early[j]+=sum(v*v for v in struct.unpack('<64f',out))
  if n>1400:late[j]+=sum(v*v for v in struct.unpack('<64f',out))
check(early[1]>0 and late[1]<early[1]*.001 and late[0]<early[0]*.001,'actual native Tape tail decays in0.9.2/1.0.0-alpha.4 at unchanged65percent feedback')
# Cutoff encoder has practical travel for its15500Hz range.
call(u,0x8120264,SET,0x1a0,2);bind(0x1ac,6000);w32(u,TICK,250000)
call(u,S['dly_turn'],W,40)
# exp.8: logarithmic travel, one 0.1% log step (40 units) from 6000 Hz.
check(6016<=u32(u,W+0x98)<=6030,'Hi Cut encoder takes one logarithmic step per 40 units (exp.8)')
# Density acceleration is bounded even if the native accelerator reports max6.
call(u,0x8120264,SET,0x1a0,1);bind(0x1a3,1)
stub(u,0x813b804,lambda u:u.reg_write(UC_ARM_REG_R0,6));u.ctl_flush_tb()
for i in range(127):w32(u,TICK,300000+i);call(u,S['dly_turn'],W,1)
check(u32(u,W+0x98)==1,'Density max native acceleration cannot cross before128 one-unit moves')
w32(u,TICK,300128);call(u,S['dly_turn'],W,1)
check(u32(u,W+0x98)==2,'Density max acceleration is capped at2 across256-unit steps')
print(str(H.checks)+' v0.9.2 native refinement checks passed; listening, UI feel and hardware headroom remain device tests.')
