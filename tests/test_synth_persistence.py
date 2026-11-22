"""Native synth and Insert On definitions, per-track model/IPC and XML save/load."""
from gate_fixture import *
import firmware_harness as H
IDS=list(range(0x1bf,0x1d5))+list(range(0x1d7,0x1de));I0=IDS[0]
KEYS=['synon','synengine','synchannel','synharm','syntimbre','synmorph','synattack','synrelease','synlevel','syndrone','syncutoff','synlforate','synlfodest','synlfoamt','synenvdest','synenvamt','synenvdecay','synenvattack','synlfoshape','synlfosync','synlfomode','synseed','synlfo2rate','synlfo2dest','synlfo2amt','synlfo2shape','synlfo2sync','synlfo2mode','synreso'] # S18 # S10: LFO2
DEFAULTS=[0,0,1,500,350,500,5,150,500,0,20000,1000,0,0,0,0,500,0,0,0,0,0]+[1000,0,0,0,0,0,0] # S10 LFO2; S18 Reso
# These definitions must be vacant in stock rather than replacing native keys.
v=machine(stock);h=[0xc0010000]
def allocation(q):
 a=h[0];h[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(v,0x8103128,allocation);call(v,0x8134cb8,0)
check(all(bytes(v.mem_read(0xc0010000+28*pid,28))==bytes(28) for pid in IDS+[0x1d5]),'all twenty-two synth parameter IDs and Insert On are unoccupied in stock')
# Real native settings registration, generic channel list and model setter.
APP=0x300173d0;TMP=0x24008000;LIST=0x24050000
u=machine();heap=[0xc0010000]
def allocate4(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate4);call(u,0x8134cb8,0);call(u,0x8120abc)
for pid,key in zip(IDS,KEYS):
 meta=0xc0010000+28*pid
 check(u32(u,meta+24)!=0 and bytes(u.mem_read(u32(u,meta+24),32)).split(b'\0')[0]==key.encode(),'new native definition/project key '+key)
for ch in range(12):
 u.mem_write(TMP,b'\1');setting=APP+0xc08+ch*0x30;call(u,0x81202b8,setting,TMP)
 check([call(u,0x8120234,setting,pid) for pid in IDS]==DEFAULTS and struct.unpack('<HH',u.mem_read(setting+10,4))==(94,112) and call(u,0x8120234,setting,0x1d5)==1,'all twenty-nine synth defaults and Insert On fit the native channel capacity '+str(ch))
notifications=[];stub(u,0x813c384,lambda q:notifications.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
commands=[];stub(u,0x8108784,lambda q:commands.append(tuple(q.reg_read(r) for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))))
stub(u,0x81247c8,lambda q:None)
u.mem_write(TMP,struct.pack('<H',3));call(u,0x8124b44,APP,TMP,I0+4,850)
check(call(u,0x8120234,APP+0xc08+3*0x30,I0+4)==850 and commands==[(3,I0+4,850)],'native model commit saves one track and sends its exact audio command')
check(call(u,0x8120234,APP+0xc08+2*0x30,I0+4)==350,'neighboring track retains its independent Timbre value')

# Native XML writer/reader; only XML storage primitives are boundary stubs.
XML=0x24058000;VTY=XML+0x100;VXML=XML+0x200;values={};last=[''];current=[0]
w32(u,VTY,0x80dfe01);stub(u,0x80dfe00,lambda q:q.reg_write(UC_ARM_REG_R0,1));w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
for addr in (0x8107b16,0x8106c92):stub(u,addr,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def key(q):last[0]=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),64)).split(b'\0')[0].decode();q.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,key);stub(u,0x8106dc6,lambda q:values.setdefault(current[0],{}).update({last[0]:q.reg_read(UC_ARM_REG_R1)}));stub(u,0x8106dba,lambda q:None)
expected={}
for ch in range(12):
 current[0]=ch;setting=APP+0xc08+ch*0x30;w32(u,setting,VTY);expected[ch]=[ch%2,ch%5,ch+1,100+ch*50,200+ch*30,800-ch*40,5+ch*10,150+ch*20,500+ch*20,ch%2,20000-ch*1000,10+ch*1500,ch%5,ch*80,(ch+1)%5,-100+ch*15,5+ch*400,ch*300,ch%4,ch%13,ch%2,ch%32,20000-ch*1500,(ch+2)%5,1000-ch*70,(ch+1)%4,(ch*5)%25,(ch+1)%2,ch*70] # S18: Reso
 for pid,val in zip(IDS,expected[ch]):call(u,0x8120264,setting,pid,val&0xffffffff)
 call(u,0x8120264,setting,0x1d5,ch%2)
 call(u,0x8120b84,XML,XML,setting)
 check([values[ch].get(k) for k in KEYS]==[v&0xffffffff for v in expected[ch]] and values[ch].get('inson')==ch%2,'native project writer saves independent synth settings and Insert On '+str(ch))
 u.mem_write(TMP,b'\1');call(u,0x81202b8,setting,TMP);w32(u,setting,VTY)
stub(u,0x81074a6,lambda q:q.reg_write(UC_ARM_REG_R0,0));stub(u,0x8106ae8,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def find(q):last[0]=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),64)).split(b'\0')[0].decode();q.reg_write(UC_ARM_REG_R0,XML if last[0] in values[current[0]] else 0)
def read(q):w32(q,q.reg_read(UC_ARM_REG_R1),values[current[0]][last[0]]);q.reg_write(UC_ARM_REG_R0,0)
stub(u,0x810747c,find);stub(u,0x8106d9e,read)
for ch in range(12):
 current[0]=ch;setting=APP+0xc08+ch*0x30;call(u,0x8120c3c,XML,setting)
 check([call(u,0x8120234,setting,pid) for pid in IDS]==[v&0xffffffff for v in expected[ch]] and call(u,0x8120234,setting,0x1d5)==ch%2,'native project reader restores synth settings and Insert On '+str(ch))
 for k in KEYS+['inson']:values[ch].pop(k)
 u.mem_write(TMP,b'\1');call(u,0x81202b8,setting,TMP);w32(u,setting,VTY);call(u,0x8120c3c,XML,setting)
 check(call(u,0x8120234,setting,I0)==0 and call(u,0x8120234,setting,0x1d0)==0 and call(u,0x8120234,setting,0x1d5)==1,'older project keeps Synth Off, immediate modulation attack and Insert On '+str(ch))

print(H.checks,'native synth project/IPC checks passed.')
