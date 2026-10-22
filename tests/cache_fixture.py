"""Real native cache constructor, allocator, record writer and sample copying.
Full SD/DMA scheduling is not emulated; page availability is explicitly seeded.
"""
from firmware_harness import *
CACHE=0x24010000;INL=0x24020000;INR=0x24020200;SID=5
def init(image=patched,mode='missing',page=0,cpu_model=UC_CPU_ARM_CORTEX_M7):
 u=machine(image,cpu_model=cpu_model);u.mem_map(0xc2000000,0x2000000)
 w32(u,0x24000000,0xc3400000);w32(u,0x24000004,0xc0000000)
 call(u,0x805a79c,CACHE);call(u,0x805a888,CACHE)
 u.mem_write(CACHE+0x2708+SID*2,struct.pack('<H',0))
 u.mem_write(INL,struct.pack('<32f',*[.1+i*.01 for i in range(32)]))
 u.mem_write(INR,struct.pack('<32f',*[-.2-i*.01 for i in range(32)]))
 if mode=='hit':
  u.mem_write(CACHE+0x14,struct.pack('<HHBB',SID,1,1,0));w32(u,CACHE+8,page)
  table=u32(u,CACHE+0x352c);index=page+0x400 if page<=31 else page&0x3ff
  u.mem_write(table+index*2,struct.pack('<H',0))
 if mode=='range':u.mem_write(CACHE+0x1b0a,b'\1');w32(u,CACHE+0x1b2c,page)
 if mode=='invalid':
  u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:q.mem_write(CACHE+0x2708+SID*2,b'\xff\xff'),begin=0x805a486,end=0x805a486)
 return u
def run(u,page=0,frames=32,count=False,stream=SID):
 sp=0x2407f000;pos=page*0x9800
 values=[0x12340000+i for i in range(8)]
 for reg,val in zip([UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11],values):u.reg_write(reg,val)
 for reg,val in [(UC_ARM_REG_R0,CACHE),(UC_ARM_REG_R1,0),(UC_ARM_REG_R2,pos&0xffffffff),(UC_ARM_REG_R3,pos>>32),(UC_ARM_REG_SP,sp),(UC_ARM_REG_LR,RET|1)]:u.reg_write(reg,val)
 u.mem_write(sp,struct.pack('<4I',stream,INL,INR,frames));instructions=[0];h=None
 if count:h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:instructions.__setitem__(0,instructions[0]+1))
 u.emu_start(0x805a445,RET,count=1000000)
 if h:u.hook_del(h)
 assert u.reg_read(UC_ARM_REG_PC)==RET
 after=[u.reg_read(reg) for reg in [UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11]]
 return {'result':u.reg_read(UC_ARM_REG_R0),'C':u32(u,CACHE+0x362c),'instructions':instructions[0],'registers_ok':after==values and u.reg_read(UC_ARM_REG_SP)==sp}
