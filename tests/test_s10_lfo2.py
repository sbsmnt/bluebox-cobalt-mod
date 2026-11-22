"""S10: the second LFO on the M7. Real synth source construct/process with
engine events on the track's own queue, as the M4 setter delivers them.
Only the downstream native node is substituted (it collects the PCM)."""
from firmware_harness import *
import firmware_harness as H
SY=M['symbols']['synth7'];SRC=0xc0100000;NODE=0x24040000;VT=NODE+0x100;VOICE=SRC+0x5a8
LFO2=VOICE+0x358 # offsetof(synth_voice,lfo2) on the M7 (856): int16 rate, dest, amount, shape, sync; float phase at +0xc; u32 random at +0x10
def run(events,blocks=200):
 u=machine();call(u,SY['synth_source_construct'],SRC)
 w32(u,SRC+0x18,0);u.mem_write(SRC+0x1e,struct.pack('<H',0));w32(u,SRC+8,NODE)
 w32(u,NODE,VT);w32(u,VT+12,NEXT_FN|1);pcm=[]
 def sink(q):
  bus=CTX+0x150e4;pcm.append(bytes(q.mem_read(u32(q,bus+8),128)));q.reg_write(UC_ARM_REG_R0,7)
 stub(u,NEXT_FN,sink)
 u.mem_write(CTX+0x150e4,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,1,1))
 base=[(0x1bf,1),(0x1c8,1),(0x1c0,0),(0x1cb,2),(0x1cc,600),(0x1ca,2000)]
 evs=base+events
 for i,(pid,value) in enumerate(evs):u.mem_write(CTX+4+i*24,struct.pack('<B7xIHHiI',0x39,0,pid,0,value,0))
 w32(u,CTX+4+0x600,len(evs));call(u,SY['synth_source_process'],SRC,CTX);w32(u,CTX+4+0x600,0)
 for i in range(blocks):call(u,SY['synth_source_process'],SRC,CTX)
 return u,b''.join(pcm)
u,lfo1=run([])
check(struct.unpack('<5h',u.mem_read(LFO2,10))==(1000,0,0,0,0) and u32(u,VOICE)==0x53594e3a,'a fresh voice: LFO2 at 1 Hz, Dest Off, Amount 0 (S15 voice magic)')
u2,same=run([(0x1d7,7000),(0x1d9,900),(0x1da,1)])
check(same==lfo1 and len(lfo1)==201*128,'LFO2 with Dest Off leaves the sound exactly as LFO1 alone')
u3,both=run([(0x1d7,3000),(0x1d8,1),(0x1d9,800),(0x1da,1),(0x1db,0)])
check(struct.unpack('<5h',u3.mem_read(LFO2,10))==(3000,1,800,1,0),'LFO2 Rate, Dest, Amount, Shape and Div arrive through the track queue')
check(both!=lfo1,'LFO2 on Timbre changes the sound')
ph=struct.unpack('<f',u3.mem_read(LFO2+0xc,4))[0]
check(abs(ph-((201*32*3.0/48000)%1.0))<1e-3,'LFO2 phase advances at its own 3 Hz (%.4f)'%ph)
u4,clamped=run([(0x1d7,99999),(0x1d8,9),(0x1d9,-4),(0x1da,9),(0x1db,99)],blocks=1)
check(struct.unpack('<5h',u4.mem_read(LFO2,10))==(20000,4,0,3,24),'out-of-range LFO2 values clamp as LFO1 does')
u5,synced=run([(0x1d8,1),(0x1d9,800),(0x1db,9)],blocks=10)
print(H.checks,'S10 LFO2 audio-core checks passed; sound and CPU need hardware.')
