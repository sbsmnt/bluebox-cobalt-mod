"""Native desktop reverb regression and actual delayed-send reverb response.
Maps ITCM used by the stock fast delay pool. Two unsupported stock float-to-double comparison sequences are modeled identically for stock and patched.
"""
from firmware_harness import *
import firmware_harness as H
REV=0xc0000000;PORT=14;QID=21;QUEUE=CTX+QID*0x604+4;BUS=CTX+0x150e4+PORT*20;DS=0x38800d00

def rsetup(image=patched):
 u=machine(image);u.mem_map(0,0x10000)
 def double_compare(u,a,n,_):
  value=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S14)))[0]
  threshold=struct.unpack('<d',struct.pack('<Q',u.reg_read(UC_ARM_REG_D5)))[0]
  flags=3 if math.isnan(value) or math.isnan(threshold) else 2 if value>threshold else 6 if value==threshold else 8
  u.reg_write(UC_ARM_REG_D7,struct.unpack('<Q',struct.pack('<d',value))[0]);u.reg_write(UC_ARM_REG_FPSCR,(u.reg_read(UC_ARM_REG_FPSCR)&0xfffffff)|(flags<<28));u.reg_write(UC_ARM_REG_PC,(a+8)|1)
 for a in [0x8049e54,0x8049ee2]:u.hook_add(UC_HOOK_CODE,double_compare,begin=a,end=a)
 call(u,0x80404d4,0x8089ee8,0x24000000,0x240001e8)
 for a in [0x8053198,0x8055620,0x8058b1c,0x80596b0]:call(u,a)
 heap=[0xc0010000];nodes=[]
 def alloc(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x80412a4,alloc)
 u.reg_write(UC_ARM_REG_R0,REV);u.reg_write(UC_ARM_REG_R1,48000);u.reg_write(UC_ARM_REG_R2,32);u.reg_write(UC_ARM_REG_R3,0)
 u.reg_write(UC_ARM_REG_SP,0x2407f000);u.reg_write(UC_ARM_REG_LR,RET|1);u.emu_start(0x8046e75,RET,count=50000000)
 assert u.reg_read(UC_ARM_REG_PC)==RET
 w32(u,REV+0x18,QID);u.mem_write(REV+0x1e,struct.pack('<H',PORT));u.mem_write(REV+0x980,b'\0')
 w32(u,REV+8,NEXT);w32(u,NEXT,VT);w32(u,VT+12,NEXT_FN|1)
 def nextnode(u):nodes.append(u32(u,REV+8));u.reg_write(UC_ARM_REG_R0,7)
 stub(u,NEXT_FN,nextnode)
 return u,nodes,heap[0]
def rblock(u,l,r=None,evs=(),silent=0):
 for i,(id,v) in enumerate(evs):u.mem_write(QUEUE+i*24,struct.pack('<B7xIHHiI',0x39,QID,id,0,v,0))
 w32(u,QUEUE+0x600,len(evs))
 u.mem_write(BUS,struct.pack('<IIIIBBxx',32,32,BUF,BUF+0x100,silent,1))
 u.mem_write(BUF,struct.pack('<32f',*l));u.mem_write(BUF+0x100,struct.pack('<32f',*(l if r is None else r)))
 ret=call(u,u32(u,u32(u,REV)+12),REV,CTX)
 return bytes(u.mem_read(BUF,128))+bytes(u.mem_read(BUF+0x100,128)),ret
