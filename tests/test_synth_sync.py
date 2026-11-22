"""LFO tempo source and all divisions compared with the original delay math."""
from firmware_harness import *
import firmware_harness as H
S=M['symbols']['synth7'];SRC=0xc0100000;SIDE=SRC+0x5a8;ENV=0xc0400000
NODE=0x24040000;VT=NODE+0x100;TMP=0x24008000
# The compact layouts keep the phase at this ABI offset in S4/S5. Audio host
# tests independently check period and range without relying on this offset.
PHASE=SIDE+0x344
NATIVE_TICKS=[60,120,240,320,360,480,640,720,960,1280,1920,3840]
q=machine(double=True)
for bpm in (60,120,180):
 for choice,ticks in enumerate(NATIVE_TICKS,1):
  u=machine();call(u,S['synth_source_construct'],SRC)
  w32(u,SRC+0x18,0);u.mem_write(SRC+0x1e,struct.pack('<H',0));w32(u,SRC+8,NODE)
  w32(u,NODE,VT);w32(u,VT+12,NEXT_FN|1);stub(u,NEXT_FN,lambda q:q.reg_write(UC_ARM_REG_R0,7))
  w32(u,CTX,ENV);u.mem_write(ENV+0x18,struct.pack('<f',bpm))
  u.mem_write(CTX+0x150e4,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,1,1))
  ev=[(0x1bf,1),(0x1c8,1),(0x1d2,choice)]
  for i,(pid,value) in enumerate(ev):u.mem_write(CTX+4+i*24,struct.pack('<B7xIHHiI',0x39,0,pid,0,value,0))
  w32(u,CTX+4+0x600,len(ev))
  for block in range(100):
   call(u,S['synth_source_process'],SRC,CTX);w32(u,CTX+4+0x600,0)
  actual=f32(u,PHASE);period=48000.*60./bpm*ticks/960.
  expected=(3200./period)%1.;distance=abs(actual-expected)
  check(min(distance,1.-distance)<.0001,'actual source follows the delay tempo source/division '+str((bpm,choice)))
  q.mem_write(TMP,struct.pack('<q',ticks));q.reg_write(UC_ARM_REG_S0,struct.unpack('<I',struct.pack('<f',bpm))[0]);q.reg_write(UC_ARM_REG_S1,struct.unpack('<I',struct.pack('<f',48000))[0])
  call(q,0x805a260,TMP+0x20,TMP)
  native=struct.unpack('<q',q.mem_read(TMP+0x20,8))[0]
  check(abs(native-period)<=.51,'LFO period agrees with original native delay conversion '+str((bpm,choice)))
print(H.checks,'native beat-sync checks passed; external clock/transport timing remains a device test.')
