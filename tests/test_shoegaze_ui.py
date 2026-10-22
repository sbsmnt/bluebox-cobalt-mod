"""Real native FX2 defs, selector, model setter/command boundary, knob input,
panel refresh and XML persistence for the experimental Shoegaze style.
"""
from firmware_harness import *
import firmware_harness as H
APP=0x300173d0;SET=APP+0xe58;S=M['symbols']['4'];P=0x24010000
TMP=0x24008000;POP=0xc0100000;LIST=POP+0x1090;SLIST=0x24050000
IDS=list(range(0x1af,0x1b6));DEFAULTS=[350,600,750,250,200,5500,120]
KEYS=['sgsat','sgsize','sgdiff','sgmod','sgrate','sghicut','sglocut']
def cs(u,a):return bytes(u.mem_read(a,64)).split(b'\0')[0]
def init(image=patched):
 u=machine(image);heap=[0xc0010000]
 def alloc(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,alloc);call(u,0x8134cb8,0);call(u,0x8120abc)
 u.mem_write(TMP,b'\5');call(u,0x81202b8,SET,TMP)
 return u
u=init();old=init(stock)
for i,pid in enumerate(IDS):
 a=0xc0010000+pid*28
 check(old.mem_read(a,28)==bytes(28),'Shoegaze ID was unused in stock: '+hex(pid))
 check(cs(u,u32(u,a+24)).decode()==KEYS[i] and call(u,0x8120234,SET,pid)==DEFAULTS[i],'native FX2 key/default registered: '+KEYS[i])
check(call(u,0x8136094,0x155)==16 and cs(u,call(u,0x8136134,0x155))==b'r2style','native style enum adds exactly one row and preserves the project key')
# Full native popup fill; read actual rows and saved highlight.
call(u,0x8138154,POP+0xf50);call(u,0x8138154,POP+0xff0);call(u,0x813c004,LIST)
u.mem_write(LIST+4,struct.pack('<4i',0,0,320,180));u.mem_write(POP+0xd99a,struct.pack('<H',21))
call(u,0x8120264,SET,0x155,15);call(u,S['dly_selector_fill'],POP)
names=[cs(u,LIST+0x181+i*0xc8) for i in range(u32(u,LIST+0xc8e4))]
check(len(names)==16 and names[-1]==b'Gazer' and u32(u,LIST+0xc8e8)==15,'FX2 B popup displays and highlights Gazer (exp.8 name) as the sixteenth native row')
selection=[];stub(u,0x813a414,lambda q:selection.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
u.mem_write(TMP,struct.pack('<H6xH2xIII',0x150,21,15,0,0))
call(u,S['dly_selector_event'],POP,TMP)
check(len(selection)==1 and struct.unpack_from('<H',selection[0],8)[0]==21 and struct.unpack_from('<II',selection[0],12)==(0x155,15),'selecting the visible row emits exactly one FX2 style15 event')
# Native parameter lists use vtable kind=5, like the actual FX2 model.
w32(u,SET,0x24009000);w32(u,0x24009000,0x80dfe21);stub(u,0x80dfe20,lambda q:q.reg_write(UC_ARM_REG_R0,5))
u.mem_write(TMP,struct.pack('<H',21));call(u,S['dly_parameters'],APP,TMP,SLIST)
listed=[struct.unpack('<H',u.mem_read(SLIST+4+i*12,2))[0] for i in range(u32(u,SLIST))]
check(listed==IDS+[0x15a],'Shoegaze has all seven requested controls and native Level, in two four-encoder groups')
# Model setter is real. Only transport, dirty-file marking and notification
# delivery are boundary substitutes. Forward its emitted command to M7 below.
commands=[];notifications=[]
stub(u,0x8108784,lambda q:commands.append(tuple(q.reg_read(r) for r in [UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3])))
stub(u,0x81247c8,lambda q:None);stub(u,0x813c384,lambda q:notifications.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
stub(u,0x8121f4c,lambda q:q.reg_write(UC_ARM_REG_R0,0))
u.mem_write(TMP,struct.pack('<H',21))
for pid,v in zip(IDS,[800,900,650,700,750,6800,240]):
 commands.clear();notifications.clear()
 # Fifth native argument: not an initialization/quiet edit.
 h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:w32(q,q.reg_read(UC_ARM_REG_SP),0),begin=0x8124b44,end=0x8124b44)
 call(u,0x8124b44,APP,TMP,pid,v);u.hook_del(h)
 check(call(u,0x8120234,SET,pid)==v and commands==[(21,pid,v)],'native model setter saves and emits exactly one FX2 command for '+hex(pid))
# Native hardware-turn path emits a child edit with the same id and slot.
W=P+0xc3b0;u.mem_write(P+0xc398,b'\1');u.mem_write(P+0xc3a4,struct.pack('<H',21))
stub(u,0x812abe0,lambda q:None);u.mem_write(TMP,struct.pack('<H',21));call(u,S['dly_populate'],P,TMP)
emitted=[];stub(u,0x813a414,lambda q:emitted.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
for pid,v,shown in [(0x1af,350,b'35.0%'),(0x1b0,600,b'60.0%'),(0x1b1,750,b'75.0%'),(0x1b2,250,b'25.0%'),(0x1b3,200,b'0.200Hz'),(0x1b4,5500,b'5500Hz'),(0x1b5,120,b'120Hz')]:
 u.mem_write(W,bytes(0x3d0));u.mem_write(W+0x32,struct.pack('<H',21));u.mem_write(W+0x3c9,b'\1')
 call(u,0x8127b84,W,pid,v,1);w32(u,0x3001eb3c,10000)
 call(u,S['dly_turn'],W,0)
 check(cs(u,W+0xd1)==shown,'native knob shows exact Shoegaze value/units for '+hex(pid))
 emitted.clear();call(u,S['dly_turn'],W,160)
 print('INFO encoder',hex(pid),'value',u32(u,W+0x98),'events',[v.hex() for v in emitted])
 check(len(emitted)==1 and struct.unpack_from('<H',emitted[0],8)[0]==21 and struct.unpack_from('<I',emitted[0],12)[0]==pid and u32(u,W+0x98)!=v,'native encoder emits the requested Shoegaze parameter on FX2 '+hex(pid))
# Style change must repopulate immediately, including when leaving Shoegaze.
pop=[];stub(u,0x812abe0,lambda q:pop.append(q.reg_read(UC_ARM_REG_R0)))
u.mem_write(TMP,struct.pack('<H6xH2xIII',0x6e,21,0x155,15,0));emitted.clear()
call(u,S['dly_event'],P,TMP)
check(pop==[P] and len(emitted)==1,'native FX2 style event bubbles once and repopulates its control list')
# Execute native writer/reader against an XML storage boundary.
XML=0x24058000;VT=0x24059000;w32(u,XML,VT);w32(u,VT,0x80dfe31)
stub(u,0x80dfe30,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
saved={};last=['']
stub(u,0x8107b16,lambda q:q.reg_write(UC_ARM_REG_R0,XML));stub(u,0x8106c92,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def prop(q):last[0]=cs(q,q.reg_read(UC_ARM_REG_R1)).decode();q.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,prop);stub(u,0x8106dc6,lambda q:saved.update({last[0]:q.reg_read(UC_ARM_REG_R1)}));stub(u,0x8106dba,lambda q:None)
call(u,0x8120b84,XML,XML,SET)
check(saved.get('r2style')==15 and all(saved.get(k)==v for k,v in zip(KEYS,[800,900,650,700,750,6800,240])),'native project writer preserves style15 and all seven Shoegaze controls')
for pid in IDS+[0x155]:call(u,0x8120264,SET,pid,0)
stub(u,0x81074a6,lambda q:q.reg_write(UC_ARM_REG_R0,0));stub(u,0x8106ae8,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def find(q):
 last[0]=cs(q,q.reg_read(UC_ARM_REG_R1)).decode();q.reg_write(UC_ARM_REG_R0,XML if last[0] in saved else 0)
stub(u,0x810747c,find)
stub(u,0x8106d9e,lambda q:(w32(q,q.reg_read(UC_ARM_REG_R1),saved[last[0]]),q.reg_write(UC_ARM_REG_R0,0)))
call(u,0x8120c3c,XML,SET)
check(call(u,0x8120234,SET,0x155)==15 and all(call(u,0x8120234,SET,pid)==v for pid,v in zip(IDS,[800,900,650,700,750,6800,240])),'native project reader restores all Shoegaze values and its style selection')
print(str(H.checks)+' native Shoegaze control/selector/model/persistence checks passed; physical UI testing remains.')
