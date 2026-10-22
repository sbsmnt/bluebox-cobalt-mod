"""Native child input -> one shared model commit -> both pages -> M7 PCM.
Only allocation, filename/dirty-file handling, IPC transport and notification
fan-out boundaries are substituted. UI bubbling and model setter execute.
A parent capture does NOT commit edits: Gate must commit locally, exactly once.
"""
from gate_fixture import *
S=M['symbols']['4'];APP=0x300173d0;P=0x24010000;T=0x24030000
TMP=0x24008000;INFO=TMP+0x100;ROOTOBJ=0x24060000;RVT=ROOTOBJ+0x100
u=machine();heap=[0xc0010000]
def alloc(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc);call(u,0x8134cb8,0);call(u,0x8120abc)
for ch in range(12):
 u.mem_write(TMP,b'\1');call(u,0x81202b8,APP+0xc08+ch*0x30,TMP)
w32(u,APP+0xe78+4,0x24058000)
u.mem_write(APP+0xe78+10,struct.pack('<HH',1,68))
u.mem_write(0x24058000,struct.pack('<H2xi',0x18c,0))
stub(u,0x8121f4c,lambda u:u.reg_write(UC_ARM_REG_R0,0))
call(u,S['gate_mix_construct'],P);call(u,0x81323b8,T);call(u,0x8132ed4,T)
parents=[];commands=[];notifications=[]
w32(u,ROOTOBJ,RVT);w32(u,RVT+0x30,0x80dfe01)
for panel in (P,T):w32(u,panel+0x2c,ROOTOBJ)
def parent(u):
 parents.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24)));u.reg_write(UC_ARM_REG_R0,0)
parent_hook=stub(u,0x80dfe00,parent)
def audio(u):
 commands.append(tuple(u.reg_read(r) for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)))
stub(u,0x8108784,audio);stub(u,0x81247c8,lambda u:None)
notify_hook=stub(u,0x813c384,lambda u:notifications.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
clock=[100000]
def setup(quad,ch,value):
 call(u,0x8120264,APP+0xe78,0x18c,quad)
 call(u,0x8120264,APP+0xc08+ch*0x30,0x1ae,value&0xffffffff)
 call(u,S['gate_mix_setup'],P,1);call(u,S['gate_mix_select'],P,6)
 # Select track using the actual global notification, which binds both pages.
 u.mem_write(TMP,struct.pack('<6I',0x63,0x8148664,ch,0,0,0))
 call(u,S['gate_mix_event'],P,TMP);call(u,0x8133194,T,TMP)
 commands.clear();notifications.clear();parents.clear()
def flush():
 pending=notifications[:];notifications.clear()
 for e in pending:
  u.mem_write(TMP,e);call(u,S['gate_mix_event'],P,TMP);call(u,0x8133194,T,TMP)
 return pending
def val(a):return struct.unpack('<i',u.mem_read(a+0x98,4))[0]
