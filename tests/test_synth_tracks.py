"""Independent native source objects, per-track MIDI and shared-input isolation."""
from firmware_harness import *
import firmware_harness as H
S=M['symbols']['synth7'];u=machine(double=True)
SRC=0xc0100000;MSG=0x24060000;MAP=0xc0200000;captured=[]
NODE=0x24040000;V=NODE+0x100
w32(u,NODE,V);w32(u,V+12,NEXT_FN|1)
active=[0]
def sink(q):
 bus=CTX+0x150e4+active[0]*20
 captured.append(bytes(q.mem_read(u32(q,bus+8),128))+bytes(q.mem_read(u32(q,bus+12),128)))
 q.reg_write(UC_ARM_REG_R0,7)
stub(u,NEXT_FN,sink)
for ch in range(12):
 obj=SRC+ch*0x1000;call(u,S['synth_source_construct'],obj)
 w32(u,obj+0x18,ch);u.mem_write(obj+0x1e,struct.pack('<H',ch));w32(u,obj+8,NODE)
def block(ch,events=()):
 active[0]=ch;q=CTX+ch*0x604+4
 for i,(pid,value) in enumerate(events):u.mem_write(q+i*24,struct.pack('<B7xIHHiI',0x39,ch,pid,0,value,0))
 w32(u,q+0x600,len(events))
 bus=CTX+0x150e4+ch*20
 u.mem_write(bus,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,0,1))
 raw=struct.pack('<64f',*([.25]*32+[-.125]*32))
 u.mem_write(BUF,raw[:128]);u.mem_write(BUF+0x100,raw[128:])
 call(u,u32(u,u32(u,SRC+ch*0x1000)+12),SRC+ch*0x1000,CTX)
 check(bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))==raw,'track source preserves physical/shared samples '+str(ch)) if events else None
 return captured[-1]
def midi(status,note,velocity):
 u.mem_write(MSG,bytes([status,note,velocity,0,3]));call(u,S['synth_midi_dispatch'],MAP,MSG,CTX)
for ch in range(12):
 out=block(ch,[(0x1bf,int(ch<2)),(0x1c0,3+ch%2),(0x1c1,ch+1)])
 check(u32(u,0x38800184+ch*4)==SRC+ch*0x1000+0x5a8,'registry points to matching owned track voice '+str(ch))
midi(0x90,48,127)
for i in range(10):a=block(0);b=block(1)
check(any(a) and b==bytes(256),'channel 1 plays only track 1 while track 2 stays silent')
midi(0x91,72,90)
for i in range(10):a=block(0);b=block(1)
check(any(a) and any(b) and a!=b,'two tracks run different engines/notes independently')
midi(0x80,48,0)
for i in range(240):a=block(0);b=block(1)
check(a==bytes(256) and any(b),'releasing one track leaves the other playing')
raw=struct.pack('<64f',*([.25]*32+[-.125]*32))
check(block(2)==raw,'third audio track retains its physical input while two synth tracks run')
print(H.checks,'native multitrack source-isolation checks passed; no safe device voice count is claimed.')
