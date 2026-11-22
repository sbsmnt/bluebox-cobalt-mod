"""I4/I5 exact PCM and state for identical channels, real stereo, silence and
filter tails; matched per-case instruction counts at 35% and 75% Drive.
"""
from gate_fixture import *
import firmware_harness as H
import base64,json,gc
ref=json.loads((ROOT/'tests/references/cobalt-inserts-I4-manifest.json').read_text())
i4=bytearray(stock)
for p in ref['patches']:
 x=base64.b64decode(p['new']);i4[p['offset']:p['offset']+len(x)]=x
i4=bytes(i4)
check(hashlib.sha256(i4).hexdigest()=='341d1039d9e18360a30e3c32ef71cdb26e6b991429d0d887ceb0b37b32d9afff','matched reference is the hardware-tested I4 firmware')
def old_setup(stereo=True):
 q,seen,_=gsetup(image=i4,stereo=stereo);call(q,ref['symbols']['gate7']['gate_construct'],EQ,4)
 w32(q,EQ+0x18,0);q.mem_write(EQ+0x1e,b'\0\0');w32(q,EQ+8,NEXT);return q
cases=['independent','identical','right_silent','digital_silence','tail_then_silence']
def source(kind,b):
 l=signal(.2,b,719)
 return (ZERO[:],ZERO[:]) if kind=='digital_silence' else (l,l[:]) if kind=='identical' else (l,ZERO[:]) if kind=='right_silent' else (l,signal(.1,b,523))
def events(mode,b):
 return [(0x1ba,mode),(0x1bb,350)] if b==0 else [(0x1bd,430),(0x1be,-3000)] if b==170 else [(0x1bd,1000),(0x1be,0)] if b==320 else []
# S8f: WRAP is anti-aliased, so only CLIP and FOLD stay exact to I4. WRAP's
# shared/silent shortcuts are checked against its own mono path below.
for mode in (1,3):
 for kind in cases:
  old=old_setup();new,_,_=gsetup();equal=True
  for b in range(420):
   ev=events(mode,b)
   l,r=source(kind,b)
   if kind=='tail_then_silence' and b>=140:l=ZERO[:];r=ZERO[:]
   # Break equality at the last sample; histories must then remain separate
   # even when the following block's samples are identical again.
   if kind=='identical' and b==220:r[-1]+=0.00001
   n=(1,7,15,31)[b%4] if 360<=b<380 else 32
   a,ar=gblock(old,l,r,ev,frames=n);c,cr=gblock(new,l,r,ev,frames=n)
   equal &= a==c and ar==cr==7 and bytes(old.mem_read(EQ+0x1d0,88))==bytes(new.mem_read(EQ+0x1d0,88))
   if not equal:raise AssertionError('PCM/state mismatch '+str((mode,kind,b,n)))
  check(equal,'exact I4 PCM/all 88 state bytes for shared/silent/tail/stereo cases '+str((mode,kind)))
  del old,new;gc.collect()
# WRAP: every stereo case equals two separate mono runs of the same build,
# bit for bit, including frame-size changes; so the twin and silence
# shortcuts never mix the per-channel anti-aliasing history.
for kind in cases:
 st,_,_=gsetup();ml,_,_=gsetup(stereo=False);mr,_,_=gsetup(stereo=False);equal=True
 for b in range(420):
  l,r=source(kind,b)
  if kind=='tail_then_silence' and b>=140:l=ZERO[:];r=ZERO[:]
  if kind=='identical' and b==220:r[-1]+=0.00001
  n=(1,7,15,31)[b%4] if 360<=b<380 else 32
  ev=events(4,b)
  c,_=gblock(st,l,r,ev,frames=n);a,_=gblock(ml,l,None,ev,frames=n);d,_=gblock(mr,r,None,ev,frames=n)
  equal &= c[:4*n]==a[:4*n] and c[256-128:256-128+4*n]==d[:4*n]
  if not equal:raise AssertionError('WRAP stereo/mono mismatch '+str((kind,b,n)))
 check(equal,'WRAP stereo equals two independent mono channels for '+kind)
 del st,ml,mr;gc.collect()
# Signed-zero and nonfinite input must not be misclassified as positive
# digital silence. Execute the actual O2 sample functions for exact bits.
SBUF=0x24040000;L=SBUF+0x200;R=SBUF+0x400
for mode in (1,3):
 qs=[old_setup(),gsetup()[0]]
 for q in qs:
  for b in range(200):gblock(q,ZERO[:],ZERO[:],[(0x1ba,mode)] if b==0 else [])
 bits=struct.pack('<32I',*([0x80000000,0x7fc12345,0x7f800000,0xff800000]*8))
 for q,hot in zip(qs,[ref['symbols']['gate7']['insert_drive_audio'],GT['insert_drive_audio']]):
  q.mem_write(L,bits);q.mem_write(R,bits);call(q,hot,EQ+0x1d0,L,R,32)
 check(bytes(qs[0].mem_read(L,128))+bytes(qs[0].mem_read(R,128))==bytes(qs[1].mem_read(L,128))+bytes(qs[1].mem_read(R,128)),'shared optimization preserves active signed-zero/nonfinite handling '+str(mode))
 del qs;gc.collect()
q=gsetup()[0]
for b in range(200):gblock(q,ZERO[:],ZERO[:],[(0x1ba,4)] if b==0 else [])
q.mem_write(L,bits);q.mem_write(R,bits);call(q,GT['insert_drive_audio'],EQ+0x1d0,L,R,32)
out=struct.unpack('<64f',bytes(q.mem_read(L,128))+bytes(q.mem_read(R,128)))
check(all(math.isfinite(x) and abs(x)<1 for x in out),'WRAP keeps signed-zero/nonfinite input finite and bounded')
del q;gc.collect()
records=[]
for drive in (350,750):
 for kind in ('independent','identical','right_silent','digital_silence'):
  for mode in (1,3,4):
   counts=[]
   for candidate in (False,True):
    q=gsetup()[0] if candidate else old_setup()
    for b in range(220):
     l,r=source(kind,b);gblock(q,l,r,[(0x1ba,mode),(0x1bb,drive)] if b==0 else [])
    counter=[0]
    h=q.hook_add(UC_HOOK_CODE,lambda a,b,c,d:counter.__setitem__(0,counter[0]+1));q.ctl_remove_cache(0x8000000,0x8200000)
    l,r=source(kind,220);gblock(q,l,r);q.hook_del(h);counts.append(counter[0]);del q;gc.collect()
   before,after=counts
   records.append(dict(drive=drive/10,case=kind,mode=mode,I4_instructions=before,I5_instructions=after,reduction_percent=round(100*(before-after)/before,2)))
   check((after<before if kind!='independent' else after<=before*1.3) if mode==4 else after<before*.85 if kind!='independent' else after<=before*1.03,'matched instruction cost bounds for exact shared and real stereo '+str((drive,kind,mode,before,after)))
(ROOT/'tests/insert-sharing.json').write_text(json.dumps(dict(records=records,exact_PCM_and_state=True,hardware_cpu_measured=False),indent=2)+'\n')
print(H.checks,'shared-channel/silence/exact-audio/per-case instruction checks passed; hardware CPU savings remain unmeasured.')
