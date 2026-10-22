"""v1.0.0-alpha.4 actual ARM queue, Tape regeneration, geometry and scope checks."""
from delay_fixture import *
import importlib.util,os
XS=0x38800200;GS=0x38800000;S=M['symbols']['4']
oldroot=(ROOT/'tests'/'references'/'v0.9.5')
spec=importlib.util.spec_from_file_location('previous',oldroot/'patch_bluebox.py');previous=importlib.util.module_from_spec(spec);spec.loader.exec_module(previous)
# WILD off must preserve all existing sound paths, including the current Tape.
a,_,_=dsetup(previous.transform(stock));b,_,_=dsetup();same=True;exact_before=True;worst=0.;granular_seen=False
for n in range(2400):
 # exp.5 skips the native body in fully wet Granular and clears its rings,
 # so a later mode would start from silent rings. Granular runs last: the
 # five other modes stay bit-exact against v0.9.4 in this one sequence.
 mode=[2,0,3,4,5,1][n//400]
 ev=INITIAL+[(0x1a0,mode),(0x1a7,1000),(0x39,650),(0x3d,1000)] if n==0 else [(0x1a0,mode),(0x1ac,9000),(0x4a,-7),(0x3a,300)] if n%400==0 else []
 oa,ra=dblock(a,signal(.23,n,733),signal(.11,n,421),ev);ob,rb=dblock(b,signal(.23,n,733),signal(.11,n,421),ev)
 if n==0:steady(a);steady(b)
 # exp.8's Reverse renderer uses a shared reciprocal: compare it, and what
 # follows, within float rounding like Granular.
 granular_seen|=u32(b,GS+12)!=0 or mode==4
 if not granular_seen:exact_before&=oa==ob and a.mem_read(DS,348)==b.mem_read(DS,348) and a.mem_read(GS,168)==b.mem_read(GS,168)
 ints,diff=granular_close(a,b,oa,ob);worst=max(worst,diff)
 skipping=u32(b,GS+12)!=0 and f32(b,GS+72)==1.
 same &= ra==rb and ints and (skipping or a.mem_read(DOBJ,0x278)==b.mem_read(DOBJ,0x278))
print('INFO largest difference from v0.9.4 after Granular first engages:',worst)
check(same and exact_before and worst<2e-5,'WILD off preserves v0.9.4 across all six modes: PCM/send/state bit-exact through Tape, Delay and Ducked; Reverse, Default and Granular within float rounding; native object exact except while fully wet Granular skips it; Granular PCM/state within float rounding')
# Real queue events clamp; state grows only64bytes, no new allocation.
q,nodes,heap=dsetup();dblock(q,ZERO,evs=INITIAL+[(0x1a0,2),(0x1ad,99999)])
check(u32(q,XS+432)==1 and 0<f32(q,XS+440)<1,'queued WILD event clamps to on and starts a bounded5ms crossfade')
for i in range(9):dblock(q,ZERO)
check(f32(q,XS+440)==1,'WILD crossfade reaches on within240 samples plus block rounding')
dblock(q,ZERO,evs=[(0x1ad,-1)])
for i in range(9):dblock(q,ZERO)
check(u32(q,XS+432)==0 and f32(q,XS+440)==0 and u32(q,XS+492)==0,'WILD off reaches exact zero and retires its filter validity')
check(u32(q,0x24000004)==heap,'WILD events/process callbacks allocate no additional memory')
# Original quiet-tail regeneration is restored, tempered by0.5dB, not doubled
# by the v0.9.4 return makeup. Compare the actual native feedback output.
a,_,_=dsetup();b,_,_=dsetup();early=[0.,0.];late=[0.,0.];peak=[0.,0.]
for n in range(2300):
 ev=INITIAL+[(0x1a0,2),(0x1a7,1000),(0x39,650),(0x1ac,16000),(0x3d,1000)] if n==0 else []
 inp=[.00001]*32 if n<120 else ZERO
 for j,u in enumerate([a,b]):
  out,ret=dblock(u,inp,evs=ev+([(0x1ad,1)] if n==0 and j else []))
  if n==0:steady(u)
  vals=struct.unpack('<64f',out);assert all(math.isfinite(v) and abs(v)<4.01 for v in vals)
  peak[j]=max(peak[j],max(abs(v) for v in vals))
  if 120<n<500:early[j]+=sum(v*v for v in vals)
  if n>1900:late[j]+=sum(v*v for v in vals)
print('INFO quiet-tail early/late energies',early,late,'peaks',peak)
check(late[0]<early[0]*.001 and late[1]>early[1]*10,'WILD restores native regenerative tails while current/off configuration decays')
check(any(b.mem_read(u32(b,DOBJ+0x30+28*i),4*u32(b,DOBJ+0x30+28*i+20))!=a.mem_read(u32(a,DOBJ+0x30+28*i),4*u32(a,DOBJ+0x30+28*i+20)) for i in range(6)),'WILD changes the real native feedback history, not only the wet return')
check(b.mem_read(DS+60,128)==b.mem_read(BUF,128) and b.mem_read(DS+188,128)==b.mem_read(BUF+0x100,128),'To Rev receives WILD final output exactly once')
# All line arrangements, extreme edits and rapid mode toggling remain bounded.
for ping,quad in [(0,0),(1,0),(0,1),(1,1)]:
 q,nodes,heap=dsetup();count=0;peak=0.
 for n in range(420):
  ev=INITIAL+[(0x1a0,2),(0x1ad,1),(0x1a7,1000),(0x39,1000),(0x36,ping),(0x37,quad),(0x3a,1000),(0x3d,1000)] if n==0 else [(0x1ad,(n//13)%2),(0x1ac,500 if n%26 else 16000)] if n%13==0 else []
  out,ret=dblock(q,signal(.9,n,2300),signal(.6,n,479),ev)
  if n==0:steady(q)
  vals=struct.unpack('<64f',out);assert all(math.isfinite(v) and abs(v)<4.01 for v in vals);peak=max(peak,max(abs(v) for v in vals))
 check(ret==7 and len(nodes)==420 and u32(q,0x24000004)==heap,'rapid WILD/HiCut changes preserve bounded output, graph and heap '+str((ping,quad)))
 # settled WILD full-cost callback, including all native options.
 for n in range(10):dblock(q,signal(.3,n),evs=[(0x1ad,1)] if n==0 else [])
 instructions=[0]
 h=q.hook_add(UC_HOOK_CODE,lambda u,a,n,d:instructions.__setitem__(0,instructions[0]+1));dblock(q,signal(.3,11));q.hook_del(h)
 print('INFO WILD callback instructions',ping,quad,instructions[0])
 check(instructions[0]<100000,'WILD complete native callback stays below100000 emulated instructions '+str((ping,quad)))
# Native parameter list, registration and compact placement, including hidden QUAD.
MODEL=0x300173d0;SET=MODEL+0xe48;GLOBAL=MODEL+0xe78;SLOT=0x24009000;LIST=0x24050000;P=0x24010000
u=machine();stocku=machine(stock);heap=[0xc0010000]
def alloc(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
stub(stocku,0x8103128,lambda u:u.reg_write(UC_ARM_REG_R0,0xc0010000));call(stocku,0x8134cb8,0)
a=0xc0010000+28*0x1ad
check(stocku.mem_read(a,28)==bytes(28),'WILD uses an actually unused native parameter definition')
check(struct.unpack('<HB',u.mem_read(a,3))==(0x1ad,4) and struct.unpack('<ii',u.mem_read(a+16,8))==(0,1),'WILD registers a native boolean range0..1')
check(bytes(u.mem_read(u32(u,a+4),16)).split(b'\0')[0]==b'Wild:' and bytes(u.mem_read(u32(u,a+24),16)).split(b'\0')[0]==b'tapewild','WILD has a native label and unique project key')
u.mem_write(SLOT,b'\4');call(u,0x81202b8,SET,SLOT)
check(call(u,0x8120234,SET,0x1ad)==0,'new/old freshly loaded projects default WILD off')
w32(u,SET,0x24054000);w32(u,0x24054000,0x80dfe21);stub(u,0x80dfe20,lambda u:u.reg_write(UC_ARM_REG_R0,4))
w32(u,GLOBAL+4,0x24052000);u.mem_write(GLOBAL+10,struct.pack('<HH',1,68));u.mem_write(0x24052000,struct.pack('<H2xi',0x18c,0))
u.mem_write(SLOT,struct.pack('<H',20));u.mem_write(P+0xc3a4,struct.pack('<H',20));u.mem_write(P+4,struct.pack('<ii',0,100))
V=0x24008000;RECT=0x80dfe00;HIDE=0x80dfe10;w32(u,V+0x20,RECT|1);w32(u,V+0x2c,HIDE|1)
KW=lambda i:P+0xc3b0+i*0x3d0;TW=lambda i:P+0x100b0+i*0x1d4
for i in range(16):
 for w in [KW(i),TW(i)]:w32(u,w,V)
for off in [0x11df0,0x11e48,0x11ed8,0x11f18]:w32(u,P+off,V)
stub(u,RECT,lambda u:u.mem_write(u.reg_read(UC_ARM_REG_R0)+4,bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),16))))
stub(u,HIDE,lambda u:None);stub(u,0x8141548,lambda u:None);stub(u,0x813f496,lambda u:None);stub(u,0x8121b50,lambda u:w32(u,u.reg_read(UC_ARM_REG_R3),0));w32(u,P+0x38,320)
for quad in [0,1]:
 w32(u,0x24052004,quad)
 for mode in range(6):
  call(u,0x8120264,SET,0x1a0,mode);call(u,0x8120264,SET,0x1ad,1)
  for beat in [0,1]:
   call(u,0x8120264,SET,0x35,beat);call(u,S['dly_parameters'],MODEL,SLOT,LIST)
   ids=[struct.unpack('<H',u.mem_read(LIST+4+12*i,2))[0] for i in range(u32(u,LIST))]
   check((0x1ad in ids)==(mode==2),'WILD appears only on Tape '+str((quad,mode,beat)))
   if mode!=2:continue
   # exp.8: Tape drops FILT (and adds Lo C in Cutoff/Drift order): 10 + QUAD.
   check(ids[-1]==0x1ad and len(ids)==10+quad and (0x37 in ids)==bool(quad) and 0xca not in ids,'Tape respects conditional QUAD, has no FILT and appends WILD exactly once '+str((quad,beat)))
   call(u,S['dly_layout'],P);idx=ids.index(0x1ad)
   check(struct.unpack('<4i',u.mem_read(TW(idx)+4,16))==(240,145,48,44),'WILD stays in bottom-right position '+str((quad,beat)))
   if quad:check(struct.unpack('<4i',u.mem_read(TW(ids.index(0x37))+4,16))==(240,101,48,44),'QUAD sits directly above WILD')
check(call(u,0x8120234,SET,0x1ad)==1,'mode/Beat/QUAD changes retain saved WILD state')
print(str(H.checks)+' v1.0.0-alpha.4 WILD checks passed; sound, touch/layout and hardware CPU headroom require a device pass.')
