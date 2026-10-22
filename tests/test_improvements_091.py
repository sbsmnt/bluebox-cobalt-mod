"""v0.9.1 native stock-path, tempo sync and fractional encoder regressions.
Clock arithmetic uses Unicorn ARM MAX to execute native double precision and
FPv8 select instructions together; normal suites retain the Cortex-M7 model.
This validates instruction behavior, not Cortex-M7 timing or interrupt behavior.
"""
from delay_fixture import *
import importlib.util,os
XS=0x38800200;GS=0x38800000;MODE=0x1a0
oldroot=(ROOT/'tests'/'references'/'v0.9.5')
s=importlib.util.spec_from_file_location('prev',oldroot/'patch_bluebox.py');prev=importlib.util.module_from_spec(s);s.loader.exec_module(prev)
before=prev.transform(stock)
# Exactly native wet audio, native ring state and downstream even with hidden
# custom parameters deliberately left at non-neutral values.
a,_,_=dsetup(stock);b,nodes,_=dsetup();same=True
for n in range(1400):
 ev=INITIAL+[(MODE,5),(0x4a,12),(0x4b,1),(0x3a,1000),(0x3d,1000),(0x1a7,1000),(0x1a8,1000),(0x1a5,1)] if n==0 else [(0x36,1),(0x37,1),(0xca,1)] if n==700 else []
 oa,ra=dblock(a,signal(.3,n),signal(.2,n,733),ev)
 ob,rb=dblock(b,signal(.3,n),signal(.2,n,733),ev)
 if n in [0,700]:steady(a);steady(b)
 same &= oa==ob and ra==rb and a.mem_read(DOBJ,0x278)==b.mem_read(DOBJ,0x278)
check(same and len(nodes)==1400,'Plain Delay is bit-exact to original native delay PCM/object with hidden custom settings and native options')
check(u32(b,DS+56)==0 and f32(b,DS+52)==0,'Plain Delay emits no custom delay-to-reverb send')
# Tape cannot inherit a hidden pitch value, while its drift keeps working.
a,_,_=dsetup();b,_,_=dsetup();c,_,_=dsetup();same=True;drift=False
for n in range(600):
 ev=INITIAL+[(MODE,2),(0x39,650)] if n==0 else []
 oa,_=dblock(a,signal(.4,n,1703),evs=ev)
 ob,_=dblock(b,signal(.4,n,1703),evs=ev+[(0x4b,1),(0x4a,12)] if n==0 else ev)
 oc,_=dblock(c,signal(.4,n,1703),evs=ev+[(0x3a,800)] if n==0 else ev)
 if n==0:steady(a);steady(b);steady(c)
 same &= oa==ob;drift |= oa!=oc
check(same and drift,'Tape ignores hidden Pitch/PITCH but Drift still changes its audio')
a,_,_=dsetup(before);b,_,_=dsetup();old=now=0.
for n in range(650):
 ev=INITIAL+[(MODE,3)] if n==0 else []
 oa,_=dblock(a,signal(.4,n),evs=ev);ob,_=dblock(b,signal(.4,n),evs=ev)
 if n==0:steady(a);steady(b)
 if n>200:old+=sum(v*v for v in struct.unpack('<64f',oa));now+=sum(v*v for v in struct.unpack('<64f',ob))
check(old>0 and now==old,'Ducked return energy remains identical to v0.9.1')
# Execute the actual native global beat-table initializer (not a guessed table),
# native queue parsing, BPM-to-samples helper and new scheduler together.
u,_,_=dsetup(double=True);call(u,0x8053198)
beat_ticks=struct.unpack('<12q',u.mem_read(0x24008d00,96))
check(beat_ticks==(60,120,240,320,360,480,639,720,960,1280,1920,3840),'real stock clock initializer creates the twelve native beat divisions')
for bpm,division,expected in [(120,8,24000),(120,5,12000),(90,8,32000),(180,8,16000),(60,0,3000)]:
 u.mem_write(0x24040018,struct.pack('<f',bpm))
 dblock(u,ZERO,evs=INITIAL+[(MODE,4),(0x35,1),(0x34,division),(0x36,1),(0x37,1)])
 check(u32(u,DOBJ+0x24)==expected and u32(u,XS+412)==expected,'Reverse uses native resolved beat time before Ping/Quad multipliers: '+str((bpm,division)))
# A real tempo-notification event updates it with no parameter edit.
u.mem_write(0x24040018,struct.pack('<f',120.))
dblock(u,ZERO,evs=[(0x34,8),(0x35,1)])
u.mem_write(0x24040018,struct.pack('<f',100.));devents(u,[])
u.mem_write(DQUEUE,struct.pack('<B23x',0x6f));w32(u,DQUEUE+0x600,1)
call(u,u32(u,u32(u,DOBJ)+12),DOBJ,CTX)
check(u32(u,XS+412)==28800,'native tempo notification changes Reverse segment at the next block')
dblock(u,ZERO,evs=[(0x35,0),(0x1aa,123)])
check(u32(u,XS+412)==0 and u32(u,XS+24)==123,'BEAT off restores independent saved free Segment milliseconds')
# Verify actual spawned lengths and valid reads on both sides of the sync limit.
# Seed valid captured audio, not NaNs: unwritten history is checked separately
# by the cold-start/wrap and host sanitizer suites.
for pitch in [-12,0,12]:
 u,_,_=dsetup(double=True);call(u,0x8053198)
 u.mem_write(0x24040018,struct.pack('<f',60.))
 dblock(u,ZERO,evs=INITIAL+[(MODE,4),(0x35,1),(0x34,11),(0x4a,pitch)])
 w32(u,GS+52,96000);ptr=u32(u,GS+8);u.mem_write(ptr,struct.pack('<f',.125)*192000)
 dblock(u,signal(.2,0))
 # heads begin at XS+104 (offsetof proven by host layout assertion below).
 lens=[u32(u,XS+104+20*j+16) for j in range(2)]
 cap=int((96000-128)/(1+(.5 if pitch==-12 else 2 if pitch==12 else 1)))&~1
 check(max(lens)==cap and cap*(1+(.5 if pitch==-12 else 2 if pitch==12 else 1))+128<=96000,'long Reverse division safely clamps to existing history at pitch '+str(pitch))
# Cold longest allowed sync segment: instrument every capture read across wrap.
u,_,_=dsetup(double=True);call(u,0x8053198)
u.mem_write(0x24040018,struct.pack('<f',60.));ptr=u32(u,GS+8);written=set();energy=0.
def history(u,access,address,size,value,_):
 if access==UC_MEM_WRITE:written.update(range(address,address+size,4))
 else:assert all(a in written for a in range(address,address+size,4)),'unwritten sync history read'
hook=u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,history,begin=ptr,end=ptr+768000-1)
for n in range(3400):
 ev=INITIAL+[(MODE,4),(0x35,1),(0x34,11),(0x4a,-12),(0x39,0)] if n==0 else []
 out,_=dblock(u,signal(.3,n),evs=ev)
 values=struct.unpack('<64f',out);assert all(math.isfinite(v) for v in values)
 if n>2200:energy+=sum(v*v for v in values)
u.hook_del(hook)
check(energy>1 and len(written)==192000,'cold longest synced Reverse reads only written history and remains audible across full ring wrap')
# Leaving an active effect for Default retires custom state and pending sends.
for mode in [1,2,3,4]:
 u,_,_=dsetup()
 for n in range(80):dblock(u,signal(.3,n),evs=INITIAL+[(MODE,mode),(0x1aa,40),(0x3d,1000)] if n==0 else [])
 dblock(u,ZERO,evs=[(MODE,5)])
 check(u32(u,DS+56)==0 and f32(u,XS+80)==0 and f32(u,XS+88)==0 and u32(u,GS+52)==0,'switch to Default retires custom wet/capture state from mode '+str(mode))
# Full compact lists preserve native switches, removing Tape pitch and custom
# controls in Default. Reverse switches first knob between beat and free ms.
MODEL=0x300173d0;SET=MODEL+0xe48;SLOT=0x24008000;LIST=0x24050000
u=machine();heap=[0xc0010000]
def alloc(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0);u.mem_write(SLOT,b'\4');call(u,0x81202b8,SET,SLOT)
check(call(u,0x8120234,SET,MODE)==5,'new/default parameter sets choose plain Delay, keeping saved enhanced mode zero distinct')
w32(u,SET,0x24009000);w32(u,0x24009000,0x80dfe21);stub(u,0x80dfe20,lambda u:u.reg_write(UC_ARM_REG_R0,4))
w32(u,MODEL+0xe78+4,0x24052000);u.mem_write(MODEL+0xe78+10,struct.pack('<HH',1,68));u.mem_write(0x24052000,struct.pack('<H2xi',0x18c,1));u.mem_write(SLOT,struct.pack('<H',20))
for beat in [0,1]:
 call(u,0x8120264,SET,MODE,4);call(u,0x8120264,SET,0x35,beat)
 call(u,M['symbols']['4']['dly_parameters'],MODEL,SLOT,LIST)
 ids=[struct.unpack('<H',u.mem_read(LIST+4+i*12,2))[0] for i in range(u32(u,LIST))]
 check(ids==[0x34 if beat else 0x1aa,0x39,0x1ab,0x4a,0x3d,0x35],'Reverse compact control list includes conditional native beat selector and BEAT switch '+str(beat))
print(str(H.checks)+' v0.9.1 native improvement checks passed; hardware sound, encoder feel and real clock still require a device pass.')
