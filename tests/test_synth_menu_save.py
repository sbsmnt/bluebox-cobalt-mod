"""Final native menu edits after B closes -> model dirty flag -> XML roundtrip.
S4 used to drop a final direct menu commit once the active view changed.
The exact hardware event timing in the user's report remains unobserved.
"""
from gate_ui_fixture import *
import firmware_harness as H
import base64,json
SC=0xc0100000;EV=TMP+0x500;IU=0x388001e0
setup(0,7,-96000);call(u,0x81284f0,SC);call(u,0x81285ec,SC,1)
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,7,0,0,0));call(u,0x8128ff4,SC,EV)
# Initialize UI, then represent the just-closed synth menu.
# insert_track_group creates the versioned state without opening another view.
call(u,S['insert_track_group'],T,0);w32(u,IU+4,2)
setting=APP+0xc08+7*0x30
for kind in (0x3c,0x3d,0x6e):
 call(u,0x8120264,setting,0x1cb,0);u.mem_write(APP+0x73a1,b'\0');commands.clear()
 e=[kind,0x8148664,7,4,0x1cb,0] if kind!=0x6e else [kind,0x8148664,7,0x1cb,4,0]
 u.mem_write(EV,struct.pack('<6I',*e));call(u,S['synth_menu_local'],SC,EV)
 check(call(u,0x8120234,setting,0x1cb)==4 and commands==[(7,0x1cb,4)],'final post-B menu event commits Cutoff exactly once '+hex(kind))
 check(u.mem_read(APP+0x73a1,1)==b'\1','final post-B edit marks the project dirty '+hex(kind))
# Repeat for the full enum ranges; the old destinations keep their ordinals.
for pid,values in [(0x1cb,range(5)),(0x1cd,range(5)),(0x1d1,range(4)),(0x1d2,range(25)),(0x1d3,range(2))]:
 for value in values:
  u.mem_write(EV,struct.pack('<6I',0x3d,0x8148664,7,value,pid,0));call(u,S['synth_menu_local'],SC,EV)
  check(call(u,0x8120234,setting,pid)==value,'late final menu value persists '+str((pid,value)))
# S6a Free/Sync row: the B menu toggles mode and keeps the encoder's division.
def mode(value):
 u.mem_write(EV,struct.pack('<6I',0x3d,0x8148664,7,value,0x1d3,0));call(u,S['synth_menu_local'],SC,EV)
 return call(u,0x8120234,setting,0x1d2),call(u,0x8120234,setting,0x1d3)
call(u,0x8120264,setting,0x1d2,0)
check(mode(1)==(9,1),'S5 Free (0) switched to Sync starts at the 1/4 division')
call(u,0x8120264,setting,0x1d2,4)
check(mode(0)==(16,0),'Free remembers division 1/8T as stored value 16')
check(mode(1)==(4,1),'Sync restores the remembered 1/8T division')
check(mode(1)==(4,1),'repeated Sync leaves the division unchanged')
# Real settings writer/reader; only XML attribute storage is substituted.
for pid,value in [(0x1cb,4),(0x1cd,4),(0x1d1,3),(0x1d2,21),(0x1d3,0)]:call(u,0x8120264,setting,pid,value)
XML=0x24058000;VTY=XML+0x100;VXML=XML+0x200;attributes={};last=['']
w32(u,VTY,0x80dfe01);stub(u,0x80dfe00,lambda q:q.reg_write(UC_ARM_REG_R0,1))
w32(u,setting,VTY);w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
for a in (0x8107b16,0x8106c92):stub(u,a,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def key(q):last[0]=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),64)).split(b'\0')[0].decode();q.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,key);stub(u,0x8106dc6,lambda q:attributes.update({last[0]:q.reg_read(UC_ARM_REG_R1)}));stub(u,0x8106dba,lambda q:None)
call(u,0x8120b84,XML,XML,setting)
check([attributes[k] for k in ('synlfodest','synenvdest','synlfoshape','synlfosync','synlfomode')]==[4,4,3,21,0],'writer keeps Cutoff destinations, Random shape, Free with a remembered 1/4 division')
u.mem_write(TMP,b'\1');call(u,0x81202b8,setting,TMP);w32(u,setting,VTY)
stub(u,0x81074a6,lambda q:q.reg_write(UC_ARM_REG_R0,0));stub(u,0x8106ae8,lambda q:q.reg_write(UC_ARM_REG_R0,XML))
def find(q):key(q);q.reg_write(UC_ARM_REG_R0,XML if last[0] in attributes else 0)
def read(q):w32(q,q.reg_read(UC_ARM_REG_R1),attributes[last[0]]);q.reg_write(UC_ARM_REG_R0,0)
stub(u,0x810747c,find);stub(u,0x8106d9e,read);call(u,0x8120c3c,XML,setting)
check([call(u,0x8120234,setting,pid) for pid in (0x1cb,0x1cd,0x1d1,0x1d2,0x1d3)]==[4,4,3,21,0],'project reload preserves final menu choices')
# Check the previous version's exact handler at the same post-B boundary.
ref=json.loads((ROOT/'tests/references/cobalt-synth-S4-manifest.json').read_text());old=bytearray(stock)
for p in ref['patches']:
 b=base64.b64decode(p['new']);old[p['offset']:p['offset']+len(b)]=b
q=machine(bytes(old));h=[0xc0010000]
def alloc(q):a=h[0];h[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(q,0x8103128,alloc);call(q,0x8134cb8,0);call(q,0x8120abc);q.mem_write(TMP,b'\1');call(q,0x81202b8,setting,TMP)
w32(q,SC+0xbc9c,7);w32(q,IU,0x53554934);w32(q,IU+4,2)
q.mem_write(EV,struct.pack('<6I',0x6e,0x8148664,7,0x1cb,4,0));call(q,ref['symbols']['4']['synth_menu_local'],SC,EV)
check(call(q,0x8120234,setting,0x1cb)==0,'S4 drops a final direct synth-menu commit arriving after B closes')
print(H.checks,'native menu persistence checks passed; device save timing still requires a retest.')
