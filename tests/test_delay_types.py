"""Native firmware integration for all five types and new persisted controls."""
from delay_fixture import *
import os,importlib.util
XS=0x38800200;GS=0x38800000;MODE=0x1a0
previous_path=(ROOT/'tests'/'references'/'v0.9.5')/'patch_bluebox.py'
s=importlib.util.spec_from_file_location('prev',previous_path);prev=importlib.util.module_from_spec(s);s.loader.exec_module(prev)
before=prev.transform(stock)
# Retain PCM/object/DS/GS equivalence through original modes and edits.
a,_,_=dsetup(before);b,_,_=dsetup();same=True;worst=0.
for n in range(2100):
 # Stays in Granular: exp.5 clears the skipped native rings, so Delay after
 # Granular deliberately differs (see test_exp5_skip.py for that path).
 ev=INITIAL+[(MODE,1),(0x1a2,40),(0x1a3,4),(0x3d,400)] if n==0 else [(MODE,1),(0x1a5,(n//300)%2),(0x1a6,1),(0x4a,-7),(0x3a,400)] if n%300==0 else []
 oa,ra=dblock(a,signal(.3,n),signal(.17,n,733),ev);ob,rb=dblock(b,signal(.3,n),signal(.17,n,733),ev)
 ints,diff=granular_close(a,b,oa,ob);worst=max(worst,diff)
 same=same and ra==rb and ints
print('INFO largest Delay/Granular difference from v0.9.1:',worst)
check(same and worst<2e-5,'original Granular matches v0.9.1 send/grain integer state exactly and PCM/float state within float rounding through freeze/reverse/pitch/drift edits')

def native_ring_snapshot(u):
 return [bytes(u.mem_read(u32(u,DOBJ+0x30+i*28),4*u32(u,DOBJ+0x30+i*28+20))) for i in range(6)]
normal,_,_=dsetup();duck,nodes,_=dsetup();en=ed=0.
for n in range(700):
 ev=INITIAL+[(0x39,600)] if n==0 else []
 an,_=dblock(normal,signal(.4,n),evs=ev)
 ad,ret=dblock(duck,signal(.4,n),evs=ev+[(MODE,3)] if n==0 else ev)
 if n==0:steady(normal);steady(duck)
 if n>200:en+=sum(v*v for v in struct.unpack('<64f',an));ed+=sum(v*v for v in struct.unpack('<64f',ad))
check(en>.01 and ed<en*.25,'native Ducked mode substantially attenuates repeats under active FX1 input')
check(normal.mem_read(DOBJ,0x278)==duck.mem_read(DOBJ,0x278) and native_ring_snapshot(normal)==native_ring_snapshot(duck),'ducking leaves the actual native feedback loop unchanged')
check(ret==7 and len(nodes)==700,'Ducked invokes downstream exactly once per block')
check(u32(duck,XS+8)==3,'Ducked selection is distinct from granular boolean mode')
stash=struct.unpack('<64f',duck.mem_read(DS+60,256))
dblock(duck,signal(.4,700),evs=[(0x3d,1000)])
out,_=dblock(duck,signal(.4,701));stash=struct.unpack('<64f',duck.mem_read(DS+60,256))
check(all(abs(x-y)<1e-6 for x,y in zip(stash,struct.unpack('<64f',out))),'To Rev captures the ducked return after gain reduction')

normal,_,_=dsetup();tape,nodes,_=dsetup();different=False
for n in range(700):
 ev=INITIAL+[(0x39,700)] if n==0 else []
 an,_=dblock(normal,signal(.6,n,2300),evs=ev)
 at,ret=dblock(tape,signal(.6,n,2300),evs=ev+[(MODE,2),(0x1a7,900)] if n==0 else ev)
 if n==0:steady(normal);steady(tape)
 if n>200:different|=an!=at
 assert all(math.isfinite(v) and abs(v)<4.01 for v in struct.unpack('<64f',at))
check(different and native_ring_snapshot(normal)!=native_ring_snapshot(tape),'Tape damping/saturation change both wet output and native feedback history')
check(ret==7 and len(nodes)==700 and u32(tape,XS+8)==2,'Tape preserves the native graph and distinct selection')

# Exercise all native line-reader arrangements with each new mode.
for selected in [2,3,4]:
 q,_,_=dsetup()
 for n in range(220):
  ev=INITIAL+[(MODE,selected),(0x37,1),(0x36,1),(0xca,1),(0x4b,1),(0x4a,12),(0x3a,1000),(0x3d,1000),(0x39,900),(0x1a7,1000)] if n==0 else []
  out,_=dblock(q,signal(.3,n),evs=ev)
  assert all(math.isfinite(v) for v in struct.unpack('<64f',out))
 check(True,'new type '+str(selected)+' remains finite with native Quad/Ping/filter/pitch/drift/send combined')

# Cold-start Reverse reads only samples actually captured, including maximum
# segment, upward octave, two playback heads and a full capture-ring wrap.
u,nodes,_=dsetup();ptr=u32(u,GS+8);u.mem_write(ptr,struct.pack('<f',float('nan'))*192000);written=set()
def memory(u,access,address,size,value,_):
 if access==UC_MEM_WRITE:
  written.update(range(address,address+size,4))
 else:assert all(a in written for a in range(address,address+size,4)),'unwritten reverse history read '+hex(address)
h=u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory,begin=ptr,end=ptr+768000-1)
energy=0.;distinct=False;reverse_audio=[]
for n in range(4000):
 ev=INITIAL+[(MODE,4),(0x1aa,600),(0x4a,12),(0x39,0),(0x3d,500)] if n==0 else []
 out,ret=dblock(u,signal(.3,n,421),signal(.17,n,733),ev)
 v=struct.unpack('<64f',out);assert all(math.isfinite(a) and abs(a)<.31 for a in v)
 if n>2000:energy+=sum(a*a for a in v);distinct|=v[:32]!=v[32:];reverse_audio.extend(v[:32])
u.hook_del(h)
check(energy>1. and distinct and len(written)==192000,'native Reverse is audible/stereo and never reads unwritten history across a full wrap')
check(abs(freq(reverse_audio[-24000:])-842)<20,'Reverse Pitch +12 shifts a known 421 Hz source approximately one octave')
check(u32(u,XS+8)==4 and ret==7 and len(nodes)==4000,'Reverse preserves distinct mode and single downstream delivery')
stash=struct.unpack('<64f',u.mem_read(DS+60,256))
check(max(abs(x-.5*y) for x,y in zip(stash,v))<1e-6,'Reverse-to-reverb send contains the actual reversed return')
def fail_alloc(u):raise AssertionError('callback allocation')
stub(u,0x80412a4,fail_alloc)
for n in range(120):dblock(u,signal(.3,n),evs=[(MODE,n%5),(0x1a5,n%2),(0x1aa,40+n),(0x1a7,1000),(0x1a8,1000)])
check(True,'all five modes switch without allocating in the audio callback')
for mode in [2,3,4]:
 q,_,_=dsetup();w32(q,GS+8,0);dblock(q,ZERO,evs=INITIAL+[(MODE,mode)])
 check(u32(q,XS+8)==(0 if mode==4 else mode),'allocation failure only disables buffer-dependent Reverse; mode '+str(mode))
 if mode==4:check(u32(q,CTX+0x1529c+0x600)==1 and u32(q,CTX+0x1529c+16)==0,'Reverse buffer failure acknowledges Delay through the actual UI queue')

# Native transport/input gating must prevent fresh input from entering Reverse
# capture, while existing reverse feedback/tails retain their normal decay.
q,_,_=dsetup()
for j in range(400):dblock(q,signal(.4,j),evs=INITIAL+[(MODE,4),(0x1aa,40),(0x39,0)] if j==0 else [])
for j in range(200):off,_=dblock(q,signal(.8,j),evs=[('off',0)] if j==0 else [])
wr=u32(q,GS+48);ptr=u32(q,GS+8)
recent=[struct.unpack('<f',q.mem_read(ptr+8*((wr+96000-1-j)%96000),4))[0] for j in range(32)]
check(all(v==0.0 for v in recent) and max(abs(v) for v in struct.unpack('<64f',off))<1e-6,'native FX1 off gates fresh Reverse capture and permits zero-feedback tails to finish')
for j in range(160):on,_=dblock(q,signal(.4,j),evs=[('on',0)] if j==0 else [])
check(max(abs(v) for v in struct.unpack('<64f',on))>.001,'native FX1 on resumes captured reverse audio')

# Representative complete-callback instruction counts, with native options
# combined and Reverse already primed. These are NOT hardware cycle counts.
for selected,bench in [(2,tape),(3,duck),(4,u)]:
 for j in range(90):
  ev=[(MODE,selected),(0x37,1),(0x36,1),(0xca,1),(0x4b,1),(0x4a,12),(0x3a,1000),(0x3d,1000),(0x39,900),(0x1aa,40)] if j==0 else []
  dblock(bench,signal(.3,j),evs=ev)
 instructions=[0]
 def count_instruction(q,a,n,data):instructions[0]+=1
 hook=bench.hook_add(UC_HOOK_CODE,count_instruction);out,_=dblock(bench,signal(.3,91));bench.hook_del(hook)
 print('INFO complete native type',selected,'callback instructions:',instructions[0])
 check(instructions[0]<100000 and all(math.isfinite(v) for v in struct.unpack('<64f',out)),'type '+str(selected)+' completes its bounded native callback with combined options')

# Leaving frozen Granular may restart capture within a block. Verify Reverse
# uses only the actual trailing writes, never an assumed full-block advance.
q,_,_=dsetup();ptr=u32(q,GS+8);q.mem_write(ptr,struct.pack('<f',float('nan'))*192000);written=set()
h=q.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory,begin=ptr,end=ptr+768000-1)
for n in range(900):
 ev=INITIAL+[(MODE,0),(0x1aa,40),(0x4a,-12)] if n==0 else [(MODE,1 if (n//2)%2 else 4),(0x1a5,1)] if n>=100 and n%2==0 else []
 out,_=dblock(q,signal(.3,n),evs=ev)
 assert all(math.isfinite(v) for v in struct.unpack('<64f',out))
q.hook_del(h)
check(True,'frozen Granular/Reverse transitions read only actual captured history, including partial-block restarts')
# Short/empty blocks do not touch output samples beyond the active prefix.
for mode in [2,3,4]:
 q,_,_=dsetup();dblock(q,ZERO,evs=INITIAL+[(MODE,mode)])
 for frames in [4,0]:
  devents(q,[]);q.mem_write(DBUS,struct.pack('<IIIIBBxx',32 if frames else 0,frames,BUF,BUF+0x100,0,1))
  values=struct.pack('<32f',*signal(.3,200));q.mem_write(BUF,values);q.mem_write(BUF+0x100,values)
  call(q,u32(q,u32(q,DOBJ)+12),DOBJ,CTX)
  check(bytes(q.mem_read(BUF+frames*4,128-frames*4))==values[frames*4:] and bytes(q.mem_read(BUF+0x100+frames*4,128-frames*4))==values[frames*4:],'type '+str(mode)+' preserves inactive output samples for frames '+str(frames))

# Native parameter table and visible lists, independently of DSP state.
MODEL=0x300173d0;SET=MODEL+0xe48;SLOT=0x24008000;LIST=0x24050000
def ui():
 q=machine();heap=[0xc0010000]
 def alloc(q):a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
 stub(q,0x8103128,alloc);call(q,0x8134cb8,0);q.mem_write(SLOT,b'\4');call(q,0x81202b8,SET,SLOT)
 w32(q,SET,0x24009000);w32(q,0x24009000,0x80dfe21);stub(q,0x80dfe20,lambda q:q.reg_write(UC_ARM_REG_R0,4))
 w32(q,MODEL+0xe78+4,0x24052000);q.mem_write(MODEL+0xe78+10,struct.pack('<HH',1,68));q.mem_write(0x24052000,struct.pack('<H2xi',0x18c,1))
 q.mem_write(SLOT,struct.pack('<H',20));return q
q=ui();stockui=machine(stock);stub(stockui,0x8103128,lambda q:q.reg_write(UC_ARM_REG_R0,0xc0010000));call(stockui,0x8134cb8,0)
for pid,value in [(0x1a7,350),(0x1a8,700),(0x1a9,300),(0x1aa,400),(0x1ab,650)]:
 check(bytes(stockui.mem_read(0xc0010000+28*pid,28))==bytes(28) and call(q,0x8120234,SET,pid)==value,'new parameter uses a previously empty definition with safe default '+hex(pid))
for mode in range(6):
 call(q,0x8120264,SET,MODE,mode);call(q,0x8120264,SET,0x35,0)
 call(q,M['symbols']['4']['dly_parameters'],MODEL,SLOT,LIST)
 ids=[struct.unpack('<H',q.mem_read(LIST+4+i*12,2))[0] for i in range(u32(q,LIST))]
 expected=([0x33,0x39,0xe,0xcb,0x4a,0x3a,0x3d,0xca,0x35,0x36,0x4b,0x37] if mode==0 else [0x1a1,0x1a2,0x1a3,0x4a,0x1a4,0x39,0x3d,0x1a5,0x1a6] if mode==1 else [0x33,0x39,0x3a,0x1a7,0x1b9,0x3d,0x1ac,0x35,0x36,0x37,0x1ad] if mode==2 else [0x33,0x39,0xe,0x1a8,0x4a,0x1a9,0x3d,0xca,0x35,0x36,0x4b,0x37] if mode==3 else [0x1aa,0x39,0x1ab,0x4a,0x3d,0x35] if mode==4 else [0x33,0x39,0xe,0xcb,0xca,0x35,0x36,0x37])
 check(ids==expected,'native controls are appropriate for type '+str(mode))
print(str(H.checks)+' new delay-type native checks passed; audio feel and hardware cycles remain device tests.')
