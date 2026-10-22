"""Desktop native delay integration. No unsupported instructions are skipped.
Allocators and terminal graph/display callbacks are stubbed; desktop constructor,
parameter events, line reads, filter and feedback run as actual firmware code.
"""
from firmware_harness import *
import firmware_harness as H
DOBJ=0xc0000000;DQUEUE=CTX+0x14*0x604+4;DBUS=CTX+0x150e4+13*20
DS=0x38800d00;SYMS=M['symbols']['delay7']
def dsetup(image=patched,chain=True,double=False):
 u=machine(image,double=double);heap=[0xc0010000];nodes=[]
 def allocate(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;w32(u,0x24000004,heap[0]);u.reg_write(UC_ARM_REG_R0,a)
 w32(u,0x24000000,0xc2000000)
 w32(u,0x24000004,heap[0])
 stub(u,0x80412a4,allocate)
 u.reg_write(UC_ARM_REG_R0,DOBJ);u.reg_write(UC_ARM_REG_SP,0x2407f000);u.reg_write(UC_ARM_REG_LR,RET|1)
 try:u.emu_start(0x8050f1b,0x8050f1e,count=50000000)
 except UcError:
  print("ERROR constructor PC",hex(u.reg_read(UC_ARM_REG_PC)));raise
 assert u.reg_read(UC_ARM_REG_PC)==0x8050f1e,'native constructor did not return'
 w32(u,DOBJ+0x18,0x14);u.mem_write(DOBJ+0x1e,struct.pack('<H',13))
 # Actual graph builder 0x08050f52..0x08050f58 selects the wet-only mode.
 u.mem_write(DOBJ+0x273,b'\0')
 w32(u,CTX,0x24040000)
 for addr,buf in [(DBUS,BUF),(CTX+0x150e4+16*20,BUF+0x400),(CTX+0x15260,BUF+0x600),(CTX+0x15274,BUF+0x800)]:
  u.mem_write(addr,struct.pack('<IIIIBBxx',32,32,buf,buf+0x100,0,1))
 if chain:
  w32(u,DOBJ+8,NEXT);w32(u,NEXT,VT);w32(u,VT+12,NEXT_FN|1)
  def nextnode(u):nodes.append(u32(u,DOBJ+8));u.reg_write(UC_ARM_REG_R0,7)
  stub(u,NEXT_FN,nextnode)
 return u,nodes,heap[0]
def devents(u,evs):
 for i,(pid,v) in enumerate(evs):
  t=2 if pid=='on' else 3 if pid=='off' else 0x39;pid=0 if isinstance(pid,str) else pid
  u.mem_write(DQUEUE+i*24,struct.pack('<B7xIHHiI',t,0x14,pid,0,v,0))
 w32(u,DQUEUE+0x600,len(evs))
def dblock(u,l,r=None,evs=()):
 devents(u,evs)
 u.mem_write(DBUS,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,0,1))
 u.mem_write(BUF,struct.pack('<32f',*l));u.mem_write(BUF+0x100,struct.pack('<32f',*(l if r is None else r)))
 ret=call(u,u32(u,u32(u,DOBJ)+12),DOBJ,CTX)
 return bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)),ret
ZERO=[0.]*32
INITIAL=[('on',0),(0x35,0),(0x36,0),(0x33,10),(0x39,500),(0xca,0)]
# Snapshot real constructor state so multiple scenarios avoid re-zeroing 7 MB.
def steady(u):
 """Seed legal settled read heads instead of waiting 18k blocks for time glide."""
 for i in range(6):
  a=DOBJ+0x30+i*0x1c;wr=u32(u,a+4);length=u32(u,a+16);size=u32(u,a+20)
  w32(u,a+8,(wr+size-length)%size);u.mem_write(a+12,struct.pack('<f',0))

def freq(x):
 z=[i+x[i]/(x[i]-x[i+1]) for i in range(len(x)-1) if x[i]<=0<x[i+1]]
 d=sorted(b-a for a,b in zip(z,z[1:]));return 48000/d[len(d)//2]
def run(u,blocks,hz=480,amp=.5):
 l=[];r=[]
 for n in range(blocks):
  out,_=dblock(u,signal(amp,n,hz));v=struct.unpack('<64f',out);l.extend(v[:32]);r.extend(v[32:])
 return l,r

def granular_close(a,b,oa,ob,tol=2e-5):
 """exp.4 optimizes active-Granular arithmetic (see test_exp4_performance.py).
 Integer Granular state stays exact; PCM, send stash and float state may differ
 only by float rounding. exp.6's +3 dB wet makeup is mapped by makeup_ref;
 partial crossfade blocks skip PCM/stash. Returns (integer state equal,
 largest float difference)."""
 ga=bytes(a.mem_read(0x38800000,168));gb=bytes(b.mem_read(0x38800000,168))
 ints=list(range(0,72,4))+[88+20*g+o for g in range(4) for o in (12,16)]
 floats=[o for o in range(72,168,4) if o not in ints]
 same=all(ga[o:o+4]==gb[o:o+4] for o in ints)
 diff=0.
 for o in floats:diff=max(diff,abs(struct.unpack_from('<f',ga,o)[0]-struct.unpack_from('<f',gb,o)[0]))
 ref,scale=makeup_ref(b,oa)
 if ref is None:return same,diff
 diff=max([diff]+[abs(x-y) for x,y in zip(struct.unpack('<64f',ref),struct.unpack('<64f',ob))])
 da=struct.unpack('<87f',bytes(a.mem_read(0x38800d00,348)));db=struct.unpack('<87f',bytes(b.mem_read(0x38800d00,348)))
 da=[x*scale if 15<=i<79 else x for i,x in enumerate(da)]  # send stash = wet output x send
 diff=max([diff]+[abs(x-y) for x,y in zip(da,db) if x==x and y==y and abs(x)<1e30])
 return same,diff
G_MAKEUP=struct.unpack('<f',struct.pack('<f',1.41253754))[0]
_fade_seen={}
def makeup_ref(b,oa):
 """exp.6 scales the Granular wet output by G_MAKEUP (+3 dB) after its
 feedback tap. Map a pre-exp.6 reference block onto it: fully wet blocks
 (fade 1 before and after) scale by G_MAKEUP, Normal blocks (fade 0) are
 unchanged, and partial crossfade blocks return None (not comparable).
 Call once per block, after rendering b."""
 after=struct.unpack('<f',bytes(b.mem_read(0x38800048,4)))[0]
 before=_fade_seen.get(id(b),0.);_fade_seen[id(b)]=after
 if before==0. and after==0.:return oa,1.
 # exp.7 power-normalizes decorrelated grains (jitter/pitch): once its blend
 # (stored after the 168-byte state) is non-zero, PCM is not comparable.
 # Grain/feedback state remains compared exactly by the callers.
 if struct.unpack('<f',bytes(b.mem_read(0x388000a8,4)))[0]!=0.:return None,None
 if before==1. and after==1.:
  return struct.pack('<64f',*[struct.unpack('<f',struct.pack('<f',x*G_MAKEUP))[0] for x in struct.unpack('<64f',oa)]),G_MAKEUP
 return None,None
