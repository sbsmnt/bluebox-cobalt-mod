"""exp.8 against the pinned exp.7 release: control feel (log Hz/rate travel,
slower Pitch, softer Gate acceleration), labels and names, Tape Lo C with the
native filter bypassed, and the optimized Reverse renderer. Emulator checks;
feel and sound need hardware.
"""
from delay_fixture import *
import firmware_harness as H
import importlib.util,json
folder=ROOT/'tests/references/tape-exp7'
check(hashlib.sha256((folder/'patch_bluebox.py').read_bytes()).hexdigest()=='3f0899ec2b8b4b879736d6ca46b2e62ac226d2e8cef4aea1686e9dc3fee022e0','comparison patcher is the pinned exp.7 release')
spec=importlib.util.spec_from_file_location('exp7',folder/'patch_bluebox.py');p7=importlib.util.module_from_spec(spec);spec.loader.exec_module(p7)
ref=p7.transform(stock)
check(hashlib.sha256(ref).hexdigest()=='52e2e625858c8499060010da4fea2157dca184e7fd0f37ebc980f28e0894d4b0','exp.7 reference image rebuilt from stock')
report={}
W=0x24010000;CLOCK=0x3001eb3c;S4=M['symbols']['4']
T_LOCUT=0x1b9;T_HICUT=0x1ac

# ---- 1. Parameter definitions, labels and names (real native initializer).
u=machine();heap=[0xc0010000]
def alloc(q):a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0);stub(u,0x813a414,lambda q:None)
TABLE=0xc0010000
def label(pid):
 ptr=u32(u,TABLE+pid*28+4)
 return bytes(u.mem_read(ptr,16)).split(b'\0')[0].decode()
def rng(pid):return struct.unpack('<ii',u.mem_read(TABLE+pid*28+16,8))
for pid,lab in ((0x1b4,'Hi C:'),(0x1b5,'Lo C:'),(T_HICUT,'Hi C:'),(T_LOCUT,'Lo C:')):
 check(label(pid)==lab,'label %s is %r'%(hex(pid),lab))
check(rng(T_LOCUT)==(20,2000),'Tape Lo C registers 20..2000 Hz in the free stock slot 0x1b9')
check(b'Gazer\0' in patched and b'Shoegaze\0' not in patched[0x78000:0x80000],'FX2 style list and title read Gazer')
check(b'Granulr\0' in patched,'FX1 Granular title reads Granulr')

# ---- 2. Encoder travel through dly_turn (real native knob/binding paths).
clk=[60000]
def bind(pid,slot,value):
 u.mem_write(W,bytes(0x3d0));u.mem_write(W+0x32,struct.pack('<H',slot));u.mem_write(W+0x3c9,b'\1')
 call(u,0x8127b84,W,pid,value&0xffffffff,1)
def val():return struct.unpack('<i',u.mem_read(W+0x98,4))[0]
def turn(delta,ms=1000):
 clk[0]+=ms;w32(u,CLOCK,clk[0]);call(u,S4['dly_turn'],W,delta&0xffffffff)
def sweep_units(pid,slot,lo,hi,delta=40):
 bind(pid,slot,lo);n=0
 while val()<hi and n<5000:turn(delta);n+=1
 return n*delta,val()
travel={}
for name,pid,slot,lo,hi in (('Gazer Rate',0x1b3,21,10,2000),('Gazer Hi C',0x1b4,21,500,16000),('Gazer Lo C',0x1b5,21,20,2000),('Tape Hi C',T_HICUT,20,500,16000),('Tape Lo C',T_LOCUT,20,20,2000)):
 units,end=sweep_units(pid,slot,lo,hi)
 travel[name]=units
 print('INFO %s full sweep %d units (Diffusion-like target ~40000)'%(name,units))
 # Integer Hz/mHz cannot step finer than 1 at the bottom, so those steps round
 # up to 1 and the very bottom passes faster than a pure log sweep.
 check(end==hi and 20000<=units<=42000,'%s sweeps its full range in Diffusion-like travel (%d units)'%(name,units))
# Equal musical steps: ratio between successive values stays roughly constant.
bind(0x1b4,21,1000);vals=[val()]
for i in range(40):turn(40);vals.append(val())
ratios=[b/a for a,b in zip(vals,vals[1:]) if b!=a]
check(min(ratios)>1.002 and max(ratios)<1.006,'Hi C steps are equal ratios (%.4f..%.4f per step)'%(min(ratios),max(ratios)))
bind(0x1b4,21,6000);turn(40);up=val();turn(-40)
check(up>6000 and val()==6000 or abs(val()-6000)<=2,'Hi C reverses one step without a jump')
# Small events accumulate (fraction kept): 4 x 10 units = one step.
bind(0x1b5,21,500)
for i in range(3):turn(10,1000)
mid=val();turn(10,1000)
check(mid==500 and val()>500,'sub-step turns accumulate into one Lo C step')
# Pitch: 160 slow units per semitone (Granular and Reverse share 0x4a).
bind(0x4a,20,0);turn(80);half=val();turn(80)
check(half==0 and val()==1,'Pitch needs 160 slow units per semitone (was ~42)')
units,end=sweep_units(0x4a,20,-12,12,delta=160)
check(end==12 and units==24*160,'Pitch sweeps -12..+12 in 24 x 160 slow units')
report['full_sweep_units']=travel
# Diffusion itself is unchanged native travel (reference for the feel).
bind(0x1b1,21,0);n=0
while val()<1000 and n<3000:turn(40);n+=1
print('INFO Diffusion native full sweep',n*40,'units')
check(abs(n*40-40000)<=400,'Diffusion keeps its native ~40000-unit travel')

# ---- 3. Gate fast acceleration is softer than exp.7 (same widget/clock fixture).
import gate_ui_fixture as GU
def gate_sweep(img_symbols,delta,ms):
 GU.setup(1,0,-96000);w32(GU.u,0x38800a40+8,0);n=0;k=GU.P+0xbe0+0x8f0
 while GU.val(k)!=-12000 and n<400:
  GU.clock[0]+=ms;w32(GU.u,CLOCK,GU.clock[0]);call(GU.u,img_symbols['gate_turn'],k,delta&0xffffffff);GU.flush();n+=1
 return n
n8=gate_sweep(GU.S,160,10)
print('INFO Gate fast sweep events exp.8:',n8,'(exp.7: 14)')
check(18<=n8<=40,'Gate fast sweep is gentler than exp.7 but still quick (%d events)'%n8)

# ---- 4. Tape: list, layout, Lo C, native filter bypass.
pair=lambda:(dsetup(ref)[0],dsetup()[0])
TAPE=[(0x1a0,2),(0x1a7,600),(0x39,650),(0x3d,0)]
a,b=pair()
for q in (a,b):dblock(q,ZERO,evs=INITIAL+[(0xca,0)]);steady(q)
same=True
for n in range(1200):
 x=signal(.3,n,180);ev=TAPE if n==0 else []
 oa,_=dblock(a,x,evs=ev);ob,_=dblock(b,x,evs=ev);same&=oa==ob
check(same,'Tape with Lo C Off and FILT off is bit-identical to exp.7')
# FILT on: exp.8 Tape ignores the native filter (equals exp.7 with FILT off),
# keeps the saved flag, and Normal mode still filters.
c=dsetup()[0];dblock(c,ZERO,evs=INITIAL+[(0xca,1)]);steady(c)
a2=dsetup(ref)[0];dblock(a2,ZERO,evs=INITIAL+[(0xca,0)]);steady(a2)
same=True
for n in range(1200):
 x=signal(.3,n,180);ev=TAPE if n==0 else []
 oc,_=dblock(c,x,evs=ev);oa,_=dblock(a2,x,evs=ev)
 # Only the setup block ran the native filter (before Tape): its residue
 # decays to ~1e-16; exp.7 FILT on vs off differs by ~3e-2 here.
 if n>20:same&=max(abs(p-q) for p,q in zip(struct.unpack('<64f',oc),struct.unpack('<64f',oa)))<1e-9
check(same,'Tape bypasses the native filter even with FILT saved on (matches exp.7 FILT off within 1e-9)')
check(c.mem_read(DOBJ+0x270,1)==b'\1','saved FILT flag is kept while Tape bypasses it')
dblock(c,ZERO,evs=[(0xca,0)]);check(c.mem_read(DOBJ+0x270,1)==b'\0','FILT off during Tape is stored')
dblock(c,ZERO,evs=[(0xca,1)]);check(c.mem_read(DOBJ+0x270,1)==b'\1','FILT on during Tape is stored')
# Lo C removes low end from the repeats; Off is exact.
def tape_energy(locut,hz):
 q=dsetup()[0];dblock(q,ZERO,evs=INITIAL+[(0xca,0)]);steady(q)
 e=0.
 for n in range(1500):
  o,_=dblock(q,signal(.3,n,hz),evs=TAPE+[(T_LOCUT,locut)] if n==0 else [])
  if n>700:e+=sum(v*v for v in struct.unpack('<64f',o))
 return e
low_off,low_on=tape_energy(20,80),tape_energy(800,80)
high_off,high_on=tape_energy(20,4000),tape_energy(800,4000)
drop_low=10*math.log10(low_on/low_off);drop_high=10*math.log10(high_on/high_off)
print('INFO Tape Lo C 800 Hz: 80 Hz repeats %+.1f dB, 4 kHz repeats %+.1f dB'%(drop_low,drop_high))
check(drop_low<-6. and drop_high>-2.,'Lo C 800 Hz cuts low repeats and leaves highs')
report['tape_locut_800hz_db']={'80Hz':drop_low,'4kHz':drop_high}

# ---- 5. Reverse: optimized renderer within float rounding of exp.7, less work.
a,b=pair()
for q in (a,b):dblock(q,ZERO,evs=INITIAL);steady(q)
worst=0.
for n in range(1500):
 ev=[(0x1a0,4),(0x39,600),(0x4a,(0,7,-5)[n//500])] if n%500==0 else []
 oa,_=dblock(a,signal(.3,n,480),signal(.2,n,733),ev);ob,_=dblock(b,signal(.3,n,480),signal(.2,n,733),ev)
 worst=max(worst,max(abs(x-y) for x,y in zip(struct.unpack('<64f',oa),struct.unpack('<64f',ob))))
print('INFO Reverse largest difference from exp.7:',worst)
check(worst<2e-5,'Reverse audio within float rounding of exp.7 (%.3g)'%worst)
from capstone import Cs,CS_ARCH_ARM,CS_MODE_THUMB,CS_MODE_MCLASS
dec=Cs(CS_ARCH_ARM,CS_MODE_THUMB|CS_MODE_MCLASS)
def work(img):
 q=dsetup(img)[0];dblock(q,ZERO,evs=INITIAL);steady(q)
 for n in range(1600):dblock(q,signal(.3,n),evs=[(0x1a0,4)] if n==0 else [])
 c=[0,0];cache={}
 def h(uu,addr,size,d):
  c[0]+=1
  if addr not in cache:
   i=next(dec.disasm(bytes(uu.mem_read(addr,size)),addr),None);cache[addr]=bool(i and i.mnemonic.startswith('vdiv'))
  c[1]+=cache[addr]
 hk=q.hook_add(UC_HOOK_CODE,h);dblock(q,signal(.3,1601));q.hook_del(hk);return c
r7,r8=work(ref),work(patched)
print('INFO Reverse callback work exp.7',r7,'exp.8',r8)
check(r8[0]<r7[0]*.95 and r8[1]<r7[1],'Reverse callback does less work than exp.7')
report['reverse_work']=dict(exp7_instructions=r7[0],exp8_instructions=r8[0],exp7_vdiv=r7[1],exp8_vdiv=r8[1])
(ROOT/'tests/exp8-controls.json').write_text(json.dumps(dict(version=M['version'],baseline='1.0.0-tape.exp.7',**report,hardware_tested=False),indent=2)+'\n')
print(H.checks,'exp.8 checks passed; encoder feel, labels on screen and sound need hardware.')
