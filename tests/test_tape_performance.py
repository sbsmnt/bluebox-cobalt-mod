"""Compare actual ARM exp.1/exp.2 audio and executed callback work.
Instruction and VDIV counts are not hardware CPU-cycle measurements.
The baseline fixture contains only our added code, not the stock firmware.
"""
from firmware_harness import *
import firmware_harness as H
from capstone import Cs,CS_ARCH_ARM,CS_MODE_THUMB,CS_MODE_MCLASS
import json,random
folder=ROOT/'tests/references/tape-exp1';meta=json.loads((folder/'reference.json').read_text());code=(folder/'gr7.bin').read_bytes()
check(meta['patched_sha256']=='891641486c3bf6556f7d4340c89e228e791c6fcfa92726cacdd2383d45a1d080','comparison identifies hardware-tested experiment1')
check(hashlib.sha256(code).hexdigest()==meta['gr7_sha256'],'baseline added-code fixture has pinned checksum')
ref=bytearray(patched);ref[0x60000:0x68000]=bytes(0x8000);ref[0x60000:0x60000+len(code)]=code
struct.pack_into('<I',ref,meta['process_pointer_address']-0x08040000,meta['process_address']|1)
ref=bytes(ref)
records=[]
def audio_case(name,blocks,params):
 pair=[setup(ref)[0],setup()[0]];rng=random.Random(322);peak=0.;sq=0.;n=0;exact=True
 for i in range(blocks):
  amp=(.2,.7,.001,1.2)[(i//240)%4]
  l=signal(amp,i,317);r=signal(amp*.7,i,431)
  # Deterministic transient/noise exercises the ADAA crossing and quiet fallback.
  if i%31==0:l=[x+.02*rng.uniform(-1,1) for x in l]
  ev=params(i);ev=([(0xb5,0)]+ev) if i==0 else ev
  oa,_=block(pair[0],l,r,ev);ob,_=block(pair[1],l,r,ev)
  va=struct.unpack('<64f',oa);vb=struct.unpack('<64f',ob);exact &= oa==ob
  for x,y in zip(va,vb):
   assert math.isfinite(x) and math.isfinite(y)
   e=abs(x-y);peak=max(peak,e);sq+=e*e;n+=1
 rms=(sq/n)**.5
 print('INFO',name,'peak delta',peak,'RMS delta',rms,'exact',exact)
 check(peak<.00002 and rms<.000002,name+' ARM audio stays within strict absolute peak/RMS error bounds')
 if name in ('Soft','Soft trimmed','Off'):check(exact,name+' remains PCM bit-identical to experiment1')
 records.append(dict(name=name,samples=n,peak_absolute_error=peak,rms_error=rms,pcm_identical=exact))
audio_case('Soft',900,lambda i:[(0x43,[0,1,20,50,700,1000][i//150])] if i%150==0 else [])
audio_case('Soft trimmed',900,lambda i:[(0x43,700),(0x1b8,[6000,0,-12000][i//300])] if i%300==0 else [])
audio_case('Off',900,lambda i:[(0x43,0),(0x1b6,1),(0x1b8,6000)] if i==0 else [])
audio_case('Tape steady',1100,lambda i:[(0x43,700),(0x1b6,1)] if i==0 else [])
def sweep(i):
 if i%400:return []
 d=(20,50,700,1000,1,0,800,700)[i//400]
 return [(0x43,d),(0x1b6,1),(0x1b7,(i//400*171)%1001),(0x1b8,(-12000,-3000,0,6000)[i//400%4])]
audio_case('Tape controls and Off fade',3200,sweep)
def switching(i):
 if i%500:return []
 return [(0x43,700),(0x1b6,(i//500)%2),(0x1b8,0)]
audio_case('mode transitions',2500,switching)
# A one-sided fade in exp1 stalls before1; the endpoint now reaches exactly1.
u,_=setup()
for i in range(700):block(u,signal(.6,i),signal(.2,i),[(0xb5,0),(0x43,700),(0x1b6,1)] if i==0 else [])
check(f32(u,0x38800990)==1.,'Tape crossfade settles exactly, enabling the fast path')
# Force a Drive-Off transition directly from that fast path.
block(u,[.7]*32,[-.2]*32,[(0x43,0),(0x1b8,6000)])
o,_=block(u,[.7]*32,[-.2]*32)
check(o==struct.pack('<64f',*([.7]*32+[-.2]*32)),'fast-path Off ramp returns exact dry unity')
# Count complete native master callbacks using the same input and timing state.
# The published percentages from the existing profiler are not synthesized.
profiles=[];decoder=Cs(CS_ARCH_ARM,CS_MODE_THUMB|CS_MODE_MCLASS)
for drive,trim,comp in ((700,0,0),(1000,0,0),(20,0,0),(700,-3000,0),(700,0,1)):
 pair=[setup(ref)[0],setup()[0]]
 for i in range(700):
  for q in pair:block(q,signal(.6,i),signal(.2,i),[(0xb5,comp),(0x132,-40000),(0x135,0),(0x43,drive),(0x1b6,1),(0x1b8,trim)] if i==0 else [])
 counts=[];divs=[]
 for q in pair:
  cache={};count=[0,0]
  def hook(q,a,n,d):
   count[0]+=1
   if a not in cache:
    ins=next(decoder.disasm(bytes(q.mem_read(a,n)),a),None)
    cache[a]=bool(ins and ins.mnemonic.startswith('vdiv'))
   count[1]+=cache[a]
  h=q.hook_add(UC_HOOK_CODE,hook);block(q,signal(.6,701),signal(.2,701));q.hook_del(h)
  counts.append(count[0]);divs.append(count[1])
 reduction=100*(1-counts[1]/counts[0])
 check(reduction>15 and divs[1]<divs[0],f'steady Tape removes callback work without reducing the sample rate: Drive{drive}, Out{trim}, Comp{comp}')
 profiles.append(dict(drive=drive,output_mdb=trim,compressor_enabled=bool(comp),exp1_instructions=counts[0],exp2_instructions=counts[1],instruction_reduction_percent=reduction,exp1_vdiv=divs[0],exp2_vdiv=divs[1]))
 print('INFO callback work',profiles[-1])
# exp.4 deliberately optimizes delay7 (see test_exp4_performance.py).
for name,digest in meta['preserved_payloads'].items():
 if name not in ('delay7.bin','gate7.bin'):check(hashlib.sha256((ROOT/'build'/name).read_bytes()).hexdigest()==digest,'unchanged tested exp1 effect/profiler payload '+name)
# M4 now intentionally changes passive presentation/error priority; covered by recording diagnostics.
report=dict(version=M['version'],baseline=meta['version'],audio=records,profiles=profiles,hardware_cpu_measured=False)
(ROOT/'tests/tape-performance.json').write_text(json.dumps(report,indent=2)+'\n')
print(H.checks,'Tape optimization parity/work checks passed; whole-device CPU savings require hardware.')
