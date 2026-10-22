"""Actual native channel EQ constructor/process, frame/queue behavior and adaptive gate."""
from gate_fixture import *
for ch in range(12):
 a,an,_=gsetup(stock,ch=ch);b,bn,heap=gsetup(ch=ch);same=True
 for n in range(80):
  ev=[(0x12e,n%2)] if n%20==0 else []
  oa,ra=gblock(a,signal(.17,n),signal(.031,n,701),ev,ch=ch)
  ob,rb=gblock(b,signal(.17,n),signal(.031,n,701),ev,ch=ch)
  same &= oa==ob and ra==rb and a.mem_read(EQ,0x1cc)==b.mem_read(EQ,0x1cc)
 check(same,'Gate Off retains native channel EQ PCM/state with EQ bypass changes, track'+str(ch+1))
 check(len(bn)==80 and u32(b,0x38800400+4+40*ch)==EQ,'one downstream call and correct independent state slot, track'+str(ch+1))
 for n in range(300):out,ret=gblock(b,signal(.0001,n),signal(.0002,n),[(0x1ae,-40000)] if n==0 else [],ch=ch)
 check(out==bytes(256) and ret==7,'queued threshold closes quiet native stereo track'+str(ch+1))
 out,_=gblock(b,signal(.1,400),signal(.01,400),ch=ch)
 vals=struct.unpack('<64f',out)
 check(abs(vals[31])>.01 and abs(vals[63])>.001,'one queued track opens both stereo sides, track'+str(ch+1))
 check(u32(b,0x38800400+4+40*ch+4)==((-40000)&0xffffffff),'native per-track event reaches saved DSP threshold, track'+str(ch+1))
 # New gate does not allocate when enabling/processing.
 check(b.mem_read(EQ+8,4)==struct.pack('<I',NEXT),'gate leaves native graph pointer intact, track'+str(ch+1))
# Main uses the same EQ class; it must bypass the gate even for a stray event.
a,_,_=gsetup(stock,ch=12);b,_,_=gsetup(ch=12);same=True
for n in range(300):
 ev=[(0x1ae,-12000)] if n==0 else []
 oa,_=gblock(a,signal(.001,n),signal(.002,n),ev,ch=12);ob,_=gblock(b,signal(.001,n),signal(.002,n),ev,ch=12);same &= oa==ob
check(same,'Main EQ remains stock even when given the new per-track parameter')
# Neither wrong-target events nor another channel's setting can control track0.
u,_,_=gsetup()
for n in range(200):out,_=gblock(u,signal(.0001,n),evs=[(0x1ae,-40000)] if n==0 else [],foreign=1)
check(f32(u,NGTRACK+20)==1.0 and struct.unpack('<i',u.mem_read(NGTRACK+4,4))[0]==-96000,'foreign target event cannot enable a track gate')
# Mono buffers are not expanded or written through a phantom right pointer.
u,seen,_=gsetup()
for n in range(300):out,_=gblock(u,signal(.0001,n),evs=[(0x1ae,-40000)] if n==0 else [])
check(out==bytes(256),'mono quiet input closes without forcing a stereo bus')
out,_=gblock(u,signal(.1,50))
check(any(out[:128]) and out[128:]==bytes(128),'mono signal reopens while untouched right storage stays zero')
# Native empty/short-buffer semantics retain inactive samples and chain count.
for frames in [0,1,4,17,31]:
 u,nodes,_=gsetup();gblock(u,ZERO,evs=[(0x1ae,-40000)])
 source=signal(.1,3);raw=struct.pack('<32f',*source)
 out,ret=gblock(u,source,source,frames=frames)
 check(out[frames*4:128]==raw[frames*4:] and out[128+frames*4:]==raw[frames*4:] and ret==7,'inactive output suffix preserved for native block '+str(frames))
# Constructor lifecycle explicitly retires stale backup SRAM state for a reused
# EQ object, including identical allocation addresses after restarting audio.
u,_,_=gsetup()
for n in range(200):gblock(u,ZERO,evs=[(0x1ae,-40000)] if n==0 else [])
call(u,GT['gate_construct'],EQ,4)
check(struct.unpack('<i',u.mem_read(NGTRACK+4,4))[0]==-96000 and f32(u,NGTRACK+20)==1.0,'reconstructing an EQ owner resets stale gate to Off/unity')
# Upper/lower malformed values stay in the safe control domain.
u,_,_=gsetup()
for raw,expected in [(100000,-12000),(-200000,-96000)]:
 gblock(u,ZERO,evs=[(0x1ae,raw)])
 check(struct.unpack('<i',u.mem_read(NGTRACK+4,4))[0]==expected,'native malformed gate threshold clamps '+str(raw))
# Representative full native callback instruction counts, not M7 cycles.
for label,level,threshold in [('Off',.1,-96000),('open',.1,-40000),('closed',.00001,-40000)]:
 u,nodes,_=gsetup()
 for n in range(300):gblock(u,signal(level,n),signal(level*.3,n),[(0x1ae,threshold)] if n==0 else [])
 count=[0];h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:count.__setitem__(0,count[0]+1));gblock(u,signal(level,300),signal(level*.3,300));u.hook_del(h)
 print('INFO native32-frame channel EQ+gate instructions',label,count[0])
 check(count[0]<16000,'bounded complete channel callback, '+label)
print(str(H.checks)+' v1.0.0-alpha.4 native gate checks passed; ADC/playback/SD/real CPU headroom require hardware tests.')
