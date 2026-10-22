"""Real native EQ channel constructor/body/queue; allocator and final sink only stubbed."""
from firmware_harness import *
import firmware_harness as H
NGBASE=0x38800400;NGTRACK=NGBASE+4;NGSTRIDE=40
EQ=0xc0000000;GT=M['symbols']['gate7'];EQSOURCE=0x806a2a8

def gsetup(image=patched,ch=0,stereo=True,chain=True):
 u=machine(image,double=True);heap=[0xc0010000];seen=[]
 def alloc(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x80412a4,alloc)
 call(u,GT['gate_construct'] if image==patched else 0x8042e60,EQ,4)
 w32(u,EQ+0x18,ch);u.mem_write(EQ+0x1e,struct.pack('<H',ch))
 if chain:
  w32(u,EQ+8,NEXT);w32(u,NEXT,VT);w32(u,VT+12,NEXT_FN|1)
  def sink(u):seen.append(bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)));u.reg_write(UC_ARM_REG_R0,7)
  stub(u,NEXT_FN,sink)
 return u,seen,heap[0]
def gblock(u,l,r=None,evs=(),ch=0,frames=32,silent=False,foreign=None):
 q=CTX+ch*0x604+4
 for i,(pid,v) in enumerate(evs):u.mem_write(q+i*24,struct.pack('<B7xIHHiI',0x39,ch if foreign is None else foreign,pid,0,v,0))
 w32(u,q+0x600,len(evs))
 bus=CTX+0x150e4+ch*20
 u.mem_write(bus,struct.pack('<IIIIBBxx',32 if frames else 0,frames,BUF,BUF+0x100,int(silent),int(r is not None)))
 u.mem_write(BUF,struct.pack('<32f',*l));u.mem_write(BUF+0x100,struct.pack('<32f',*(ZERO if r is None else r)))
 ret=call(u,u32(u,u32(u,EQ)+12),EQ,CTX)
 return bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)),ret
ZERO=[0.]*32
