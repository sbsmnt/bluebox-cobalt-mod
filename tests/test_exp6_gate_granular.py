"""exp.6 against the pinned exp.5 release: bit-identical noise gate with less
work, and +3 dB Granular wet output with unchanged feedback/capture state.
Real native EQ/gate and delay code under the emulator; instruction counts are
not CPU percentages.
"""
from gate_fixture import *
import delay_fixture as D
import firmware_harness as H
import importlib.util,json
folder=ROOT/'tests/references/tape-exp5'
check(hashlib.sha256((folder/'patch_bluebox.py').read_bytes()).hexdigest()=='b640a13626cc734a1a233b6651adef8f49e6500de76b8652dfbb4387df537dff','comparison patcher is the pinned exp.5 release')
spec=importlib.util.spec_from_file_location('exp5',folder/'patch_bluebox.py');p5=importlib.util.module_from_spec(spec);spec.loader.exec_module(p5)
ref=p5.transform(stock)
check(hashlib.sha256(ref).hexdigest()=='1146218457afd7cb1ec1e706d49939ede45f49863e235aa75d2d18c36ca3c0fd','exp.5 reference image rebuilt from stock')
def gate_pair(ch=0):
 out=[]
 for img,sym in ((ref,p5.MANIFEST['symbols']['gate7']),(patched,M['symbols']['gate7'])):
  u=machine(img,double=True);heap=[0xc0010000]
  def alloc(u,heap=heap):
   a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
  stub(u,0x80412a4,alloc)
  call(u,sym['gate_construct'],EQ,4)
  w32(u,EQ+0x18,ch);u.mem_write(EQ+0x1e,struct.pack('<H',ch))
  w32(u,EQ+8,NEXT);w32(u,NEXT,VT);w32(u,VT+12,NEXT_FN|1);stub(u,NEXT_FN,lambda u:u.reg_write(UC_ARM_REG_R0,7))
  out.append(u)
 return out
from capstone import Cs,CS_ARCH_ARM,CS_MODE_THUMB,CS_MODE_MCLASS
decoder=Cs(CS_ARCH_ARM,CS_MODE_THUMB|CS_MODE_MCLASS)
def work(u,run):
 cache={};c=[0,0]
 def hook(q,a,n,d):
  c[0]+=1
  if a not in cache:
   i=next(decoder.disasm(bytes(q.mem_read(a,n)),a),None);cache[a]=bool(i and i.mnemonic.startswith('vdiv'))
  c[1]+=cache[a]
 h=u.hook_add(UC_HOOK_CODE,hook);run();u.hook_del(h);return c
TRACK=NGTRACK
records=[]

# 1. Gate: bit-identical audio and state through every phase, stereo and mono.
for stereo in (True,False):
 a,b=gate_pair();same=True;rng=__import__('random').Random(7)
 for n in range(2600):
  amp=(.5,.002,.3,0.,.05,.8,.0005)[(n//300)%7]
  l=[amp*math.sin(.07*(n*32+i))+amp*.1*rng.uniform(-1,1) for i in range(32)]
  r=[x*.6 for x in l] if stereo else None
  if n==1300:l[5]=float('nan');l[9]=float('inf')
  ev=[(0x1ae,-40000)] if n==0 else [(0x1ae,(-30000,-60000,-96000,-50000)[(n//650)%4])] if n%650==0 else []
  silent=n%997==500
  oa,ra=gblock(a,l,r,ev,silent=silent);ob,rb=gblock(b,l,r,ev,silent=silent)
  same&=oa==ob and ra==rb and a.mem_read(TRACK,40)==b.mem_read(TRACK,40)
 check(same,'gate PCM, return value and per-track state bit-identical to exp.5 ('+('stereo' if stereo else 'mono')+'): opening, holds, releases, closed, threshold edits, Off, nonfinite input, silent bus')
# 2. Gate work per block by state.
for label,thr,amp in (('Gate Off',-96000,.3),('Gate open, steady',-60000,.3),('Gate closed, steady',-20000,.001)):
 res=[]
 for u in gate_pair():
  for n in range(400):gblock(u,signal(amp,n),signal(amp,n,700),evs=[(0x1ae,thr)] if n==0 else [])
  res.append(work(u,lambda:gblock(u,signal(amp,401),signal(amp,401,700))))
 records.append(dict(case=label,exp5_instructions=res[0][0],exp6_instructions=res[1][0],exp5_vdiv=res[0][1],exp6_vdiv=res[1][1]))
 print('INFO gate callback work',records[-1])
 if 'Off' in label:check(res[1][0]<=res[0][0],label+': no extra work')
 else:check(res[1][0]<res[0][0]*.6,label+': at least 40% fewer instructions per block than exp.5')

# 3. Granular: wet output is exp.5's times G_MAKEUP (one float multiply); grain,
# feedback and capture state are bit-identical, so the repeats' loop is too.
G=struct.unpack('<f',struct.pack('<f',1.41253754))[0]
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
a=D.dsetup(ref)[0];b=D.dsetup()[0]
for u in (a,b):D.dblock(u,D.ZERO,evs=D.INITIAL);D.steady(u)
exact=True;state=True;normal=True;scaled=0;prev=0.
for n in range(2400):
 mode=0 if n<300 or 1500<=n<1800 else 1
 ev=[(0x1a0,1),(0x1a3,4),(0x1a2,80),(0x39,700),(0x1a4,0),(0x4a,0)] if n==300 else [(0x1a0,mode)] if n in (1500,1800) else []
 oa,_=D.dblock(a,signal(.3,n,480),signal(.2,n,733),ev);ob,_=D.dblock(b,signal(.3,n,480),signal(.2,n,733),ev)
 state&=a.mem_read(0x38800000,168)==b.mem_read(0x38800000,168)
 fade=struct.unpack('<f',bytes(b.mem_read(0x38800000+72,4)))[0]
 if fade==0. and n<300:normal&=oa==ob
 decor=struct.unpack('<f',bytes(b.mem_read(0x388000a8,4)))[0]
 if fade==1. and prev==1. and decor==0.:   # fully wet, exp.7 blend inactive
  va=struct.unpack('<64f',oa);vb=struct.unpack('<64f',ob)
  exact&=all(f32(x*G)==y for x,y in zip(va,vb));scaled+=1
 prev=fade
check(normal,'Delay Normal before Granular is bit-identical to exp.5')
check(state,'Granular grain, capture writer, feedback and fade state are bit-identical to exp.5 (loop gain unchanged)')
check(scaled>1500 and exact,'fully wet coherent Granular (jitter 0, pitch 0) equals exp.5 output x 1.41253754 (+3 dB) to the bit, %d blocks'%scaled)
gb=[bytes(u.mem_read(u32(u,0x38800008),768000)) for u in (a,b)]
check(gb[0]==gb[1],'Granular feedback capture history is bit-identical to exp.5')
(ROOT/'tests/exp6-gate-granular.json').write_text(json.dumps(dict(version=M['version'],baseline='1.0.0-tape.exp.5',gate=records,granular_makeup_db=20*math.log10(G),hardware_cpu_measured=False),indent=2)+'\n')
print(H.checks,'exp.6 gate/Granular checks passed; device CPU and listening remain hardware tests.')
