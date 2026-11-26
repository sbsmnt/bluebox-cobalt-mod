"""MIDI-mapped CCs for Cobalt's parameters, on the M7. Real code: the stock
definition build (0x0805342c) plus learn_defs_build, the map add for an M4
0x7c message (0x0804f364) and the CC resolver (0x0804f0c8, through the
patched post at 0x0804f1ca). Only the M7 allocator and the event-queue push
(0x0804e1e8) are substituted."""
from firmware_harness import *
import firmware_harness as H
G=M['symbols']['gate7']
THIS=0xc0100000;CTX=0x24040000;MSG=0x24030000;MIDI=0x24030100;DEFS=0x24008e50
def heap_machine(image,alloc_at,start):
 m=machine(image);heap=[start]
 def alloc(q):
  a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
 stub(m,alloc_at,alloc);return m
u=heap_machine(patched,0x80413d4,0xc0400000)
posts=[]
def push(q):
 a=q.reg_read(UC_ARM_REG_R0);e=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))
 queue=(a-CTX-4)//0x604 if CTX<=a<CTX+4+0x38*0x604 and (a-CTX-4)%0x604==0 else None
 posts.append((queue,e[0],struct.unpack_from('<I',e,8)[0],struct.unpack_from('<H',e,12)[0],struct.unpack_from('<i',e,16)[0]))
stub(u,0x804e1e8,push)
s=heap_machine(stock,0x80413d4,0xc0400000);call(s,0x805342c,0)
stock_defs=bytes(s.mem_read(u32(s,DEFS),0x3934))
check(call(u,G['learn_defs_build'],0) is not None and u32(u,DEFS)!=0,'learn_defs_build runs the stock build')
defs=bytes(u.mem_read(u32(u,DEFS),0x3934))
OURS=[0x43,0x3a,0x3d,0x4a,0x4b,*range(0x1a0,0x1ba),*range(0x19c,0x1a0),0x1e6,0x1e7,0x1eb,0x1ec,0x1ed,0x1ee,0x1ef,0x1df,0x1e0] # Freeze 0x19c-0x19f, 0x1e6; G30 Spiral 0x1df, 0x1e0
SYNTH_INSERT=[*range(0x1ba,0x1d6),*range(0x1d7,0x1df)] # checked in test_s9b_midi_map.py
check(all(struct.unpack_from('<H',stock_defs,i*28)[0]==0 for i in OURS),'the stock M7 table has none of these ids (why their CCs gave 0)')
check(all(defs[i*28:i*28+28]==stock_defs[i*28:i*28+28] for i in range(0x20b) if i not in OURS+[0x155]+SYNTH_INSERT),'every other stock M7 definition is unchanged')
m4=heap_machine(patched,0x8103128,0xc0010000);call(m4,0x8134cb8,0);T4=u32(m4,0x3001eb30)
def fields(d):return d[2],*struct.unpack_from('<iii',d,12)
bad=[hex(i) for i in OURS+[0x155] if fields(defs[i*28:i*28+28])!=fields(bytes(m4.mem_read(T4+i*28,28)))]
check(not bad,'M7 type, count and range equal the M4 definition for every Cobalt id '+str(bad))
def add_map(chan,ctrl,param,cell):
 u.mem_write(MSG,struct.pack('<BxHII',0x7c,cell,0,0x19)+struct.pack('<IIHH',chan,0xb0,ctrl,param))
 call(u,0x804f364,THIS,MSG)
def cc(chan,ctrl,value):
 posts.clear();u.mem_write(MIDI,bytes([0xb0|chan,ctrl,value,0,3]));call(u,0x804f0c8,THIS,MIDI,CTX,0)
 return [x for x in posts if x[0] is not None]
u.mem_write(THIS,bytes(0x14000))
FX1,FX2=0x14,0x15
for ctrl,param,cell in [(20,0x4a,FX1),(21,0x3a,FX1),(22,0x1a7,FX1),(23,0x1b0,FX2),(24,0x155,FX2),(25,0x1b3,FX2),(26,0x1ae,3),(27,0x1b8,0x0c),(28,0x4,5)]:add_map(0,ctrl,param,cell)
def value(p,q,t,i):
 v=[x[4] for x in p if x[0]==q and x[2]==t and x[3]==i];return v[0] if len(v)==1 else v
check((value(cc(0,20,0),FX1,FX1,0x4a),value(cc(0,20,127),FX1,FX1,0x4a))==(-12,12),'delay Pitch follows CC 0..127 as -12..+12 on the FX1 queue')
check(value(cc(0,21,127),FX1,FX1,0x3a)==1000 and value(cc(0,21,64),FX1,FX1,0x3a)==504,'delay Drift: CC 127 -> 1000, CC 64 -> 504')
check(value(cc(0,22,127),FX1,FX1,0x1a7)==1000,'tape Drive reaches 1000')
check(value(cc(0,23,127),FX2,FX2,0x1b0)==1000,'Gazer Size reaches 1000 on the FX2 queue')
check(value(cc(0,24,127),FX2,FX2,0x155)==15,'Reverb Style reaches Gazer (15)')
check((value(cc(0,25,0),FX2,FX2,0x1b3),value(cc(0,25,127),FX2,FX2,0x1b3))==(10,2000),'Gazer Rate covers its 10..2000 range')
p=cc(0,26,127)
check(value(p,0x0c,3,0x1ae)==-12000 and value(p,3,3,0x1ae)==-12000,'Gate threshold: on queue 0x0c and on its track queue, where the Gate reads it')
check(value(cc(0,27,0),0x0c,0x0c,0x1b8)==-12000,'master saturation Output starts at -12 dB')
p=cc(0,28,127)
check([x for x in p if x[0]==5]==[] and value(p,0x0c,5,0x4)!=[],'a stock parameter (track Level) still goes to queue 0x0c only')
print(H.checks,'Cobalt MIDI map checks passed; controller response needs hardware.')
