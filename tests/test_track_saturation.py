"""Per-track saturation on the real native channel EQ node, ahead of the gate.
Real native EQ constructor/body/queue; allocator and final sink only stubbed."""
from gate_fixture import *
TS=0x38800c00;TSTRACK=TS+4;TSSTRIDE=16;TID=0x1ba;CID=0x1bb;TSYM=M['symbols']['trsat7']
def curve(x):return 1. if x>3 else -1. if x< -3 else x*(27+x*x)/(27+9*x*x)
def shape(x,c):m=c/1000;return (1-m)*curve(x)+m*max(-1.,min(1.,x*(1+3*m)))
def reference(x,drive,c=0):
 d=max(0,min(1000,drive))/1000;g=2**(6*d);mix=min(d*20,1);k=.2/shape(g*.2,c)
 return x+mix*(shape(x*g,c)*k-x)
def drive_of(u,ch=0):return struct.unpack('<h',u.mem_read(TSTRACK+ch*TSSTRIDE+4,2))[0]
def curve_of(u,ch=0):return struct.unpack('<h',u.mem_read(TSTRACK+ch*TSSTRIDE+6,2))[0]
def sink_sees(seen):return struct.unpack('<64f',seen[-1])
check(u32(machine(),EQSOURCE+12)==TSYM['tsat_process']|1,'channel EQ process pointer enters track saturation first')
check(all(b==0 for b in stock[0x80c8000-0x8040000:0x80d0000-0x8040000]),'stock flash 0x080c8000..0x080cffff is an empty gap')
for ch in range(12):
 a,an,_=gsetup(stock,ch=ch);b,bn,_=gsetup(ch=ch);same=True
 for n in range(60):
  ev=[(0x12e,n%2)] if n%20==0 else []
  oa,ra=gblock(a,signal(.4,n),signal(.2,n,701),ev,ch=ch);ob,rb=gblock(b,signal(.4,n),signal(.2,n,701),ev,ch=ch)
  same&=oa==ob and ra==rb and a.mem_read(EQ,0x1cc)==b.mem_read(EQ,0x1cc)
 check(same,'Sat Off (and gate Off) keeps native channel EQ PCM/state, track'+str(ch+1))
 for n in range(3):gblock(b,[.8]*32,[-.3]*32,[(TID,700)] if n==0 else [],ch=ch)
 vals=sink_sees(bn)
 check(abs(vals[31]-reference(.8,700))<6e-5 and abs(vals[63]-reference(-.3,700))<6e-5,'queued drive saturates both sides before the native EQ, track'+str(ch+1))
 check(u32(b,TSTRACK+ch*TSSTRIDE)==EQ and drive_of(b,ch)==700 and curve_of(b,ch)==0,'per-track state slot owned and set (Soft curve), track'+str(ch+1))
 c=200+60*ch
 for n in range(3):gblock(b,[.8]*32,[-.05]*32,[(CID,c)] if n==0 else [],ch=ch)
 vals=sink_sees(bn)
 check(curve_of(b,ch)==c and abs(vals[31]-reference(.8,700,c))<6e-5 and abs(vals[63]-reference(-.05,700,c))<6e-5,'queued curve morphs towards hard clip before the native EQ, track'+str(ch+1))
# Drive back to 0 ramps out, then is bit-exact dry again.
u,seen,_=gsetup()
for n in range(3):gblock(u,[.7]*32,[-.7]*32,[(TID,1000)] if n==0 else [])
gblock(u,[.7]*32,[-.7]*32,[(TID,0)]);out,_=gblock(u,[.7]*32,[-.7]*32)
check(out==struct.pack('<64f',*([.7]*32+[-.7]*32)),'after ramp to zero audio is bit-exact dry input')
# Main (ch 12) is never saturated, even by a stray event.
a,_,_=gsetup(stock,ch=12);b,_,_=gsetup(ch=12);same=True
for n in range(40):
 ev=[(TID,1000)] if n==0 else []
 oa,_=gblock(a,signal(.5,n),signal(.5,n),ev,ch=12);ob,_=gblock(b,signal(.5,n),signal(.5,n),ev,ch=12);same&=oa==ob
check(same,'Main EQ remains stock even when given the per-track parameter')
u,_,_=gsetup()
for n in range(3):out,_=gblock(u,[.8]*32,[.8]*32,[(TID,1000)] if n==0 else [],foreign=1)
check(drive_of(u)==0 and out==struct.pack('<64f',*([.8]*64)),'foreign target event cannot saturate a track')
u,_,_=gsetup()
for n in range(3):out,_=gblock(u,[.8]*32,[.8]*32,[(CID,1000)] if n==0 else [])
check(curve_of(u)==1000 and out==struct.pack('<64f',*([.8]*64)),'a curve alone (drive 0) keeps the track bit-exact dry')
u,_,_=gsetup()
for n in range(3):gblock(u,[.8]*32,[.8]*32,[(CID,1000)] if n==0 else [],foreign=1)
check(curve_of(u)==0,'foreign target event cannot change a track curve')
# Mono: right storage stays untouched.
u,_,_=gsetup()
for n in range(3):out,_=gblock(u,[.8]*32,evs=[(TID,500)] if n==0 else [])
check(abs(struct.unpack('<f',out[:4])[0]-reference(.8,500))<6e-5 and out[128:]==bytes(128),'mono track saturates left only')
# Silent and short buses keep native semantics.
u,_,_=gsetup();gblock(u,ZERO,evs=[(TID,1000)])
src=signal(.5,3);raw=struct.pack('<32f',*src)
out,_=gblock(u,src,src,silent=True)
check(out==raw+raw,'silent bus is not written')
for frames in [1,4,17,31]:
 out,ret=gblock(u,src,src,frames=frames)
 check(out[frames*4:128]==raw[frames*4:] and out[128+frames*4:]==raw[frames*4:] and ret==7,'inactive suffix preserved for native block '+str(frames))
# Malformed values clamp.
u,_,_=gsetup()
for raw_v,expected in [(100000,1000),(-5,0)]:
 gblock(u,ZERO,evs=[(TID,raw_v)]);check(drive_of(u)==expected,'malformed drive clamps '+str(raw_v))
 gblock(u,ZERO,evs=[(CID,raw_v)]);check(curve_of(u)==expected,'malformed curve clamps '+str(raw_v))
# Reconstructing the EQ owner (same address) resets the drive to Off.
u,_,_=gsetup();gblock(u,ZERO,evs=[(TID,800),(CID,900)])
call(u,TSYM['tsat_construct'],EQ,4)
check(drive_of(u)==0 and curve_of(u)==0 and u32(u,NGTRACK+4)==(-96000)&0xffffffff,'reconstruct resets saturation (Soft curve) and gate to Off')
# Sat then gate: a saturated quiet-ish signal and the gate both act; order is sat -> gate.
u,seen,_=gsetup()
for n in range(300):out,_=gblock(u,signal(.0001,n),signal(.0001,n),[(TID,1000),(0x1ae,-40000)] if n==0 else [])
check(out==bytes(256),'gate still closes quiet input after saturation')
# Work per block (instructions, not M7 cycles): Off overhead is bounded, active is bounded.
for label,drive,c,limit in [('Sat Off',0,0,120),('Sat on',700,0,9000),('Sat on, half curve',700,500,10000)]:
 a,_,_=gsetup();b,_,_=gsetup()
 for u in (a,b):
  for n in range(3):gblock(u,signal(.3,n),signal(.3,n,700),[(TID,drive),(CID,c)] if n==0 and u is b else [])
 counts=[]
 for u in (a,b):
  if u is a:w32(u,EQSOURCE+12,GT['gate_process']|1)
  c=[0];h=u.hook_add(UC_HOOK_CODE,lambda q,x,n,d:c.__setitem__(0,c[0]+1));gblock(u,signal(.3,9),signal(.3,9,700));u.hook_del(h);counts.append(c[0])
 print('INFO channel callback instructions',label,'gate only',counts[0],'with track saturation',counts[1])
 check(counts[1]-counts[0]<=limit,label+': extra work per 32-frame stereo block within '+str(limit)+' instructions')
print(str(H.checks)+' per-track saturation checks passed; sound and real CPU headroom require hardware tests.')
