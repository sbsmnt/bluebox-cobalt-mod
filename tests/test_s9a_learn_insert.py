"""S9a: MIDI Learn on the insert TRACK page. Same boundaries as
test_s9a_learn_synth: real TRACK view, native learn info and overlays,
SessionMgr arm and CC learn."""
from gate_ui_fixture import *
import firmware_harness as H
IU=0x388001e0;OFFSETS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48]
IDS=list(range(0x1ba,0x1bf));EV=TMP+0x400;MSG=TMP+0x700;MAP=0x24070000
INS_ON=0x1d5;BUTTON=T+0xe814
global_fn=u32(u,0x814bb70)
stub(u,0x8102a4c,lambda q:None);stub(u,0x8108950,lambda q:None)
sent=[];stub(u,0x810884a,lambda q:sent.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
def binding(i):return struct.unpack('<H',u.mem_read(T+OFFSETS[i]+0x94,2))[0]
def bind_notify(group):
 call(u,S['insert_track_group'],T,group)
 u.mem_write(EV,struct.pack('<6I',0x67,0x8148664,0,0,0,0));call(u,global_fn,T,EV)
def press_a(action):
 u.mem_write(APP+0x739a,bytes([5 if action==2 else 6]))
 u.mem_write(EV,struct.pack('<6I',12,0x8148664,0,0,0,0));call(u,0x8123862,APP,EV)
def refresh(ch=7):
 u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,ch,0,0,0));call(u,global_fn,T,EV)
def overlay(w,toggle=False):
 o=w+(0x100 if toggle else 0x2f4)
 return u.mem_read(o+0x30,1)[0]==0,u32(u,o+0xc0)==0x1b,bytes(u.mem_read(o+0x65,16)).split(b'\0')[0]
def knob(i):return T+OFFSETS[i]
def learn_tap(w,toggle=False):
 parents.clear();u.mem_write(EV,struct.pack('<6I',0x13e,0x8148664,0,0,0,0))
 call(u,0x8126f4c if toggle else 0x8127e98,w,EV)
 posted=[p for p in parents if struct.unpack_from('<H',p)[0]==0x13e]
 for p in posted:u.mem_write(EV,p);call(u,0x8125d44,APP,EV)
 refresh()
 return [(a&0xffff,b) for a,b in (struct.unpack_from('<HxxIII',p)[2:] for p in posted)] # cell is a u16 in that word (as test_s9c)
def learn_cc(cc,chan=0):
 u.mem_write(MSG,bytes(0x18));u.mem_write(MSG+0xc,struct.pack('<II',chan,0xb0));u.mem_write(MSG+0x14,struct.pack('<H',cc))
 call(u,0x8123e44,APP,MSG);refresh()
def items():
 return [struct.unpack_from('<HHHHH',bytes(u.mem_read(MAP+i*20,20))) for i in range(u32(u,APP+0x5efc))]

setup(0,7,-96000);w32(u,APP+0x5ef8,MAP);w32(u,APP+0x5efc,0)
press_a(2);u.mem_write(APP+0x739a,b'\3');bind_notify(1);press_a(3);bind_notify(0)
check(u32(u,IU+4)==1 and [binding(i) for i in range(5)]==IDS,'insert page bound: type, Drive, Tone, Mix, Output')
u.mem_write(APP+0x73b3,b'\1');refresh()
check([overlay(knob(i))[0] for i in range(8)]==[False,True,True,True,True,False,False,False],'Learn on: Drive, Tone, Mix and Output are outlined; the type knob and empty slots are not')
check(overlay(BUTTON,True)[0],'Learn on: INSRT is outlined')
group=u32(u,0x38800c08)
check(not overlay(group,True)[0],'Learn on: DRV / FLTR (a type-group switch, not a value) is not outlined')
for i,cc in ((1,30),(2,31),(3,32),(4,33)):
 e=learn_tap(knob(i));learn_cc(cc)
 check(e==[(7,IDS[i])] and (0,0xb0,cc,IDS[i],7) in items() and overlay(knob(i))[2]==b'1:%d'%cc,'%s maps to CC %d and shows it'%(['','Drive','Tone','Mix','Output'][i],cc))
e=learn_tap(BUTTON,True);learn_cc(34)
check(e==[(7,INS_ON)] and (0,0xb0,34,INS_ON,7) in items() and overlay(BUTTON,True)[2]==b'1:34','INSRT maps to CC 34 and shows it')
check(all(overlay(knob(i))[2]==b'1:%d'%cc for i,cc in ((1,30),(2,31),(3,32),(4,33))),'every insert knob keeps showing its own CC after later learns')
check(overlay(knob(0))==(False,False,b''),'type knob stays without learn state')
print(H.checks,'S9a insert MIDI Learn checks passed; controller response needs hardware.')
