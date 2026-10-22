"""Compare actual ARM exp.3 and exp.4 audio and executed callback work for
master Tape, FX1 Granular and FX2 Shoegaze. Instruction and VDIV counts are
emulator measurements, not hardware CPU-cycle or CPU-percentage predictions.
The exp.3 baseline is its own released patcher, which contains only added code.
"""
from reverb_fixture import rsetup,rblock
from delay_fixture import *
from delay_fixture import makeup_ref
import firmware_harness as H
from capstone import Cs,CS_ARCH_ARM,CS_MODE_THUMB,CS_MODE_MCLASS
import importlib.util,json,random
folder=ROOT/'tests/references/tape-exp3'
check(hashlib.sha256((folder/'patch_bluebox.py').read_bytes()).hexdigest()=='9e31492e980c40aac8c909c58d214d93131cb268b7b0d7dc799b00896b96830b','comparison patcher is the pinned exp.3 release')
spec=importlib.util.spec_from_file_location('exp3',folder/'patch_bluebox.py');p3=importlib.util.module_from_spec(spec);spec.loader.exec_module(p3)
ref=p3.transform(stock)
check(hashlib.sha256(ref).hexdigest()=='19d331ccc287eed80bad7203c606d4c8d22256affdca3233650241c984b9a8e6','exp.3 reference image rebuilt from stock')
decoder=Cs(CS_ARCH_ARM,CS_MODE_THUMB|CS_MODE_MCLASS)
def profile(u,run):
 cache={};count=[0,0]
 def hook(q,a,n,d):
  count[0]+=1
  if a not in cache:
   ins=next(decoder.disasm(bytes(q.mem_read(a,n)),a),None)
   cache[a]=bool(ins and ins.mnemonic.startswith('vdiv'))
  count[1]+=cache[a]
 h=u.hook_add(UC_HOOK_CODE,hook);run();u.hook_del(h)
 return count
def compare(name,pairs,limit_peak,limit_rms,exact_expected=False):
 peak=0.;sq=0.;n=0;exact=True
 for oa,ob in pairs:
  va=struct.unpack('<64f',oa);vb=struct.unpack('<64f',ob);exact&=oa==ob
  for x,y in zip(va,vb):
   assert math.isfinite(x) and math.isfinite(y)
   e=abs(x-y);peak=max(peak,e);sq+=e*e;n+=1
 rms=(sq/n)**.5
 print('INFO',name,'peak delta',peak,'RMS delta',rms,'exact',exact)
 if exact_expected:check(exact,name+' remains PCM bit-identical to exp.3')
 else:check(peak<limit_peak and rms<limit_rms,name+' stays within peak %g / RMS %g of exp.3'%(limit_peak,limit_rms))
 return dict(name=name,samples=n,peak_absolute_error=peak,rms_error=rms,pcm_identical=exact)
records=[];profiles=[]

# ---- Master Tape/Soft ---------------------------------------------------
def master_case(name,blocks,params,**kw):
 pair=[setup(ref)[0],setup()[0]];rng=random.Random(322);out=[]
 for i in range(blocks):
  amp=(.2,.7,.001,1.2)[(i//240)%4]
  l=signal(amp,i,317);r=signal(amp*.7,i,431)
  if i%31==0:l=[x+.02*rng.uniform(-1,1) for x in l]
  ev=params(i);ev=([(0xb5,0)]+ev) if i==0 else ev
  out.append((block(pair[0],l,r,ev)[0],block(pair[1],l,r,ev)[0]))
 records.append(compare(name,out,**kw))
master_case('Soft',900,lambda i:[(0x43,[0,1,20,50,700,1000][i//150])] if i%150==0 else [],limit_peak=0,limit_rms=0,exact_expected=True)
master_case('Soft trimmed',900,lambda i:[(0x43,700),(0x1b8,[6000,0,-12000][i//300])] if i%300==0 else [],limit_peak=0,limit_rms=0,exact_expected=True)
master_case('Off',900,lambda i:[(0x43,0),(0x1b6,1),(0x1b8,6000)] if i==0 else [],limit_peak=0,limit_rms=0,exact_expected=True)
master_case('Tape steady',1100,lambda i:[(0x43,700),(0x1b6,1)] if i==0 else [],limit_peak=2e-5,limit_rms=2e-6)
def sweep(i):
 if i%400:return []
 d=(20,50,700,1000,1,0,800,700)[i//400]
 return [(0x43,d),(0x1b6,1),(0x1b7,(i//400*171)%1001),(0x1b8,(-12000,-3000,0,6000)[i//400%4])]
master_case('Tape controls and Off fade',3200,sweep,limit_peak=2e-5,limit_rms=2e-6)
master_case('Tape mode transitions',2500,lambda i:[(0x43,700),(0x1b6,(i//500)%2),(0x1b8,0)] if i%500==0 else [],limit_peak=2e-5,limit_rms=2e-6)
u,_=setup()
for i in range(700):block(u,signal(.6,i),signal(.2,i),[(0xb5,0),(0x43,700),(0x1b6,1)] if i==0 else [])
check(f32(u,0x38800990)==1.,'Tape crossfade still settles exactly, enabling the fast path')
block(u,[.7]*32,[-.2]*32,[(0x43,0),(0x1b8,6000)]);o,_=block(u,[.7]*32,[-.2]*32)
check(o==struct.pack('<64f',*([.7]*32+[-.2]*32)),'fast-path Off ramp returns exact dry unity')
for mode,drive,trim,comp in ((1,700,0,0),(1,1000,0,0),(1,20,0,0),(1,700,-3000,0),(1,700,0,1),(0,700,0,0)):
 pair=[setup(ref)[0],setup()[0]];res=[]
 for q in pair:
  for i in range(700):block(q,signal(.6,i),signal(.2,i),[(0xb5,comp),(0x132,-40000),(0x135,0),(0x43,drive),(0x1b6,mode),(0x1b8,trim)] if i==0 else [])
  res.append(profile(q,lambda:block(q,signal(.6,701),signal(.2,701))))
 label=('Tape' if mode else 'Soft')+f' Drive{drive} Out{trim} Comp{comp}'
 if mode:check(res[1][0]<res[0][0] and res[1][1]<res[0][1],'master '+label+': fewer instructions and divisions at full rate')
 else:check(res[1][0]<=res[0][0] and res[1][1]==res[0][1],'master '+label+': Soft path does no extra work (bit-identical PCM checked above)')
 profiles.append(dict(effect='master',case=label,exp3_instructions=res[0][0],exp4_instructions=res[1][0],exp3_vdiv=res[0][1],exp4_vdiv=res[1][1]))
 print('INFO callback work',profiles[-1])

# ---- FX1 Granular (actual native delay constructor/body) ------------------
GMODE=0x1a0;GSIZE=0x1a2;GDENS=0x1a3;GJIT=0x1a4;GFREEZE=0x1a5;GREV=0x1a6
pair=[dsetup(ref)[0],dsetup()[0]]
for q in pair:steady(q)
out=[]
for n in range(1500):
 x=signal(.4,n,480);ev=INITIAL if n==0 else []
 out.append((dblock(pair[0],x,evs=ev)[0],dblock(pair[1],x,evs=ev)[0]))
records.append(compare('Delay Normal with Granular priming',out,0,0,exact_expected=True))
out=[]
for n in range(2400):
 ev=[(GMODE,1),(0x39,[700,950,300][n//800]),(0x4a,[0,7,-12][n//800]),(GREV,n//800==1),(GSIZE,[80,20,250][n//800]),(GDENS,[4,2,4][n//800]),(GJIT,[0,500,1000][n//800])] if n%800==0 else []
 x=signal(.4,n,480)
 oa=dblock(pair[0],x,evs=ev)[0];ob=dblock(pair[1],x,evs=ev)[0];ra,_=makeup_ref(pair[1],oa)
 if ra is not None:out.append((ra,ob))
records.append(compare('Granular with feedback, pitch, reverse and density changes (exp.6 +3 dB wet mapped)',out,2e-5,2e-6))
out=[]
for n in range(300):
 ev=[(GMODE,0)] if n==0 else []
 x=signal(.4,n,480)
 oa=dblock(pair[0],x,evs=ev)[0];ob=dblock(pair[1],x,evs=ev)[0];ra,_=makeup_ref(pair[1],oa)
 if ra is not None:out.append((ra,ob))
records.append(compare('Granular fade back to Normal (crossfade blocks excluded)',out,2e-5,2e-6))
for label,evs in (('Delay Normal (Granular priming)',[]),('Granular density 4, feedback 70%',[(GMODE,1),(GDENS,4),(GSIZE,80),(0x39,700)]),('Granular density 1, feedback 0',[(GMODE,1),(GDENS,1),(GSIZE,250),(0x39,0)])):
 res=[]
 for img in (ref,patched):
  q=dsetup(img)[0];steady(q)
  for n in range(1600):dblock(q,signal(.3,n),evs=(INITIAL+evs) if n==0 else [])
  res.append(profile(q,lambda:dblock(q,signal(.3,1601))))
 check(res[1][0]<res[0][0],'FX1 '+label+': fewer callback instructions')
 profiles.append(dict(effect='FX1',case=label,exp3_instructions=res[0][0],exp4_instructions=res[1][0],exp3_vdiv=res[0][1],exp4_vdiv=res[1][1]))
 print('INFO callback work',profiles[-1])

# ---- FX2 Shoegaze (actual native reverb constructor/queue) ----------------
def sgsetup(img,syms):
 u,nodes,heap=rsetup(img);w32(u,0x24000004,heap);w32(u,0x24000000,0xc2000000)
 call(u,syms['sg_allocate']);assert u32(u,0x38800104)
 return u
SY3=p3.MANIFEST['symbols']['delay7'];SY4=M['symbols']['delay7']
def sg_pair():return [sgsetup(ref,SY3),sgsetup(patched,SY4)]
for label,ctl in (('Shoegaze steady, Sat 0',[(0x1af,0)]),('Shoegaze steady, Sat 35%',[(0x1af,350)]),('Shoegaze steady, full fuzz and modulation',[(0x1af,1000),(0x1b2,1000),(0x1b3,2000)])):
 a,b=sg_pair();out=[]
 for n in range(900):
  # Controls applied before the custom style starts are already settled.
  ev=[(0x155,15),(0x15c,0)]+ctl if n==0 else []
  x=signal(.3,n,311) if n<300 else [0.]*32;y=signal(.2,n,499) if n<300 else [0.]*32
  out.append((rblock(a,x,y,evs=ev)[0],rblock(b,x,y,evs=ev)[0]))
 records.append(compare(label,out,2e-5,2e-6,exact_expected=label.endswith('Sat 0')))
# Moving controls: block-rate ramps change the modulated tank's phase, not its
# level. Check per-section energy rather than sample identity.
a,b=sg_pair();ea=[0.]*6;eb=[0.]*6
for n in range(1800):
 ev=[(0x155,15),(0x15c,0)] if n==0 else [(0x1af,(n*7)%1001),(0x1b0,(n*3)%1001),(0x1b2,(n*11)%1001),(0x1b4,500+(n*97)%15500)] if n%150==0 else []
 x=signal(.3,n,311);y=signal(.2,n,499)
 oa=struct.unpack('<64f',rblock(a,x,y,evs=ev)[0]);ob=struct.unpack('<64f',rblock(b,x,y,evs=ev)[0])
 ea[n//300]+=sum(v*v for v in oa);eb[n//300]+=sum(v*v for v in ob)
changes=[10*math.log10(y/x) for x,y in zip(ea,eb)]
print('INFO Shoegaze moving-control energy change per 200 ms section (dB):',['%.3f'%c for c in changes])
check(all(abs(c)<.5 for c in changes),'Shoegaze moving controls keep wet energy within 0.5 dB of exp.3 in every section')
records.append(dict(name='Shoegaze moving controls (energy)',section_energy_change_db=changes))
for label,ctl in (('Shoegaze Sat 0',[(0x1af,0)]),('Shoegaze full fuzz, size, diffusion, modulation',[(0x1af,1000),(0x1b0,1000),(0x1b1,1000),(0x1b2,1000),(0x1b3,2000)])):
 res=[]
 for q in sg_pair():
  for n in range(250):rblock(q,signal(.4,n),evs=[(0x155,15),(0x15c,0)]+ctl if n==0 else [])
  res.append(profile(q,lambda:rblock(q,signal(.4,251))))
 check(res[1][0]<res[0][0] and res[1][1]<res[0][1],'FX2 '+label+': fewer instructions and divisions')
 profiles.append(dict(effect='FX2',case=label,exp3_instructions=res[0][0],exp4_instructions=res[1][0],exp3_vdiv=res[0][1],exp4_vdiv=res[1][1]))
 print('INFO callback work',profiles[-1])

for p in profiles:p['instruction_reduction_percent']=100*(1-p['exp4_instructions']/p['exp3_instructions'])
report=dict(version=M['version'],baseline='1.0.0-tape.exp.3',audio=records,profiles=profiles,hardware_cpu_measured=False)
(ROOT/'tests/exp4-performance.json').write_text(json.dumps(report,indent=2)+'\n')
print(H.checks,'exp.4 parity/work checks passed; whole-device CPU savings require hardware.')
