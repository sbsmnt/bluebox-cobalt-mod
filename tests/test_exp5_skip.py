"""exp.5: skip the native delay body while Granular is fully wet. Runs the real
desktop delay constructor, queue, body and rings against the pinned exp.4
release. Instruction counts are emulator measurements, not CPU percentages.
"""
from delay_fixture import *
import firmware_harness as H
import importlib.util,json
folder=ROOT/'tests/references/tape-exp4'
check(hashlib.sha256((folder/'patch_bluebox.py').read_bytes()).hexdigest()=='7988c0e1d88b438bfca803d6b8681bbd0bf8e71ba392aa9f61970310eee80a58','comparison patcher is the pinned exp.4 release')
spec=importlib.util.spec_from_file_location('exp4',folder/'patch_bluebox.py');p4=importlib.util.module_from_spec(spec);spec.loader.exec_module(p4)
ref=p4.transform(stock)
check(hashlib.sha256(ref).hexdigest()=='cd813d8d5596eea97f67dd6dc913c5a31f71dab78f1c490d41a0a37d80211198','exp.4 reference image rebuilt from stock')
GS=0x38800000;XS=0x38800200;RC=0x38800120;BODY=0x805287c
GMODE=0x1a0;GSIZE=0x1a2;GDENS=0x1a3
def lines(u):return [(u32(u,DOBJ+0x30+i*0x1c),u32(u,DOBJ+0x34+i*0x1c),u32(u,DOBJ+0x44+i*0x1c)) for i in range(6)]
def rings_zero(u):
 for buf,wr,size in lines(u):
  data=bytes(u.mem_read(buf,size*4))
  if data.count(0)!=len(data):return False
 return True
def body_work(u,run):
 inside=[0,0,0];ret=[]
 def h(q,a,n,d):
  if a==BODY and not inside[1]:inside[1]=1;ret.append(q.reg_read(UC_ARM_REG_LR)&~1);inside[2]+=1
  if inside[1]:inside[0]+=1
  if inside[1] and ret and a==ret[-1]:inside[1]=0;ret.pop()
 hk=u.hook_add(UC_HOOK_CODE,h);run();u.hook_del(hk)
 return inside[0],inside[2]
def ready(img=None,evs=INITIAL):
 """Apply the delay time first, then seed settled read heads (as test_delay.py)."""
 u=dsetup(img)[0] if img is not None else dsetup()[0]
 dblock(u,ZERO,evs=evs);steady(u);return u
def pair():return ready(ref),ready()
GRAN=[(GMODE,1),(GDENS,4),(GSIZE,80),(0x39,700)]

# 1. Delay Normal never skips: PCM, returns and native object exactly as exp.4.
a,b=pair();same=True
for n in range(1200):
 ev=[] if n==0 else [(0x39,(n//200)*150),(0x3a,(n//300)*200),(0x36,(n//400)%2)] if n%200==0 else []
 oa,ra=dblock(a,signal(.3,n,480),signal(.2,n,733),ev);ob,rb=dblock(b,signal(.3,n,480),signal(.2,n,733),ev)
 same&=oa==ob and ra==rb and a.mem_read(DOBJ,0x278)==b.mem_read(DOBJ,0x278)
check(same,'Delay Normal is bit-identical to exp.4: PCM, return value and native delay object')

# 2. Granular fully wet: identical PCM/send, while the native body takes its idle exit.
from delay_fixture import makeup_ref
a,b=pair();same=True;compared=0
for n in range(1400):
 ev=GRAN if n==0 else []
 oa,ra=dblock(a,signal(.3,n,480),signal(.2,n,733),ev);ob,rb=dblock(b,signal(.3,n,480),signal(.2,n,733),ev)
 mapped,_=makeup_ref(b,oa)
 if mapped is not None:same&=mapped==ob;compared+=1
 same&=ra==rb and a.mem_read(GS,168)==b.mem_read(GS,168)
check(same and compared>1380,'fully wet Granular PCM equals exp.4 x the exp.6 +3 dB makeup exactly; return value and grain state bit-identical')
w4,_=body_work(a,lambda:dblock(a,signal(.3,1400,480)))
w5,calls=body_work(b,lambda:dblock(b,signal(.3,1400,480)))
print('INFO native body instructions per block, Granular: exp.4',w4,'exp.5',w5)
check(calls==1 and w5<w4/4,'native body still runs once per block but takes its idle exit while Granular is fully wet')
before=lines(b);dblock(b,signal(.3,1401,480))
check(lines(b)==before,'skipped native rings do not advance')
gate=bytes(b.mem_read(DOBJ+0x124,0x1c));flag=b.mem_read(DOBJ+0x80,1);bus=bytes(b.mem_read(DBUS,20))
dblock(b,signal(.3,1402,480))
check(bytes(b.mem_read(DOBJ+0x124,0x1c))==gate and b.mem_read(DOBJ+0x80,1)==flag and bytes(b.mem_read(DBUS,16))==bus[:16],'input-gate tracking, line flag and bus header are restored after the events-only call')
check(rings_zero(b),'native rings are fully cleared while Granular stays fully wet')
check(u32(b,RC+8)==0 and u32(b,RC+16)==1,'ring clearing finishes and stays idle once clean')

# 3. Events during the skip still reach the native object: compare against
# exp.4 using only parameter state. DSP-state offsets are learned from a run of
# event-free Delay blocks (they change with audio alone).
probe=ready()
dsp=set()
for n in range(1,600):
 if n==300:dblock(probe,signal(.3,n),evs=[(0xca,1),(0x36,1),(0x37,1)])  # filter and PING/QUAD histories too
 x=bytes(probe.mem_read(DOBJ,0x278));dblock(probe,signal(.3*(n%7)/7,n,300+n));y=bytes(probe.mem_read(DOBJ,0x278))
 dsp|={i for i in range(0x278) if x[i]!=y[i]}
dsp|=set(range(0x30,0x30+6*0x1c))   # ring read/write/glide descriptors
a,b=pair()
for n in range(200):dblock(a,signal(.3,n),evs=GRAN if n==0 else []);dblock(b,signal(.3,n),evs=GRAN if n==0 else [])
edits=[(0x33,40),(0x39,350),(0x3d,800),(0xca,1),(0x36,1),(0x37,1),(0x3a,600),(0x4a,5),(0x4b,1),('off',0),('on',0)]
for pid,v in edits:
 dblock(a,ZERO,evs=[(pid,v)]);dblock(b,ZERO,evs=[(pid,v)])
oa=bytes(a.mem_read(DOBJ,0x278));ob=bytes(b.mem_read(DOBJ,0x278))
diff=[hex(i) for i in range(0x278) if oa[i]!=ob[i] and i not in dsp]
print('INFO parameter-state offsets differing from exp.4 after edits during Granular:',diff)
check(not diff,'time, feedback, send, filter, PING, QUAD, drift, pitch and on/off edits during Granular update the native object as in exp.4')
check(bytes(a.mem_read(0x38800d00,16))==bytes(b.mem_read(0x38800d00,16)) and a.mem_read(XS,64)==b.mem_read(XS,64),'wrapper send/drift and delay-type state match exp.4')

# 4. Tails: loud Delay history, long Granular, then Delay with silence: no
# stale echoes. A fresh impulse then echoes like a delay that never left.
def tail_case(granular_blocks):
 u=ready(evs=INITIAL+[(0x39,800)])
 for n in range(400):dblock(u,signal(.6,n,480))
 for n in range(granular_blocks):dblock(u,ZERO,evs=GRAN if n==0 else [])
 peak=0.
 for n in range(600):
  o,_=dblock(u,ZERO,evs=[(GMODE,0)] if n==0 else [])
  if n>=10:peak=max(peak,max(map(abs,struct.unpack('<64f',o))))
 return u,peak
delay_len=u32(ready(evs=INITIAL+[(0x39,800)]),DOBJ+0x40)
print('INFO fixture delay length',delay_len,'samples')
check(0<delay_len<600*32-320,'tail window spans more than one full delay period')
ref_u=ready(ref,INITIAL+[(0x39,800)])
for n in range(400):dblock(ref_u,signal(.6,n,480))
for n in range(900):dblock(ref_u,signal(.6,n,480),evs=GRAN if n==0 else [])
ref_peak=0.
for n in range(600):
 o,_=dblock(ref_u,ZERO,evs=[(GMODE,0)] if n==0 else [])
 if n>=10:ref_peak=max(ref_peak,max(map(abs,struct.unpack('<64f',o))))
print('INFO exp.4 echo level after returning from Granular with input playing:',ref_peak)
check(ref_peak>.01,'the same window carries audible delay echoes in exp.4, so silence below is meaningful')
u,peak=tail_case(900)
check(peak==0.,'after a long Granular passage, Delay resumes from cleared rings: no stale echoes')
for early in (30,60,120):
 u,peak=tail_case(early)
 print('INFO early return after',early,'Granular blocks: largest stale output',peak)
 check(peak==0.,f'returning to Delay {early} blocks after selecting Granular replays no stale echoes')
def echo_after(u,offset):
 out=[]
 for n in range(260):
  x=[0.]*32
  if n==0:x[offset]=.8
  o,_=dblock(u,x);out.extend(struct.unpack('<32f',o[:128]))
 return out
fresh=ready(evs=INITIAL+[(0x39,800)])
for n in range(400):dblock(fresh,ZERO)
u,_=tail_case(900)
x=echo_after(fresh,3);y=echo_after(u,3)
lead=lambda s:next((i for i,v in enumerate(s) if abs(v)>1e-4),None)
print('INFO first echo sample: never-Granular',lead(x),'after Granular',lead(y))
check(lead(x) is not None and lead(x)==lead(y),'a new note after returning echoes at the same time as a delay that never left')

# 5. Clearing never writes outside the six rings.
u=ready()
for n in range(50):dblock(u,signal(.4,n))
sym=sorted((v,k) for k,v in M['symbols']['delay7'].items())
start=M['symbols']['delay7']['rc_step_o2'];end=next(v for v,k in sym if v>start)
rings=[(buf,buf+size*4) for buf,_,size in lines(u)];bad=[0];seen=[0]
def mw(q,acc,addr,size,val,d):
 pc=q.reg_read(UC_ARM_REG_PC)
 if start<=(pc|1)<end or start<=pc<end:
  seen[0]+=1
  if not any(lo<=addr and addr+size<=hi for lo,hi in rings):bad[0]+=1
h=u.hook_add(UC_HOOK_MEM_WRITE,mw,begin=0xc0000000,end=0xc1ffffff)
for n in range(700):dblock(u,signal(.4,n),evs=GRAN if n==0 else [(GMODE,0)] if n==650 else [])
u.hook_del(h)
check(seen[0]>900000 and bad[0]==0,'ring clearing writes only inside the six native ring buffers (%d writes)'%seen[0])

# 6. Work per block. Peak during clearing must not exceed exp.4.
profiles=[]
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
for label,evs,blocks in (('Granular density 4, feedback 70%, after clearing',GRAN,1600),
                          ('Granular density 4, during clearing',GRAN,40),
                          ('Granular density 1, feedback 0, after clearing',[(GMODE,1),(GDENS,1),(GSIZE,250),(0x39,0)],1600),
                          ('Delay Normal',[],1600)):
 res=[]
 for img in (ref,patched):
  q=ready(img)
  for n in range(blocks):dblock(q,signal(.3,n),evs=evs if n==0 else [])
  res.append(work(q,lambda:dblock(q,signal(.3,blocks+1))))
 profiles.append(dict(case=label,exp4_instructions=res[0][0],exp5_instructions=res[1][0],exp4_vdiv=res[0][1],exp5_vdiv=res[1][1],instruction_reduction_percent=100*(1-res[1][0]/res[0][0])))
 print('INFO callback work',profiles[-1])
 if label=='Delay Normal':check(res[1][0]<=res[0][0]*1.02,label+': within 2% of exp.4 (wrapper, dispatch and exp.8 Tape/Reverse guards)')
 elif 'during' in label:check(res[1][0]<=res[0][0],label+': no more work than exp.4 while clearing')
 else:check(res[1][0]<res[0][0]*.85,label+': at least 15% fewer instructions than exp.4')
(ROOT/'tests/exp5-skip.json').write_text(json.dumps(dict(version=M['version'],baseline='1.0.0-tape.exp.4',profiles=profiles,hardware_cpu_measured=False),indent=2)+'\n')
print(H.checks,'exp.5 native-skip checks passed; device CPU and listening remain hardware tests.')
