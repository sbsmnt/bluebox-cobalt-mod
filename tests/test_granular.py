"""Execute compiled ARM engine, real delay constructor/body, bus and event queues.
Allocator boundary is stubbed in dsetup but its real arena counters are modeled.
"""
from delay_fixture import *
import importlib.util,os
GS=0x38800000;GMODE=0x1a0;GPOS=0x1a1;GSIZE=0x1a2;GDENS=0x1a3;GJIT=0x1a4;GFREEZE=0x1a5;GREV=0x1a6
# Correct layout of the production M7 struct, independently asserted in C.
GWR=48;GVALID=52;GPEAK=64;GSPAWN=68;GFADE=72
# Constructor preflight executes the REAL bump allocator (no allocator stub).
for remaining,expected in [(768000,True),(767999,False),(0,False)]:
 u=machine();stub(u,0x8052478,lambda u:u.reg_write(UC_ARM_REG_R0,42))
 w32(u,0x24000004,0xc0010000);w32(u,0x24000000,0xc0010000+remaining)
 # Corrupt retained state must never be treated as a valid old heap pointer.
 u.mem_write(GS,b'\xff'*168)
 ret=call(u,SYMS['dly_construct'],DOBJ)
 check(ret==42 and u32(u,GS)==0x47524e36 and u32(u,GS+8)==(0xc0010000 if expected else 0),'real arena preflight allocation with remaining bytes '+str(remaining))
 used=768000 if expected else 0
 sg_expected=remaining-used>=205056
 check(u32(u,0x24000004)==0xc0010000+used+(205056 if sg_expected else 0),'arena cursor accounts only for preflighted granular and Shoegaze buffers '+str(remaining))
 check(bool(u32(u,0x38800104))==sg_expected,'independent Shoegaze allocation follows its own preflight '+str(remaining))
 check(u32(u,GS+GVALID)==0 and u32(u,GS+12)==0,'constructor resets grain history and starts in Normal '+str(remaining))
# Invalid or exhausted arena values cannot reach the assert-on-failure allocator.
for cursor,limit in [(0,0xc2000000),(0x24001000,0xc2000000),(0xc0100000,0xc00fffff),(0xc33fff00,0xc4000000)]:
 u=machine();stub(u,0x8052478,lambda u:u.reg_write(UC_ARM_REG_R0,42))
 w32(u,0x24000004,cursor);w32(u,0x24000000,limit)
 stub(u,0x80412a4,lambda u:(_ for _ in ()).throw(AssertionError('unsafe arena allocation')))
 call(u,SYMS['dly_construct'],DOBJ)
 check(u32(u,GS+8)==0 and u32(u,0x24000004)==cursor,'unsafe arena range safely refused '+hex(cursor)+'/'+hex(limit))
# Read/write instrumentation checks the actual ARM engine at a cold start.
u,_,_=dsetup();ptr=u32(u,GS+8);u.mem_write(ptr,struct.pack('<f',float('nan'))*192000)
written=set()
def memory(u,access,address,size,value,_):
 if access==UC_MEM_WRITE:
  for a in range(address,address+size,4):written.add(a)
 else:
  assert all(a in written for a in range(address,address+size,4)),'unwritten history read '+hex(address)
h=u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory,begin=ptr,end=ptr+768000-1)
for n in range(300):
 out,_=dblock(u,signal(.3,n),evs=INITIAL+[(GMODE,1),(GSIZE,250),(GDENS,4),(GJIT,1000),(GREV,1),(GPOS,1000),(0x4a,12)] if n==0 else [])
 assert all(math.isfinite(v) for v in struct.unpack('<64f',out))
u.hook_del(h)
check(len(written)==300*64,'cold ARM grains only read history previously written, including reverse/pitch/jitter extremes')
# Missing memory: normal rendering and native UI acknowledgment of Mode=0.
u,_,_=dsetup();w32(u,GS+8,0)
out,ret=dblock(u,ZERO,evs=INITIAL+[(GMODE,1)])
q=CTX+0x1529c
check(ret==7 and u32(u,GS+12)==0 and u32(u,q+0x600)==1 and bytes(u.mem_read(q,24))==struct.pack('<6I',0x39,0,20,GMODE,0,0),'allocation failure returns Mode to Normal through actual M7-to-M4 queue')
# Compare the complete hardware-tested granular firmware with Normal-mode v0.6,
# including nonzero pitch/drift/send, transport and parameter edits.
s=importlib.util.spec_from_file_location('previous',(ROOT/'tests'/'references'/'v0.9.5')/'patch_bluebox.py');v5=importlib.util.module_from_spec(s);s.loader.exec_module(v5)
a,_,_=dsetup(v5.transform(stock));b,_,_=dsetup()
check(u32(b,GS+8)>=0xc0010000,'new constructor allocated a separate 768000-byte capture buffer')
same=True
for n in range(1200):
 ev=INITIAL+[(0x4a,-7),(0x4b,1),(0x3a,800),(0x3d,600)] if n==0 else [(0x37,1)] if n==400 else [(0x36,1)] if n==600 else [(GSIZE,250),(GDENS,1),(GFREEZE,1),(GREV,1),(GJIT,1000),(GMODE,0),(GPOS,700)] if n==800 else []
 x=signal(.3,n);y=signal(.2,n,700)
 oa,ra=dblock(a,x,y,ev);ob,rb=dblock(b,x,y,ev)
 same=same and oa==ob and ra==rb and a.mem_read(DOBJ,0x278)==b.mem_read(DOBJ,0x278) and a.mem_read(DS,348)==b.mem_read(DS,348)
check(same,'Normal mode exactly matches v0.6 PCM/object/send state for 1200 blocks, including hidden granular edits')
# New parameter ids must be no-ops in the actual native body.
check(same,'stock delay ignores all seven newly registered parameter ids')
# Granular source and output are stereo; both send channels capture the new wet.
u,nodes,_=dsetup();dblock(u,ZERO,evs=INITIAL+[(0x39,0),(0x3d,500)])
for n in range(3200):dblock(u,signal(.3,n,440),signal(.15,n,700))
check(u32(u,GS+GVALID)==96000,'Normal captures exactly two seconds and wraps without borrowing native rings')
outputs=[]
for n in range(750):
 out,ret=dblock(u,signal(.3,n,440),signal(.15,n,700),[(GMODE,1),(GFREEZE,1),(GSIZE,80),(GDENS,4)] if n==0 else [])
 outputs.append(struct.unpack('<64f',out))
check(ret==7 and len(nodes)==3951,'granular branch invokes the downstream node exactly once per block')
check(u32(u,GS+GPEAK)==4 and u32(u,GS+GSPAWN)>8,'compiled ARM engine uses at most four slots and reaches four overlapping grains')
check(all(math.isfinite(v) and abs(v)<.31*G_MAKEUP for frame in outputs for v in frame),'stereo granular output remains finite and within the source peak (plus the exp.6 +3 dB wet makeup) at zero feedback')
print('INFO granular stereo peak amplitudes',max(abs(v) for frame in outputs[100:] for v in frame[:32]),max(abs(v) for frame in outputs[100:] for v in frame[32:]));check(max(abs(v) for frame in outputs[100:] for v in frame[:32])>.01 and max(abs(v) for frame in outputs[100:] for v in frame[32:])>.01,'both stereo channels remain audible')
check(any(abs(frame[i]-frame[32+i])>.01 for frame in outputs[100:] for i in range(32)),'stereo channels remain distinct')
check(f32(u,GS+GFADE)==1.,'mode crossfade reaches exactly Granular')
stash=struct.unpack('<64f',u.mem_read(DS+60,256))
check(max(abs(x-y*.5) for x,y in zip(stash,outputs[-1]))<1e-7,'To Rev captures granular output, not the parallel Normal output')
# Freeze holds writer and its full buffer across arbitrary new live input.
wr=u32(u,GS+GWR);ptr=u32(u,GS+8);before=bytes(u.mem_read(ptr,768000))
for n in range(200):dblock(u,signal(.9,n,123),signal(.8,n,321))
check(wr==u32(u,GS+GWR) and before==bytes(u.mem_read(ptr,768000)),'Freeze stops all capture and feedback writes')
# Reverse and octave pitch: inspect actual living grain speeds after old grains end.
for pitch,reverse in [(12,0),(-12,1),(0,1)]:
 for n in range(450):
  dblock(u,ZERO,evs=[(0x4a,pitch),(GREV,reverse)] if n==0 else [])
 steps=[f32(u,GS+88+i*20+4) for i in range(4) if u32(u,GS+88+i*20+16)]
 expected=2**(pitch/12)*(-1 if reverse else 1)
 check(steps and all(abs(s-expected)<1e-6 for s in steps),'new grains use expected signed resampling ratio '+str((pitch,reverse)))
# Resuming capture and very high feedback must stay bounded and can change size/density.
for n in range(1000):
 out,_=dblock(u,signal(.8,n),evs=[(GFREEZE,0),(0x39,1000),(GJIT,1000),(GSIZE,250),(GDENS,1)] if n==0 else [])
 assert all(math.isfinite(v) and abs(v)<4.01 for v in struct.unpack('<64f',out))
check(u32(u,GS+GWR)!=wr and u32(u,GS+44)==950,'unfreezing resumes capture and granular feedback clamps at 95 percent')
# Count actual ARM instructions: an estimate only, not cycles or hardware load.
for density in [1,4]:
 for n in range(1600):dblock(u,signal(.3,n),evs=[(GDENS,density),(GSIZE,20),(0x4a,12)] if n==0 else [])
 steps=[0]
 def tick(u,a,n,_):steps[0]+=1
 h=u.hook_add(UC_HOOK_CODE,tick);dblock(u,signal(.3,1601));u.hook_del(h)
 print('INFO total native delay + granular callback instruction count, density',density,':',steps[0])
 check(steps[0]<55000,'density '+str(density)+' completes within bounded emulator instruction budget')
# Worst combined inherited Normal settings still execute in parallel; include
# these when budgeting the maximum four-grain path, rather than an empty body.
for n in range(1700):dblock(u,signal(.3,n),evs=[(0x37,1),(0x36,1),(0x4b,1),(0x4a,12),(0x3a,1000),(0x3d,1000),(0xca,1),(GSIZE,250),(GDENS,4)] if n==0 else [])
steps=[0];h=u.hook_add(UC_HOOK_CODE,tick);out,_=dblock(u,signal(.3,1701));u.hook_del(h)
print('INFO complete worst combined native + four-grain callback instructions:',steps[0])
check(steps[0]<55000 and all(math.isfinite(v) and abs(v)<4.01 for v in struct.unpack('<64f',out)),'inherited Quad/Ping/filter/pitch/drift/send with four grains completes within instruction budget')
# Normal fade completion clears grains, does not leak stale feedback into capture.
for n in range(20):dblock(u,ZERO,evs=[(GMODE,0)] if n==0 else [])
check(f32(u,GS+GFADE)==0 and all(u32(u,GS+88+i*20+16)==0 for i in range(4)),'return to Normal reaches exact zero blend and deactivates every grain')
check(u32(u,GS+GPEAK)<=4,'grain cap survives all native mode/parameter transitions')
# No audio-time allocator, even when rapidly selecting modes/size/freeze.
def bad_alloc(u):raise AssertionError('allocation in audio callback')
stub(u,0x80412a4,bad_alloc)
for n in range(100):dblock(u,ZERO,evs=[(GMODE,n%2),(GFREEZE,n%3),(GSIZE,20+n)])
check(True,'rapid changes never allocate in the audio callback')
# Real bus helpers: mono capture, silent frozen tails, short and empty blocks.
u,_,_=dsetup()
def custom_bus(frames=32,silent=0,stereo=1,ev=(),cap=32):
 devents(u,ev)
 u.mem_write(DBUS,struct.pack('<IIIIBBxx',cap,frames,BUF,BUF+0x100,silent,stereo))
 u.mem_write(BUF,struct.pack('<32f',*signal(.3,400,440)))
 u.mem_write(BUF+0x100,struct.pack('<32f',*signal(.9,400,900)))
 before=bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
 call(u,u32(u,u32(u,DOBJ)+12),DOBJ,CTX)
 return before,bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128))
for n in range(400):custom_bus(stereo=0,ev=INITIAL+[(0x39,0)] if n==0 else [])
for n in range(300):before,out=custom_bus(stereo=0,ev=[(GMODE,1),(GSIZE,20),(GFREEZE,1)] if n==0 else [])
v=struct.unpack('<64f',out)
check(v[:32]==v[32:] and max(abs(x) for x in v)>.001,'mono input is captured into identical stereo granular channels')
wr=u32(u,GS+GWR);before,out=custom_bus(silent=1)
check(max(abs(x) for x in struct.unpack('<64f',out))>.001 and u32(u,GS+GWR)==wr and bytes(u.mem_read(DBUS+16,1))==b'\0','silent input still plays a frozen tail and marks the wet bus audible')
before,out=custom_bus(frames=4)
check(out[16:128]==before[16:128] and out[144:]==before[144:],'four-frame granular callback leaves both output tails untouched')
before,out=custom_bus(frames=0,cap=0)
check(out==before,'empty granular callback leaves output unchanged')
print(str(H.checks)+' native granular integration checks passed; CPU cycles and complete device routing remain hardware tests.')
