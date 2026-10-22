"""Shared native acceleration is capped at 2x; slow precision stays native.
Actual history update, clock arithmetic and numeric-knob input execute.
"""
from firmware_harness import *
import firmware_harness as H
HISTORY=0x24008000;W=0x24010000;CLOCK=0x3001eb3c
reference=bytearray(patched)
reference[0xfb86e:0xfb874]=bytes.fromhex('0628a8bf0620')
reference=bytes(reference) # same parameter definitions, original acceleration
def accel(image,delta,dt,start=10000):
 u=machine(image)
 for i in range(8):call(u,0x813b7e8,HISTORY,delta&0xffffffff,(start+i*dt)&0xffffffff)
 return call(u,0x813b804,HISTORY)
for delta,dt in [(1,1000),(1,10),(20,20),(80,20),(160,10),(1000,1),(-80,20),(-1000,1),(0,0)]:
 old=accel(stock,delta,dt);new=accel(patched,delta,dt)
 check(new==min(old,2) and 1<=new<=2,'shared acceleration follows native speed with a 2x ceiling '+str((delta,dt)))
check(accel(stock,160,10)==6 and accel(patched,160,10)==2,'fast encoder motion drops from the native 6x multiplier to 2x')
check(accel(patched,80,20,0xfffffff0)<=2,'native clock rollover keeps bounded acceleration')
def knob(image,pid,slot,initial,fast):
 u=machine(image);heap=[0xc0010000]
 def alloc(q):a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
 u.mem_write(W+0x32,struct.pack('<H',slot));u.mem_write(W+0x3c9,b'\1')
 call(u,0x8127b84,W,pid,initial&0xffffffff,1)
 changes=[];stub(u,0x813a414,lambda q:changes.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
 values=[]
 for i in range(20):
  w32(u,CLOCK,10000+i*(10 if fast else 1000));call(u,0x8127d50,W,160)
  values.append(struct.unpack('<i',u.mem_read(W+0x98,4))[0])
 # A direction change must edit toward the lower value immediately.
 before=values[-1];w32(u,CLOCK,20001);call(u,0x8127d50,W,(-160)&0xffffffff)
 after=struct.unpack('<i',u.mem_read(W+0x98,4))[0]
 return values,after,before
for pid,slot,initial in [(4,0,-30000),(0x43,12,100),(0x33,20,100),(0x1af,21,100)]:
 slow_old,_,_=knob(reference,pid,slot,initial,False)
 slow_new,_,_=knob(patched,pid,slot,initial,False)
 check(slow_new==slow_old,'slow native numeric-knob precision is retained '+hex(pid))
 new,after,before=knob(patched,pid,slot,initial,True)
 check(all(a<=b for a,b in zip(new,new[1:])) and after<before,'accelerated native knob remains monotonic and reverses promptly '+hex(pid))
 old,_,_=knob(reference,pid,slot,initial,True)
 check(new[-1]-initial<old[-1]-initial,'rapid native turns produce smaller value jumps '+hex(pid))
u=machine();heap=[0xc0010000]
def alloc(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
stub(u,0x813a414,lambda q:None)
# exp.8: Gazer Rate/Hi C/Lo C move one logarithmic step (0.1% of their log
# range) per 40 slow units, the travel of a native 0..1000 control.
for pid,start,lo,hi in [(0x1b3,200,10,2000),(0x1b4,5500,500,16000),(0x1b5,120,20,2000)]:
 u.mem_write(W,bytes(0x3d0));u.mem_write(W+0x32,struct.pack('<H',21));u.mem_write(W+0x3c9,b'\1')
 call(u,0x8127b84,W,pid,start,1)
 ratio=(hi/lo)**.001
 for i in range(8):
  w32(u,CLOCK,30000+1000*i);before=u32(u,W+0x98);call(u,M['symbols']['4']['dly_turn'],W,40)
  after=u32(u,W+0x98)
  check(after>before and after<=round(before*ratio)+1,'Gazer slow step is one fine logarithmic step '+hex(pid))
 before=u32(u,W+0x98);w32(u,CLOCK,39000);call(u,M['symbols']['4']['dly_turn'],W,(-40)&0xffffffff)
 check(before>u32(u,W+0x98)>=before/ratio-1,'fine Gazer control reverses one step without a jump '+hex(pid))
 if pid==0x1b3:check(bytes(u.mem_read(W+0xd1,16)).split(b'\0')[0].startswith(b'0.20') and bytes(u.mem_read(W+0xd1,16)).split(b'\0')[0].endswith(b'Hz'),'Rate displays three decimals so fine moves are visible')
print(str(H.checks)+' shared encoder acceleration and numeric-knob precision checks passed; tactile feel remains a hardware test.')
