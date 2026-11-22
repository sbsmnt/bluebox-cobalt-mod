"""Matched I3/current production ARM audio streams and callback costs.
No tolerance hides a changed waveshaper, coefficient ramp or filter history.
"""
from gate_fixture import *
import firmware_harness as H
import base64,json,gc
parent=json.loads((ROOT/'tests/references/cobalt-inserts-I3-manifest.json').read_text())
i3=bytearray(stock)
for p in parent['patches']:
 data=base64.b64decode(p['new']);i3[p['offset']:p['offset']+len(data)]=data
i3=bytes(i3)
check(hashlib.sha256(i3).hexdigest()=='c0caa8e80e274a771a2b2f201f905c5e0fc4ba218b3710a867d9bf3ce7d18c65','comparison is the exact hardware-tested I3 output')
def old_setup(stereo=True):
 q,seen,_=gsetup(image=i3,stereo=stereo)
 call(q,parent['symbols']['gate7']['gate_construct'],EQ,4)
 w32(q,EQ+0x18,0);q.mem_write(EQ+0x1e,b'\0\0');w32(q,EQ+8,NEXT)
 return q
# S8f: WRAP is anti-aliased now, so only CLIP (to 70% Drive) and FOLD stay
# bit-identical to I3; WRAP is compared by level below.
for stereo in (False,True):
 for mode in (1,3):
  old=old_setup(stereo);new,_,_=gsetup(stereo=stereo)
  pcm=True;state=True
  for b in range(1150):
   ev=[(0x1ba,mode),(0x1bb,350),(0x1bc,650),(0x1bd,1000),(0x1be,0)] if b==0 else [(0x1bb,1000 if mode!=1 else 700)] if b==160 else [(0x1bd,430),(0x1be,-3000)] if b==310 else [(0x1bc,230)] if b==460 else [(0x1ba,0)] if b==610 else [(0x1ba,mode)] if b==740 else [(0x1ba,{1:3,3:1}[mode])]+([(0x1bb,700)] if mode==3 else []) if b==900 else []
   # Block-size changes must re-evaluate float fixed points rather than
   # incorrectly reuse a settled flag from a shorter block.
   n=(1,7,15,31,32)[b%5] if 1050<=b<1080 else 32
   silence=1080<=b<1085
   l=signal(.31,b,719);r=signal(.13,b,523) if stereo else None
   a,ar=gblock(old,l,r,ev,frames=n,silent=silence);c,cr=gblock(new,l,r,ev,frames=n,silent=silence)
   pcm &= a==c and ar==cr==7
   state &= bytes(old.mem_read(EQ+0x1d0,68))==bytes(new.mem_read(EQ+0x1d0,68))
   if not pcm or not state:raise AssertionError('I3/current mismatch at '+str((stereo,mode,b,n,pcm,state)))
  check(pcm and state,'bit-identical PCM and legacy state through steady/ramp/bypass/mode/silence/frame changes '+str((stereo,mode)))
  del old,new;gc.collect()
def wrap_events(b):
 return [(0x1ba,4),(0x1bb,350),(0x1bc,650),(0x1bd,1000),(0x1be,0)] if b==0 else [(0x1bb,1000)] if b==160 else [(0x1bd,430),(0x1be,-3000)] if b==310 else [(0x1bc,230)] if b==460 else [(0x1ba,0)] if b==610 else [(0x1ba,4)] if b==740 else []
for stereo in (False,True):
 old=old_setup(stereo);new,_,_=gsetup(stereo=stereo)
 eo=en=0.;finite=True;dry=True
 for b in range(900):
  l=signal(.31,b,719);r=signal(.13,b,523) if stereo else None
  a,_=gblock(old,l,r,wrap_events(b));c,_=gblock(new,l,r,wrap_events(b))
  xa=struct.unpack('<%df'%(len(a)//4),a);xc=struct.unpack('<%df'%(len(c)//4),c)
  finite &= all(math.isfinite(x) and abs(x)<2 for x in xc)
  if 712<=b<740:dry &= a==c
  if b<610:eo+=sum(x*x for x in xa);en+=sum(x*x for x in xc)
 ratio=10*math.log10(en/eo)
 check(finite and dry and -3<ratio<1,'anti-aliased WRAP stays bounded, near I3 level (%.2f dB) and reaches the same exact dry Off %s'%(ratio,stereo))
 del old,new;gc.collect()
# Measure both builds with identical input/settling, including their gate/EQ
# wrapper. Counts are instructions, not physical cycles or whole-device CPU %.
records=[]
for stereo in (False,True):
 for mode in (0,1,3,4):
  counts=[]
  for candidate in (False,True):
   q=gsetup(stereo=stereo)[0] if candidate else old_setup(stereo)
   for b in range(300):gblock(q,signal(.2,b),signal(.1,b,719) if stereo else None,[(0x1ba,mode),(0x1bb,750)] if b==0 else [])
   count=[0]
   hook=q.hook_add(UC_HOOK_CODE,lambda a,b,c,d:count.__setitem__(0,count[0]+1));q.ctl_remove_cache(0x8000000,0x8200000)
   gblock(q,signal(.2,300),signal(.1,300,719) if stereo else None);q.hook_del(hook)
   counts.append(count[0]);del q;gc.collect()
  before,after=counts
  records.append(dict(stereo=stereo,mode=mode,I3_instructions=before,I5_instructions=after,reduction_percent=round(100*(before-after)/before,2)))
  check(after<=before+16+48 if mode==0 else after<before*1.3 if mode==4 else after<before*.9,'matched native callback improves steady active cost '+str((stereo,mode,before,after)))
(ROOT/'tests/insert-optimization.json').write_text(json.dumps(dict(records=records,exact_pcm=True,hardware_cpu_measured=False),indent=2)+'\n')
print(H.checks,'matched I3/current ARM audio/performance checks passed; physical CPU savings require matched hardware tests.')
