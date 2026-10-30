"""Scenes experiment under Unicorn (bluebox-scenes/SCENES.md).
PC-map machinery (now Bank Select -> project): name table extension, settings
defaults and save/load loops, Bank # keyboard/keypad, rename/delete, MIDI
capture stubs, bank load sequence, page/Tools hooks. Scenes: template, save,
recall through the knob path (stubbed to apply values), batching, deferral,
and a real <scenes> project-XML round trip on the firmware's own XML library
and tag-table writer/reader. No MIDI hardware, SD filesystem or screen.
"""
from firmware_harness import *
import firmware_harness as H

PC=M['symbols']['pcm4']
TAG0=0x20b;SLOTS=128
SESSION=0x300173d0;SETTINGS=SESSION+0x737c;STATE=0x38800fe0
def string(u,p):return bytes(u.mem_read(p,64)).split(b'\0')[0]
def fnv(name):
 h=2166136261
 for c in name.lower().encode():h=((h^c)*16777619)&0xffffffff
 return h or 1
def heap_machine(image=patched):
 u=machine(image);heap=[0xc0010000]
 def allocate(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,allocate);return u

# Name table: 0x28b records, stock ids unchanged, slots named pcmap0..127.
def definitions(image,size):
 u=heap_machine(image);call(u,0x8134cb8,0)
 return u,bytes(u.mem_read(0xc0010000,size))
a,old=definitions(stock,0x3934);u,new=definitions(patched,0x484c)
check(patched[0x08134cce-0x08040000:][:4]==bytes.fromhex('44f64c00') and patched[0x08134ce0-0x08040000:][:4]==bytes.fromhex('44f64e02'),'definition table: allocation (r0) 0x484c = 0x295 records, clear length (r2) 0x484e')
cobalt={0x43,0x3a,0x3d,0x4a,0x4b,0x155,*range(0x1a0,0x1ba)}
check(all(old[i*28:(i+1)*28]==new[i*28:(i+1)*28] for i in range(0x20b) if i not in cobalt),'stock parameter definitions 0..0x20a are preserved')
recs=[struct.unpack('<HB',new[i*28:i*28+3]) for i in range(TAG0,TAG0+SLOTS)]
check(all(r==(TAG0+i,1) for i,r in enumerate(recs)),'128 PC slot records registered at 0x20b..0x28a')
check([string(u,u32(u,0xc0010000+(TAG0+i)*28+24)) for i in (0,5,127)]==[b'pcmap0',b'pcmap5',b'pcmap127'],'slot keys pcmap0/pcmap5/pcmap127')
check(string(u,call(u,0x8136134,TAG0))==b'pcmap0' and string(u,call(u,0x8136134,0x28a))==b'pcmap127','tag->name resolves the first and last slot')
check(string(u,call(u,0x8136134,0x28b))==b'bankcc' and struct.unpack('<H',new[0x28b*28:0x28b*28+2])[0]==0x28b,'Bank CC setting registered at 0x28b (bankcc)')
keys={0x28b:b'bankcc',0x28c:b'scenesel',0x28d:b'scenesave',0x28e:b'bankassign',0x28f:b'scenech',0x290:b'bankch',0x291:b'midimenu',0x292:b'dinout',0x293:b'ccsync',0x294:b'clksrc'}
check(all(string(u,call(u,0x8136134,i))==k for i,k in keys.items()),'scene/MIDI tags registered at 0x28b..0x294')
check(call(u,0x8136134,0x295)==0,'tag->name still refuses ids past the table')
u.mem_write(0x24010000,b'bankcc\0');check(call(u,0x8136164,0x24010000)==0x28b,'name->tag finds bankcc')
check(string(u,call(u,0x8136134,0x43))==b'mstdrive','tag->name keeps Cobalt/stock names')
u.mem_write(0x24010000,b'pcmap5\0');check(call(u,0x8136164,0x24010000)==TAG0+5,'name->tag finds pcmap5 in the extended table')
def lookup(m,name):m.mem_write(0x24010000,name+b'\0');return call(m,0x8136164,0x24010000)
keys=[b'gaindb',b'vertmode',b'xparam']
check([lookup(u,k) for k in keys]==[lookup(a,k) for k in keys],'name->tag results for stock keys match stock')

# settings.tml defaults: 7 stock tags then 128 zero slots, capacity 144.
def defaults(image):
 u=heap_machine(image);call(u,0x8120a52,SETTINGS)
 n,cap=struct.unpack('<HH',u.mem_read(SETTINGS+10,4))
 return u,[struct.unpack('<H2xi',u.mem_read(u32(u,SETTINGS+4)+8*i,8)) for i in range(n)],cap
_,sold,scap=defaults(stock);u,snew,cap=defaults(patched)
check(scap==0x44 and cap==0x90,'settings capacity 68 -> 144')
check(snew[:7]==sold and len(sold)==7,'stock settings defaults unchanged and first')
check(snew[7:]==[(TAG0+i,0) for i in range(SLOTS)]+[(0x28b,0),(0x28f,0),(0x290,0),(0x291,0),(0x292,0),(0x293,1),(0x294,0)] and len(snew)<=cap,'Bank # slots, Bank CC (MSB), Scene/Bank channels (All), the MIDI row, MIDI Out (Default), Map CC Sync (USB) and Clock Src (MIDI In) appended within capacity')

# settings.tml writer/reader loops with the XML object boundary stubbed.
u=heap_machine();call(u,0x8134cb8,0);call(u,0x8120a52,SETTINGS)
XML=0x24050000;VXML=0x24051000
w32(u,XML,VXML);w32(u,VXML,0x80dfe11);stub(u,0x80dfe10,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
for f in (0x81078c0,0x8107e24,0x8106c92,0x8107b16,0x8106ae8):stub(u,f,lambda u:u.reg_write(UC_ARM_REG_R0,XML))
saved={};last=['']
def key(u):last[0]=string(u,u.reg_read(UC_ARM_REG_R1)).decode();u.reg_write(UC_ARM_REG_R0,XML)
stub(u,0x81074dc,key);stub(u,0x8106dc6,lambda u:saved.update({last[0]:u.reg_read(UC_ARM_REG_R1)}))
call(u,0x8120264,SETTINGS,TAG0+4,0xdeadbeef);call(u,0x8120264,SETTINGS,TAG0+127,0x1234)
call(u,0x8137698,0x24010000,0,0,SETTINGS,end=0x813774e)
check(saved.get('pcmap4')==0xdeadbeef and saved.get('pcmap127')==0x1234 and saved.get('pcmap0')==0,'settings writer emits every PC slot attribute')
check(len([k for k in saved if k.startswith('pcmap')])==SLOTS and len(saved)==7+SLOTS+7 and saved.get('bankcc')==0 and saved.get('scenech')==0 and saved.get('midimenu')==0 and saved.get('dinout')==0 and saved.get('ccsync')==1 and saved.get('clksrc')==0,'writer emits 7 stock + 128 slot + 7 MIDI attributes')
stub(u,0x8137be8,lambda u:u.reg_write(UC_ARM_REG_R0,1))
def find(u):last[0]=string(u,u.reg_read(UC_ARM_REG_R1)).decode();u.reg_write(UC_ARM_REG_R0,XML if last[0] in saved else 0)
stub(u,0x810747c,find)
def read(u):w32(u,u.reg_read(UC_ARM_REG_R1),saved[last[0]]);u.reg_write(UC_ARM_REG_R0,0)
stub(u,0x8106d9e,read)
OTHER=0x24030000
call(u,0x81377f4,0x24010000,0,0,OTHER,end=0x81378ae)
check(call(u,0x8120234,OTHER,TAG0+4)==0xdeadbeef and call(u,0x8120234,OTHER,TAG0+127)==0x1234,'settings reader restores PC slots')
saved.pop('pcmap4');call(u,0x81377f4,0x24010000,0,0,OTHER,end=0x81378ae)
check(call(u,0x8120234,OTHER,TAG0+4)==0,'older settings.tml without a slot loads it as empty')

# pc_* entry points on a live settings blob.
def session():
 u=heap_machine();call(u,0x8120a52,SETTINGS);saves=[]
 stub(u,0x811e920,lambda u:saves.append((u.reg_read(UC_ARM_REG_R0),string(u,u.reg_read(UC_ARM_REG_R1)),u.reg_read(UC_ARM_REG_R2))))
 return u,saves
def slot(u,i):return call(u,0x8120234,SETTINGS,TAG0+i)
def slots(u):return {i:v for i in range(SLOTS) if (v:=slot(u,i))}
u,_=session();u.mem_write(0x24010000,b'My Song\0')
check(call(u,PC['pc_hash'],0x24010000)==fnv('my song'),'pc_hash is FNV-1a of the lower-cased name')

VIEW=0x24040000;KB=0x24060000
# Rename and delete follow the name.
u,saves=session();renames=[];deletes=[]
stub(u,0x8122468,lambda u:renames.append((string(u,u.reg_read(UC_ARM_REG_R1)),string(u,u.reg_read(UC_ARM_REG_R2)))))
stub(u,0x8122f50,lambda u:deletes.append(string(u,u.reg_read(UC_ARM_REG_R1))))
call(u,0x8120264,SETTINGS,TAG0+9,fnv('Old'));u.mem_write(0x24010000,b'Old\0');u.mem_write(0x24010100,b'New Name\0')
call(u,PC['pc_rename'],SESSION,0x24010000,0x24010100)
check(renames==[(b'Old',b'New Name')] and slots(u)=={9:fnv('new name')} and len(saves)==1,'rename runs natively and moves Bank 10 to the new name')
u.mem_write(0x24010000,b'Unassigned\0');call(u,PC['pc_rename'],SESSION,0x24010000,0x24010100)
check(len(renames)==2 and len(saves)==1,'renaming an unassigned project does not save settings')
u.mem_write(0x24010100,b'NEW NAME\0');call(u,PC['pc_delete'],SESSION,0x24010100)
check(deletes==[b'NEW NAME'] and slots(u)=={} and len(saves)==2,'delete clears the slot (case-insensitive) then deletes natively')

# MIDI capture stub (TIM4 IRQ context, r0=status, r4=message).
MSG=0x24010000
def isr(status,data1):
 u=machine();w32(u,STATE+4,0);u.mem_write(MSG,bytes([status,data1,0,0,2]))
 u.reg_write(UC_ARM_REG_R4,MSG);u.reg_write(UC_ARM_REG_R0,status&0xf0 if status<0xf0 else status);u.reg_write(UC_ARM_REG_SP,0x2407f000)
 hit=[]
 def resume(u,a,n,_):
  if a in (0x8103386,0x81033ba):hit.append(a);u.emu_stop()
 u.hook_add(UC_HOOK_CODE,resume,begin=0x8103380,end=0x81033c0)
 u.emu_start(PC['pc_midi_isr']|1,0,count=20)
 if not hit:raise AssertionError('ISR stub did not resume stock code')
 return hit[0],u32(u,STATE+4)
check(isr(0xc0,0x05)==(0x8103386,0x1005),'Program Change 5 (channel 1) stored as 0x1000|ch<<8|program, stock forwarding resumes')
check(isr(0xcf,0x7f)==(0x8103386,0x1f7f),'Program Change channel 16 keeps its channel')
check(isr(0xd0,0x05)==(0x81033ba,0),'non-PC status takes the stock ignore path without touching pending')


# Bank Select stub at 0x08103378: CC 0 stores its value, everything else
# resumes the stock dispatch with the original compare flags.
TR=0x38800fa0
def cc(status,d1,d2):
 u=machine();w32(u,TR+0x18,0);w32(u,TR+0x1c,0);u.mem_write(MSG,bytes([status,d1,d2,0,3]))
 u.reg_write(UC_ARM_REG_R4,MSG);u.reg_write(UC_ARM_REG_R0,status&0xf0 if status<0xf0 else status);u.reg_write(UC_ARM_REG_SP,0x2407f000)
 hit=[]
 def resume(u,a,n,_):
  if a in (0x8103386,0x810337c):hit.append(a);u.emu_stop()
 u.hook_add(UC_HOOK_CODE,resume,begin=0x8103370,end=0x8103390)
 u.emu_start(PC['pc_midi_cc']|1,0,count=20)
 if not hit:raise AssertionError('CC stub did not resume stock code')
 cc.lsb=u32(u,TR+0x1c)
 return hit[0],u32(u,TR+0x18),(u.reg_read(UC_ARM_REG_CPSR)>>29)&3
check(cc(0xb0,0,5)==(0x8103386,0x1005,3) and cc.lsb==0,'Bank Select MSB (CC 0 = 5) stored, stock CC forwarding resumes')
check(cc(0xb0,32,9)==(0x8103386,0,3) and cc.lsb==0x1009,'Bank Select LSB (CC 32 = 9) stored separately')
check(cc(0xb4,0,1)[1]==0x1401,'Bank Select keeps its channel (5)')
check(cc(0xb0,7,100)[:2]==(0x8103386,0),'other controllers (CC 7) forwarded untouched')
r=cc(0x90,60,100);check(r[:2]==(0x810337c,0) and r[2]==0,'non-CC status resumes at the stock bhi with the compare flags (lower)')
r=cc(0xe0,0,64);check(r[:2]==(0x810337c,0) and r[2]==1,'higher status resumes with carry set (stock bhi taken)')

# Main loop, banks: Bank Select -> 300 ms settle -> event 0x18 (LoadBank) for
# the project with that Bank #; dropped while recording, while a load runs or
# during a bank load sequence. Program Change -> scene request (see below).
u,_=session();names=['Alpha','Beta','My Song','Delta']
stub(u,0x8100730,lambda u:None)
def listname(u):
 i=u.reg_read(UC_ARM_REG_R1)
 if i<len(names):u.mem_write(u.reg_read(UC_ARM_REG_R2),names[i].encode()+b'\0');u.reg_write(UC_ARM_REG_R0,1)
 else:u.reg_write(UC_ARM_REG_R0,0)
stub(u,0x810204e,listname)
curname=['']
stub(u,0x8122458,lambda u:u.mem_write(u.reg_read(UC_ARM_REG_R1),curname[0].encode()+b'\0'))  # current project name
RING=0x30000180;NOW=0x3001eb3c;LOADNAME=0x3001734c;LOADBUF=0x24012000;clock=[1000]
ENGINE=0x30040000;plays=[]
w32(u,0x3000a894,ENGINE)
stub(u,0x81087e8,lambda u:plays.append((u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1),u.reg_read(UC_ARM_REG_R2),u.reg_read(UC_ARM_REG_R3))))
w32(u,LOADNAME,LOADBUF);u.mem_write(LOADBUF,b'\0')
w32(u,STATE,0x50434d31)
def main(bank=0,dt=0,moving=False,program=0):
 clock[0]+=dt;w32(u,NOW,clock[0]);w32(u,TR+0x18,bank);w32(u,STATE+4,program);before=u32(u,RING+0x1488)
 if moving:w32(u,ENGINE+0x78b0,u32(u,ENGINE+0x78b0)+32)
 call(u,PC['pc_main'],RING);after=u32(u,RING+0x1488)
 e=bytes(u.mem_read(RING+0xe84+(before&63)*24,24))
 return after-before,struct.unpack('<H',e[:2])[0],struct.unpack('<I',e[12:16])[0],u32(u,TR+0x18)
def phase():return u32(u,TR+4)
def complete(moving=False):
 u.mem_write(LOADBUF,b'Busy\0');main(dt=20,moving=moving);u.mem_write(LOADBUF,b'\0');main(dt=20,moving=moving);main(dt=320,moving=moving)
call(u,0x8120264,SETTINGS,TAG0+2,fnv('my song'));call(u,0x8120264,SETTINGS,TAG0+1,fnv('beta'))
u.mem_write(SESSION+0x739a,b'\x07')
check(main(0x1002)[0]==0 and main(dt=250)[0]==0,'Bank Select waits for the 300 ms settle')
check(main(dt=60)==(1,0x18,2,0) and phase()==2,'then Bank 3 (CC 0 = 2) posts Load event 0x18 for list index 2')
check(u32(u,STATE+24)==0x107,'current page (7) remembered for after the load')
check(main(0x1001,dt=10)[0]==0 and main(dt=400)[0]==0,'Bank Select during the bank load sequence is ignored')
complete();check(phase()==0,'sequence ends 300 ms after the load completes')
check(main(0x1001,dt=10)[0]==0 and main(0x1002,dt=222)[0]==0 and main(dt=310)==(1,0x18,2,0),'Bank Selects 222 ms apart collapse: only the last one loads')
complete()
u.mem_write(LOADBUF,b'Alpha\0')
check(main(0x1001,dt=10)[0]==0 and main(dt=500)[0]==0,'Bank Select during a running (manual) load is ignored')
u.mem_write(LOADBUF,b'\0');check(main(dt=500)[0]==0,'and does not fire afterwards')
for flag in (0x73d6,0x73d7):
 u.mem_write(SESSION+flag,b'\x01')
 check(main(0x1002,dt=10)[0]==0 and main(dt=400)[0]==0,'Bank Select ignored while recording (flag +0x%x)'%flag)
 u.mem_write(SESSION+flag,b'\x00')
check(main(0x1003,dt=500)[0]==0 and main(dt=400)[0]==0,'unassigned Bank #: nothing')
def main_lsb(lsb,dt=0):
 w32(u,TR+0x1c,lsb);return main(dt=dt)
check(main_lsb(0x1001,dt=500)[0]==0 and main(dt=400)[0]==0,'Bank CC = MSB (default): CC 32 is ignored')
call(u,0x8120264,SETTINGS,0x28b,1)
check(main(0x1002,dt=500)[0]==0 and main(dt=400)[0]==0,'Bank CC = LSB: CC 0 is ignored')
check(main_lsb(0x1002,dt=500)[0]==0 and main(dt=400)==(1,0x18,2,0),'Bank CC = LSB: CC 32 = 2 loads Bank 3')
complete();call(u,0x8120264,SETTINGS,0x28b,0)
# MIDI channels (Tools > MIDI): 0 All, 1 None, 2..17 = channel 1..16.
call(u,0x8120264,SETTINGS,0x28f,1+5)
check(main(0x1302,dt=500)[0]==0 and main(dt=400)[0]==0,'PC Ch = 5: Bank Select on channel 4 ignored')
check(main(0x1402,dt=500)[0]==0 and main(dt=400)==(1,0x18,2,0),'PC Ch = 5: Bank Select on channel 5 loads')
complete()
call(u,0x8120264,SETTINGS,0x28f,1)
check(main(0x1402,dt=500)[0]==0 and main(dt=400)[0]==0,'PC Ch = None: every Bank Select ignored')
call(u,0x8120264,SETTINGS,0x28f,0)
call(u,0x8120264,SETTINGS,0x290,1)
check(main(0x1402,dt=500)[0]==0 and main(dt=400)==(1,0x18,2,0),'old Bank Ch setting (None) no longer filters')
complete();call(u,0x8120264,SETTINGS,0x290,0)
# Bank Select for the project already loaded: ignored (no reload, no delay).
curname[0]='My Song'
check(main(0x1002,dt=500)[0]==0 and u32(u,STATE+16)==0 and main(dt=400)[0]==0,'Bank Select for the loaded project (My Song) is dropped at once')
curname[0]='Alpha'
check(main(0x1002,dt=500)[0]==0 and main(dt=400)==(1,0x18,2,0),'from another project it loads')
complete();curname[0]=''

# Restart after a bank load: internal transport playing -> Play once the load is done.
def run_load(moving,ext=None):
 del plays[:]
 for _ in range(3):main(dt=20,moving=moving)
 w32(u,TR,clock[0]-ext if ext is not None else 0)
 main(0x1001,dt=10,moving=moving);r=main(dt=310,moving=moving)
 u.mem_write(LOADBUF,b'Beta\0');main(dt=20);main(dt=2000)
 before=len(plays);u.mem_write(LOADBUF,b'\0');main(dt=100);mid=len(plays);main(dt=250)
 return r[0],before,mid,list(plays)
r=run_load(True)
check(r[0]==1 and r[1]==0 and r[2]==0 and r[3]==[(ENGINE,0x7a,0x18,0)],'playing (internal clock): Play sent 300 ms after the bank load completes')
check(run_load(False)[3]==[],'stopped before: no Play')
check(run_load(True,ext=100)[3]==[],'external MIDI clock running: no Play (waits for the master Start)')
# MIDI realtime stub: replays the two stock instructions, stamps the UI clock.
v=machine();w32(v,NOW,0x1234);v.reg_write(UC_ARM_REG_SP,0x2407f000);v.reg_write(UC_ARM_REG_R3,0x55)
v.mem_write(0x2407f000,b'\xaa'*0x20)
hit=[]
def resume(v,a,n,_):
 if a==0x81033e6:hit.append(a);v.emu_stop()
v.hook_add(UC_HOOK_CODE,resume,begin=0x81033e6,end=0x81033e6)
v.emu_start(PC['pc_midi_realtime']|1,0,count=20)
check(hit and v.reg_read(UC_ARM_REG_R3)==0 and bytes(v.mem_read(0x2407f011,4))==bytes(4) and u32(v,TR)==0x1234,'realtime stub replays stock code, stamps the clock, resumes at 0x081033e6')

# After the Load event: return to the remembered page instead of MIXER.
pages=[];stub(u,0x8122508,lambda u:pages.append(u.reg_read(UC_ARM_REG_R1)))
def after(saved):w32(u,STATE+24,saved);call(u,PC['pc_after_load'],SESSION,2,0,0);return pages[-1],u32(u,STATE+24)
check(after(0x107)==(7,0),'after a PC load the previous page (7) is shown')
check(after(0)==(2,0),'a normal Load still shows MIXER')
check(after(0x10a)[0]==2 and after(0x11e)[0]==2,'keyboard/keypad overlay pages fall back to MIXER')

# Tools panel: PC slots never become rows.
rows=[];stub(u,0x8138546,lambda u:rows.append(u.reg_read(UC_ARM_REG_R1)))
for tag in (0x80,0x81,0x117,TAG0,TAG0+127,0x28b,0x28f,0x290,0x291,0x292,0x293,0x294,0x204):call(u,PC['pc_tools_append'],0x24070000,tag,0)
check(rows==[0x80,0x81,0x291,0x204],'Tools rows: Bank # slots, MIDI settings and MIDIStopRcv hidden; the MIDI row shown')


# ---------------------------------------------------------------- Scenes
APP=0xc0f00000;EDIT=APP+0x1e978;ROW=EDIT+0x9c8+0x99ec
# A booted machine (bank allocated by the parameter-definitions hook) with
# native tag tables for every cell, the M4 .data image, heap and C locale.
def scene_machine():
 u=machine();heap=[0xc0100000]
 def alloc(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,alloc)
 u.mem_write(0x30000000,bytes(u.mem_read(0x0814d230,0x400)))  # M4 .data image (startup copy)
 w32(u,0x30000088,0xc0800000);w32(u,0x3000008c,0xc0400000)    # main heap (XML library)
 call(u,0x8134cb8,0)
 for c in range(12):table_init(u,SESSION+0xc08+c*0x30,1)
 table_init(u,SESSION+0xe78,2)
 for i in range(1,7):table_init(u,SESSION+0xe78+i*0x10,3)
 table_init(u,SESSION+0xe48,4);table_init(u,SESSION+0xe58,5);table_init(u,SESSION+0xe68,8)
 w32(u,0x3000d9fc,APP);w32(u,EDIT,0x814ca10)
 stub(u,0x8122508,lambda u:u.mem_write(SESSION+0x739a,bytes([u.reg_read(UC_ARM_REG_R1)])))  # page switch
 stub(u,0x8123818,lambda u:None)
 stub(u,0x81224d4,lambda u:projsaves.append(u.reg_read(UC_ARM_REG_R0)))  # SessionMgr::Save
 # the edit page's bottom button (view+0x888): label and rect calls recorded
 w32(u,EDIT+0x888,BVT);w32(u,BVT+0x20,0x8137d04|1)
 stub(u,0x813820c,lambda u:button.append(('label',string(u,u.reg_read(UC_ARM_REG_R1)).decode())))
 stub(u,0x8137d04,lambda u:button.append(('rect',struct.unpack('<4i',u.mem_read(u.reg_read(UC_ARM_REG_R1),16)))))
 return u
projsaves=[];button=[];BVT=0x24017000
def table_init(u,addr,kind):
 u.mem_write(0x24010000,bytes([kind]));call(u,0x81202b8,addr,0x24010000);w32(u,addr,0x814ae3c)
def taddr(cell):
 return SESSION+(0xc08+cell*0x30 if cell<12 else 0xe78+(cell-0x0c)*0x10 if cell<=0x12 else 0xe48+(cell-0x14)*0x10)
SB=lambda u:u32(u,0x38800fd4)
def items(u):
 b=SB(u);n=u32(u,b+4)
 return [(u.mem_read(b+0x20+4*j,1)[0],struct.unpack('<H',u.mem_read(b+0x22+4*j,2))[0]) for j in range(n)]
def tget(u,cell,id):return call(u,0x8120234,taddr(cell),id)&0xffffffff
def tset(u,cell,id,v):call(u,0x8120264,taddr(cell),id,v&0xffffffff)
u=scene_machine()
check(u32(u,0x38800fd0)==0x53434e31 and SB(u)>=0xc0100000,'scene bank allocated once at boot (parameter-definitions hook)')
TRACK={0x4,0x3,0x63,0xda,0xdb,0x15d,0x15e,0x15f,0x160,0x161,0x163,0x1ae}|set(range(0x119,0x130))
MASTER={0x4,0x160,0xb5,0x43,0x1b6,0x1b7,0x1b8}|set(range(0x130,0x13c))|set(range(0x119,0x130))

# Scene page actions: A on PROJ opens it; A on the page acts on the selected row.
B0=SB(u)
FIELDS=0x20+640*4+16*640*4+64*8+64*2
def field(i):return SB(u)+FIELDS+4*i
def used(u):return u32(u,SB(u)+8)
def current(u):return u32(u,SB(u)+12)
def val(u,k,j):return u32(u,SB(u)+0x20+640*4+(k*640+j)*4)
def page(p):u.mem_write(SESSION+0x739a,bytes([p]))
def press_a():call(u,PC['pc_button_a'],SESSION,0)
def select(id=0x2a0):u.mem_write(0x24011400,struct.pack('<HHIHHIII',2,0,0x8148664,0,0,id,0,0));return call(u,PC['pc_edit_event'],EDIT,0x24011400)
def act(row,value=None):
 page(0x19);w32(u,field(4),1);w32(u,ROW,row)
 if value is not None:w32(u,field(row),value)
 select()
def save(n):act(1,n)
tset(u,0,0x4,-6000);tset(u,3,0x160,1);tset(u,0x0c,0x132,-18000);tset(u,0x14,0x33,250)
save(1)
it=items(u)
check(used(u)==1 and current(u)==1 and len(it)>400,'Save Scene 1 builds the template (%d values) and snapshots it'%len(it))
check(all(id in TRACK for c,id in it if c<12),'tracks: only level/pregain/pan/sends/mute/solo/comp/EQ/gate')
check(not any(id in (0x112,0x162,0x16d,0x16e) or 0x1f<=id<=0x2b for c,id in it if c<12),'tracks: no source, rec-enable, play, asset or ducking')
check(all(id in MASTER for c,id in it if c==0x0c),'master: compressor/EQ/level/mute/saturation only (no routing/USB)')
check(all(id in (0x4,0x160) for c,id in it if 0x0d<=c<=0x12),'aux buses: level and mute only')
fx1=[id for c,id in it if c==0x14]
check(fx1 and fx1[0]==0x1a0 and 0x33 in fx1,'FX1: every delay parameter, mode (0x1a0) first')
fx2=[id for c,id in it if c==0x15]
check(fx2 and fx2[0]==0x155,'FX2: reverb style (0x155) first')
check([id for c,id in it if c==0x16]==[0x85],'project settings (cell 0x16): BPM only, no metronome/transport/clock')
check(not any(c==0x13 or c>0x16 for c,_ in it),'no other cells')
j=it.index((0,0x4));check(val(u,0,j)==(-6000)&0xffffffff,'snapshot holds the live track 1 level')
tset(u,0,0x4,-1000);tset(u,3,0x160,0);tset(u,0x0c,0x132,-6000);tset(u,0x14,0x33,500);tset(u,0x14,0x1a0,2);tset(u,0x16,0x85,90)
save(2)
check(used(u)==3 and current(u)==2,'Save Scene 2 (scene page row 2 + Select)')

# Recall: changed values only, through the knob path, at most 24 per pass, FX mode first.
knob=[]
def knobpath(u):
 cell=struct.unpack('<H',u.mem_read(u.reg_read(UC_ARM_REG_R1),2))[0];id=u.reg_read(UC_ARM_REG_R2);v=u.reg_read(UC_ARM_REG_R3)
 knob.append((u.reg_read(UC_ARM_REG_R0),cell,id,v))
 # apply like the native handler would (table set)
 base=u32(u,taddr(cell)+4);n=struct.unpack('<H',u.mem_read(taddr(cell)+10,2))[0]
 for i in range(n):
  if struct.unpack('<H',u.mem_read(base+8*i,2))[0]==id:w32(u,base+8*i+4,v)
stub(u,0x8124b44,knobpath)
stub(u,0x8100730,lambda u:None)
LOADNAME=0x3001734c;LOADBUF=0x24012000;w32(u,LOADNAME,LOADBUF);u.mem_write(LOADBUF,b'\0')
clock=[5000]
def mainpass(program=0,bank=0,dt=5):
 clock[0]+=dt;w32(u,0x3001eb3c,clock[0]);w32(u,STATE+4,program);w32(u,TR+0x18,bank)
 n=len(knob);call(u,PC['pc_main'],0x30000180);return len(knob)-n
for _ in range(3):
 for c in range(12):tset(u,c,0x63,77)  # extra differences: pan on all tracks
act(0,1)
passes=[]
while True:
 k=mainpass()
 passes.append(k)
 if k==0 or len(passes)>20:break
calls=knob[:]
check(sum(passes)==len(calls) and max(passes)<=24 and len(passes)>1,'Scene 1 recall replays %d changed values over %d passes (<=24 each)'%(len(calls),len(passes)-1))
check(set((c,id) for _,c,id,_ in calls)>={(0,0x4),(3,0x160),(0x0c,0x132),(0x14,0x33),(0x14,0x1a0)} and all(s==SESSION for s,_,_,_ in calls),'recall sends only differing values, via SessionMgr')
fxcalls=[id for _,c,id,_ in calls if c==0x14]
check(fxcalls.index(0x1a0)<fxcalls.index(0x33),'FX1 mode replayed before its parameters')
check(tget(u,0,0x4)==(-6000)&0xffffffff and tget(u,3,0x160)==1 and tget(u,0x0c,0x132)==(-18000)&0xffffffff and tget(u,0x14,0x33)==250 and tget(u,0x16,0x85)==120,'live state equals scene 1 after recall (BPM back to 120)')
check(current(u)==1 and all(tget(u,c,0x63)==val(u,0,items(u).index((c,0x63))) for c in range(12)),'current scene = 1; every captured value restored')
del knob[:];check(mainpass()==0 and mainpass()==0,'nothing left to replay')

# Program Change recalls scene n+1, immediately; empty scenes and n>16 ignored.
del knob[:];mainpass(program=0x1001)
while mainpass():pass
check(current(u)==2 and tget(u,0x14,0x1a0)==2 and tget(u,0,0x4)==(-1000)&0xffffffff,'Program Change 1 recalls scene 2')
del knob[:];mainpass(program=0x1005);mainpass();check(knob==[] and current(u)==2,'Program Change for an empty scene does nothing')
mainpass(program=0x1010);mainpass();check(knob==[] and current(u)==2,'Program Change 16+ (scene 17+) is ignored')
u.mem_write(SESSION+0x73d7,b'\x01');del knob[:]
mainpass(program=0x1000)
while mainpass():pass
check(current(u)==1 and knob,'scenes recall while recording (no load involved)')
u.mem_write(SESSION+0x73d7,b'\x00')

# Deferral: a scene request waits while a load runs or a Bank Select settles.
del knob[:];u.mem_write(LOADBUF,b'Busy\0')
mainpass(program=0x1001);mainpass();check(knob==[],'Program Change during a load waits')
u.mem_write(LOADBUF,b'\0')
while mainpass():pass
check(current(u)==2 and knob,'and recalls once the load is done')
# Bank Select + Program Change: the scene waits for the project load.
call(u,0x8120a52,SETTINGS)
call(u,0x8120264,SETTINGS,TAG0+5,fnv('Other'))
names=['Mine','Other'];stub(u,0x810204e,listname)
curname=['Mine'];stub(u,0x8122458,lambda u:u.mem_write(u.reg_read(UC_ARM_REG_R1),curname[0].encode()+b'\0'))
del knob[:];mainpass(bank=0x1005,program=0x1000);mainpass(dt=100)
check(knob==[],'Bank Select (another project) + Program Change: scene waits while the bank settles')
for _ in range(8):mainpass(dt=50)
u.mem_write(LOADBUF,b'Other\0');mainpass(dt=20);check(knob==[],'and while the project loads')
u.mem_write(LOADBUF,b'\0');curname[0]='Other';mainpass(dt=50);check(knob==[],'and for 300 ms after it')
for _ in range(10):mainpass(dt=50)
check(current(u)==1,'then the scene is recalled in the loaded project')
del knob[:];mainpass(bank=0x1005,program=0x1001)
while mainpass():pass
check(current(u)==2,'Bank Select for the loaded project + Program Change: scene recalled at once')
call(u,0x8120264,SETTINGS,0x28f,1+3)
del knob[:];mainpass(program=0x1100);mainpass()
check(current(u)==2 and knob==[],'PC Ch = 3: Program Change on channel 2 ignored')
mainpass(program=0x1200)
while mainpass():pass
check(current(u)==1,'PC Ch = 3: Program Change on channel 3 recalls')
call(u,0x8120264,SETTINGS,0x28f,0)

# Project XML: <scenes> written by the project-writer hook, read back by the reader hook.
DOC=0x24060000;STR=[0x24070000]
def s(txt):
 a=STR[0];u.mem_write(a,txt+b'\0');STR[0]+=0x40;return a
def el(n):return call(u,u32(u,u32(u,n)),n) if n else 0
call(u,0x81078c0,DOC,1,0)
docel=el(call(u,0x8106c92,DOC,call(u,0x8107b16,DOC,s(b'document'))))
sess=el(call(u,0x8106c92,docel,call(u,0x8107b16,DOC,s(b'session'))))
saved1=[val(u,0,j) for j in range(len(items(u)))];saved2=[val(u,1,j) for j in range(len(items(u)))]
cv=call(u,0x8107b16,DOC,s(b'cvmap'))
r=call(u,PC['sc_write'],sess,cv,DOC)
check(r==cv,'writer hook still inserts <cvmap> and returns it')
scenes=call(u,0x8106ae8,sess,s(b'scenes'))
check(scenes!=0 and call(u,0x8106ae8,scenes,s(b'scene'))!=0,'<scenes><scene> written under <session>')
freed=[]
stub(u,0x81079c0,lambda u:freed.append(u.reg_read(UC_ARM_REG_R0)))
for k in (0,1):
 for j in range(len(saved1)):w32(u,SB(u)+0x20+640*4+(k*640+j)*4,0)
w32(u,SB(u)+8,0);w32(u,SB(u)+12,0);w32(u,SB(u)+24,0x103)
call(u,PC['sc_read'],DOC)
check(freed==[DOC],'reader hook frees the document (stock behaviour)')
check(used(u)==3 and current(u)==1,'scenes 1-2 and the current scene restored from the project XML')
check([val(u,0,j) for j in range(len(saved1))]==saved1 and [val(u,1,j) for j in range(len(saved2))]==saved2,'all %d values of both scenes round-trip exactly'%len(saved1))
check(u32(u,SB(u)+24)==0x103,'a pending scene request survives a project load')
# A project without <scenes> clears them.
DOC2=0x24064000;call(u,0x81078c0,DOC2,1,0)
d2=el(call(u,0x8106c92,DOC2,call(u,0x8107b16,DOC2,s(b'document'))));el(call(u,0x8106c92,d2,call(u,0x8107b16,DOC2,s(b'session'))))
call(u,PC['sc_read'],DOC2)
check(used(u)==0 and current(u)==0,'loading a project without scenes clears them')
s3=el(call(u,0x8106ae8,DOC2,s(b'document')));s3=call(u,0x8106ae8,s3,s(b'session'))
cv2=call(u,0x8107b16,DOC2,s(b'cvmap'));call(u,PC['sc_write'],s3,cv2,DOC2)
check(call(u,0x8106ae8,s3,s(b'scenes'))==0 and call(u,0x8106ae8,s3,s(b'cvmap'))!=0,'writer with no scenes adds no <scenes> (cvmap still written)')

# ---------------------------------------------------------------- s3 pages
# Scene page: A on PROJ -> page 0x19 shown with cell 0x17; rows, edits, actions.
BROWSER=APP+0x28d60;w32(u,BROWSER,0x814cb14);w32(u,BROWSER+0x614,0)
names=['Mine','Other'];curname[0]='Mine'
save(1);save(2)  # the XML test above cleared the scenes (project without <scenes>)
page(0x18);w32(u,field(4),0)
press_a()
check(u.mem_read(SESSION+0x739a,1)[0]==0x19 and u32(u,field(4))==1,'A on PROJ opens the scene page (page 0x19, scene flag set)')
check([u32(u,field(i)) for i in range(3)]==[current(u),3,0],'rows start at: current scene, next free slot (3), Bank # of the selected project (none)')
posted=[]
stub(u,0x813c384,lambda u:posted.append(struct.unpack('<HHIHHIII',u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
u.mem_write(0x24011000,struct.pack('<HHIHHIII',0xab,0,0x8148664,0x16,0,0,0,0))
call(u,PC['pc_projset_post'],SESSION,0x24011000)
check(posted[-1][0]==0xab and posted[-1][3]==0x17,'page 0x19 shows cell 0x17 while the scene page is open')
w32(u,field(4),0);u.mem_write(0x24011000,struct.pack('<HHIHHIII',0xab,0,0x8148664,0x16,0,0,0,0));call(u,PC['pc_projset_post'],SESSION,0x24011000)
check(posted[-1][3]==0x16,'B on PROJ (no flag): project settings (cell 0x16) as stock')
LIST=0x24020000;rowsl=[]
stub(u,0x8138546,lambda u:rowsl.append((u.reg_read(UC_ARM_REG_R1),u.reg_read(UC_ARM_REG_R2))))
def page_rows(cell):
 del rowsl[:];u.mem_write(0x24011800,struct.pack('<H',cell));w32(u,LIST,99);r=call(u,PC['pc_page_rows'],SESSION,0x24011800,LIST);return r,u32(u,LIST),list(rowsl)
w32(u,field(0),2);w32(u,field(1),3);w32(u,field(2),6)
del button[:]
check(page_rows(0x17)==(1,0,[(0x28c,2),(0x28d,3),(0x28e,6)]),'scene page rows: Scene 2, Save to 3, Bank # 6')
check(button==[('label','Select'),('rect',(226,182,93,38))] and u32(u,EDIT+0x888+0x14)==0x2a0,'scene page: bottom button labelled "Select" at the right (id 0x2a0)')
call(u,0x8120264,SETTINGS,0x28b,1);call(u,0x8120264,SETTINGS,0x28f,3)
check(page_rows(0x1b)[2]==[(0x28b,1),(0x28f,3),(0x117,1),(0x292,0),(0x293,1),(0x294,0)],'MIDI page rows: Bank CC, PC Ch, MIDIStopRcv (stock default on), MIDI Out, Map CC Sync (USB), Clock Src')
call(u,0x8120264,SETTINGS,0x28b,0);call(u,0x8120264,SETTINGS,0x28f,0)
built=[];stub(u,0x8121cf4,lambda u:(built.append(struct.unpack('<H',u.mem_read(u.reg_read(UC_ARM_REG_R1),2))[0]),u.reg_write(UC_ARM_REG_R0,7)))
del button[:]
check(page_rows(0x16)[0]==7 and built==[0x16],'other cells use the stock builder')
check(button==[('label',''),('rect',(0,182,320,38))] and u32(u,EDIT+0x888+0x14)==0,'other edit pages: the button is reset to stock (empty, full width, id 0)')
knobs=[];saves=[]
stub(u,0x8124b44,lambda u:knobs.append((struct.unpack('<H',u.mem_read(u.reg_read(UC_ARM_REG_R1),2))[0],u.reg_read(UC_ARM_REG_R2),u.reg_read(UC_ARM_REG_R3))))
stub(u,0x811e920,lambda u:saves.append(1))
def edit(cell,id,v):u.mem_write(0x24011800,struct.pack('<H',cell));call(u,PC['pc_knob'],SESSION,0x24011800,id,v&0xffffffff,0)
edit(0x17,0x28c,5);edit(0x17,0x28d,40);edit(0x17,0x28e,-3)
check([u32(u,field(i)) for i in range(3)]==[5,16,0] and knobs==[],'scene page edits only change the selections (clamped 1-16 / 0-128), nothing reaches the engine')
edit(0x1b,0x28f,4)
check(knobs==[(0x18,0x28f,4)] and saves,'MIDI page edits go to the settings (cell 0x18) and save settings.tml')
del knobs[:];edit(3,0x4,-100)
check(knobs==[(3,0x4,(-100)&0xffffffff)],'other edits pass through to the knob path')
toolsev=[];stub(u,0x8131210,lambda u:(toolsev.append(1),u.reg_write(UC_ARM_REG_R0,5)))
def tools(ev,id):u.mem_write(0x24011000,struct.pack('<HHIHHIII',ev,0,0x8148664,0,0,1,id,0));return call(u,PC['pc_tools_event'],0x24030000,0x24011000)
del posted[:]
check(tools(0x3d,0x291)==0 and not posted and not toolsev,'turning the Tools "MIDI:" row does nothing (no value, no page)')
# A tap on a row: list widget event 0x145 with the row index.
LW=0xc1100000;taps=[]
stub(u,0x813d89c,lambda u:(taps.append(u.reg_read(UC_ARM_REG_R0)),u.reg_write(UC_ARM_REG_R0,0)))
w32(u,LW+0x99e8,3)
for i,id in enumerate((0x80,0x81,0x291)):u.mem_write(LW+0xe8+i*0x330+0x32c,struct.pack('<H',id))
def tap(ev,row):u.mem_write(0x24011000,struct.pack('<HHIHHIII',ev,0,0x8148664,0,0,row,0,0));return call(u,PC['pc_list_event'],LW,0x24011000)
del posted[:]
check(tap(0x145,2)==0 and taps==[LW] and posted and posted[-1][0]==0xab and posted[-1][3]==0x1b,'tapping the MIDI row: stock selection, then the MIDI page (cell 0x1b) opens')
del posted[:]
check(tap(0x145,0)==0 and not posted,'tapping another row (Brightness) only selects it')
check(tap(0x145,5)==0 and not posted and tap(0x144,2)==0 and not posted,'out-of-range rows and other list events: stock only')
check(tools(0x3c,0x80)==5 and toolsev,'other Tools rows behave as stock')
# Bank # row: assign the number to the project selected in PROJ.
w32(u,BROWSER+0x614,1);page(0x18);press_a()
check(u32(u,field(2))==6 and u32(u,field(3))==fnv('Other'),'Bank # row starts at the selected project\'s number (Other = 6)')
act(2,9)
check(call(u,0x8120234,SETTINGS,TAG0+8)==fnv('Other') and call(u,0x8120234,SETTINGS,TAG0+5)==0,'A on the Bank # row moves Other to Bank 9')
act(2,0)
check(all(call(u,0x8120234,SETTINGS,TAG0+i)!=fnv('Other') for i in range(SLOTS)),'Bank # 0 clears it')
TOAST=lambda:u32(u,field(8))
call(u,0x8120264,SETTINGS,TAG0+3,fnv('Third'))
act(2,4)
check(call(u,0x8120234,SETTINGS,TAG0+3)==fnv('Third') and all(call(u,0x8120234,SETTINGS,TAG0+i)!=fnv('Other') for i in range(SLOTS)) and TOAST()==(7<<8)|4,'Select on a Bank # another project has: refused, "BANK 4 IN USE"')
w32(u,0x3001eb3c,50000);w32(u,field(9),0);w32(u,field(8),0)
edit(0x17,0x28e,4)
check(TOAST()==(7<<8)|4 and u32(u,field(2))==4,'turning onto a used number: "BANK 4 IN USE" shows at once')
edit(0x17,0x28e,5)
check(u32(u,field(9))==50000,'turning on to a free number: the warning is cleared')
call(u,0x8120264,SETTINGS,TAG0+3,0)
act(0,9);check(TOAST()==(2<<8)|9,'recalling an empty scene shows "SCENE 9 IS EMPTY"')
act(0,1);check(TOAST()==(1<<8)|1,'recall shows "SCENE 1"')
page(0x02);w32(u,field(4),1);n=current(u);press_a()
check(current(u)==n and u32(u,field(4))==1,'A on other pages does nothing new')
page(0x19);w32(u,field(4),1);w32(u,ROW,1);w32(u,field(1),5);used1=used(u);press_a()
check(u.mem_read(SESSION+0x739a,1)[0]==0x18 and u32(u,field(4))==0 and used(u)==used1,'A on the scene page leaves it (back to PROJ), no action')
stockev=[];stub(u,0x813cfb8,lambda u:(stockev.append(1),u.reg_write(UC_ARM_REG_R0,1)))
page(0x19);w32(u,field(4),0);w32(u,ROW,1)
check(select()==0 and used(u)==used1 and not stockev,'Select on project settings (not the scene page): no action')
check(select(3)==1 and stockev,'other edit page events go to the stock handler')
# Scene saves request a project save (SessionMgr::Save) in the main loop.
del projsaves[:];w32(u,field(10),0);act(1,5)
check(u32(u,field(10))==1 and not projsaves,'Select on Save to: project save requested')
u.mem_write(SESSION+0x73d7,b'\x01');mainpass()
check(not projsaves and u32(u,field(10))==1,'deferred while recording')
u.mem_write(SESSION+0x73d7,b'\x00');mainpass()
check(projsaves==[SESSION] and u32(u,field(10))==0,'then SessionMgr::Save runs once')
mainpass();check(len(projsaves)==1,'only once')
# PROJ Save assigns the chosen Bank # to the loaded project; None keeps it.
def proj_save():del projsaves[:];call(u,PC['pc_project_save'],SESSION);return list(projsaves)
curname[0]='Mine';w32(u,field(3),fnv('Mine'));w32(u,field(2),12)
check(proj_save()==[SESSION] and call(u,0x8120234,SETTINGS,TAG0+11)==fnv('Mine') and TOAST()==(5<<8)|12,'PROJ Save with Bank # 12 chosen: Mine gets Bank 12 ("BANK 12"), then the stock save')
w32(u,field(2),0)
check(proj_save()==[SESSION] and call(u,0x8120234,SETTINGS,TAG0+11)==fnv('Mine'),'Bank # None: Mine keeps Bank 12')
w32(u,field(3),fnv('Other'));w32(u,field(2),3)
check(proj_save()==[SESSION] and call(u,0x8120234,SETTINGS,TAG0+2)!=fnv('Other'),'Bank # chosen for another project (not loaded): not applied')
call(u,0x8120264,SETTINGS,TAG0+0,fnv('Other'));w32(u,field(3),fnv('Mine'));w32(u,field(2),1)
check(proj_save()==[SESSION] and call(u,0x8120234,SETTINGS,TAG0+0)==fnv('Other') and call(u,0x8120234,SETTINGS,TAG0+11)==fnv('Mine') and TOAST()==(7<<8)|1,'Bank 1 already used by Other: not taken on save, Mine keeps 12, "BANK 1 IN USE"')
call(u,0x8120264,SETTINGS,TAG0+0,0)
w32(u,field(3),0);w32(u,field(2),0)
# B on Tools opens the MIDI page; B elsewhere is stock.
bcalls=[];stub(u,0x8123818,lambda u:bcalls.append(u.reg_read(UC_ARM_REG_R1)))
TL=APP+0x119a88+0x590;w32(u,TL+0x99e8,3)
for i,id in enumerate((0x80,0x81,0x291)):u.mem_write(TL+0xe8+i*0x330+0x32c,struct.pack('<H',id))
def press_b():del posted[:];call(u,PC['pc_button_b'],SESSION,0);return list(posted)
w32(u,APP+0x2b4,APP+0x119a88);w32(u,TL+0x99ec,0)
check(press_b()==[] and bcalls==[0],'B on Tools with Brightness selected: stock only')
w32(u,TL+0x99ec,2);p=press_b()
check(len(bcalls)==2 and p and p[-1][0]==0xab and p[-1][3]==0x1b,'B on Tools with "MIDI Settings" selected: the MIDI page (cell 0x1b) opens')
w32(u,APP+0x2b4,EDIT);u.mem_write(EDIT+0xa3e0,struct.pack('<H',0x1b));p=press_b()
check(len(bcalls)==3 and p and p[-1][0]==0xcb,'B on the MIDI page: back to Tools (UI event 0xcb)')
u.mem_write(EDIT+0xa3e0,struct.pack('<H',0x16))
check(press_b()==[] and len(bcalls)==4,'B on other edit pages: stock only')
# PROJ list: the loaded project's row gets "* ".
LB=0x24013000
def row(i):u.mem_write(LB,bytes(64));r=call(u,PC['pc_list_row'],0x300172fc,i,LB);return r,string(u,LB)
curname[0]='Other'
check(row(1)==(1,b'* Other') and row(0)==(1,b'Mine') and row(5)==(0,b''),'PROJ list: "* " marks the loaded project only')
curname[0]='Mine'

# Quick save: hold A for 2 s on MAIN (page 8) saves to the next empty scene.
# Button table as the stock init (0x081047a0) sets it up: A is entry 12.
BT=0x30009d80
call(u,0x81047a0)
check([struct.unpack('<H',u.mem_read(BT+24*k+8,2))[0] for k in range(13)]==[0xf9]*8+[0xf4,0xf6,0xf7,7,0x0c],'stock button table: track keys, REC, PLAY, STOP, B, A (entry 12)')
for k in range(13):u.mem_write(BT+24*k+0xe,b'\x01')  # idle = high
w32(u,field(5),0)
def hold(level):u.mem_write(BT+24*12+0xe,bytes([0 if level else 1]))
page(8);used0=used(u);nxt=[k for k in range(16) if not used0>>k&1][0]
hold(1);mainpass(dt=10);mainpass(dt=1000);mainpass(dt=900)
check(used(u)==used0,'held 1.9 s: nothing saved yet')
mainpass(dt=200)
check(used(u)==used0|1<<nxt and current(u)==nxt+1 and TOAST()==(3<<8)|(nxt+1),'held 2 s on MAIN: saved to the next empty scene (%d), "SCENE %d SAVED"'%(nxt+1,nxt+1))
DIRTY=APP+0x280+0x39
u.mem_write(DIRTY,b'\x00');mainpass(dt=50)
check(u.mem_read(DIRTY,1)[0]==1,'while the band shows, the UI layer is marked dirty so the flip draws it')
u.mem_write(DIRTY,b'\x00');mainpass(dt=10)
check(u.mem_read(DIRTY,1)[0]==0,'at most every 40 ms')
mainpass(dt=4000);check(used(u)==used0|1<<nxt,'keeping it held saves only once')
check(u.mem_read(DIRTY,1)[0]==1,'once the message has expired: one more redraw clears the band')
u.mem_write(DIRTY,b'\x00');mainpass(dt=100);check(u.mem_read(DIRTY,1)[0]==0,'then no more redraws')
hold(0);mainpass(dt=10);page(2);hold(1);mainpass(dt=10);mainpass(dt=4000)
check(used(u)==used0|1<<nxt,'holding A on another page (MIXER) saves nothing')
hold(0);mainpass(dt=10);page(8)
w32(u,SB(u)+8,0xffff);hold(1);mainpass(dt=10);mainpass(dt=2100)
check(TOAST()==(4<<8),'all 16 slots used: "NO SCENE SLOTS", nothing saved')
hold(0);mainpass(dt=10);w32(u,SB(u)+8,used0|1<<nxt)

# New project (PROJ File > New, or the first one on an empty card): the
# scenes are cleared, so Save Scene and quick save start at scene 1.
newp=[];stub(u,0x8122e3c,lambda u:newp.append(string(u,u.reg_read(UC_ARM_REG_R1))))
keep=used(u);u.mem_write(0x24011c00,b'New Project\0');call(u,PC['pc_new_project'],SESSION,0x24011c00)
check(newp==[b'New Project'] and used(u)==0 and current(u)==0,'New project: the stock create runs and the scenes are cleared')
page(0x18);w32(u,field(4),0);press_a()
check(u32(u,field(0))==1 and u32(u,field(1))==1,'scene page on the new project: Load Scene 1, Save Scene 1')
w32(u,field(4),0);page(8);hold(1);mainpass(dt=10);mainpass(dt=3100);hold(0);mainpass(dt=10)
check(used(u)==1 and current(u)==1 and TOAST()==(3<<8)|1,'quick save on the new project: scene 1')
w32(u,SB(u)+8,keep)

# On-screen band, drawn before Cobalt's flip hook (cpu_present).
FB=0xc1000000;DESC=0x24016000
u.mem_write(DESC,struct.pack('<IHHBBH',FB,320,240,4,0,2))
stub(u,0x810139a,lambda u:u.reg_write(UC_ARM_REG_R0,DESC))
flips=[];stub(u,M['symbols']['4']['cpu_present'],lambda u:flips.append(u.reg_read(UC_ARM_REG_R1)))
def present(layer):u.mem_write(FB,bytes(320*14*2));call(u,PC['pc_present'],0x30001d80,layer,0,0);return bytes(u.mem_read(FB,320*12*2))
w32(u,field(9),clock[0]+1000);w32(u,0x3001eb3c,clock[0])
img=present(1)
check(flips==[1] and img.count(b'\xff\xff')>20 and img[:2]!=b'\x00\x00','message band and text drawn on layer 1, then Cobalt/stock flip')
check(present(0)==bytes(320*12*2) and flips[-1]==0,'other layers untouched')
# Scene page shown: project name in the strip at the top left.
def strip():u.mem_write(FB,bytes(320*70*2));call(u,PC['pc_present'],0x30001d80,0,0,0);return bytes(u.mem_read(FB+320*24*2,320*38*2))
page(0x19);w32(u,field(4),1);w32(u,APP+0x2b4,EDIT);u.mem_write(SB(u)+FIELDS+13*4,b'My Song\0')
img=strip();lit=[i//2%320 for i in range(0,len(img),2) if img[i:i+2]!=b'\0\0']
check(lit and min(lit)>=8 and max(lit)<226,'scene page: project name drawn at 2x in the strip, left of Select (x %d-%d)'%(min(lit),max(lit)))
up=strip();u.mem_write(SB(u)+FIELDS+13*4,b'my song\0');low=strip()
check(low!=up and low.count(b'\xff\xff')>40,'lower case drawn as lower case (not upper case)')
u.mem_write(SB(u)+FIELDS+13*4,b'gy\0');img=strip();rows=[i//2//320 for i in range(0,len(img),2) if img[i:i+2]!=b'\0\0']
check(rows and max(rows)>=26 and max(rows)<38,'g and y descend below the line, inside the strip (rows to %d)'%max(rows))
u.mem_write(SB(u)+FIELDS+13*4,b'A Very Long Project Nam\0');img=strip();lit=[i//2%320 for i in range(0,len(img),2) if img[i:i+2]!=b'\0\0']
check(lit and max(lit)<226,'23 characters: drawn at 1x, still left of Select')
w32(u,APP+0x2b4,APP+0x119a88);check(strip()==bytes(320*38*2),'other views: no name')
w32(u,field(4),0)
w32(u,0x3001eb3c,clock[0]+2000)
n=len(flips);check(present(1)==bytes(320*12*2) and len(flips)==n+1,'message expires after its time; the flip still runs')
hold(1);w32(u,field(6),clock[0]+1000);w32(u,field(7),0);w32(u,0x3001eb3c,clock[0]+2000)
img=present(1);bar=img[320*2*10:320*2*11]
filled=sum(1 for i in range(0,len(bar),2) if bar[i:i+2]!=struct.pack('<H',0x1082))
check(150<=filled<=170,'holding A for 1 s: progress bar half full (%d px)'%filled)
hold(0)

# PROJ list: a selected name longer than 23 characters scrolls in the middle row.
rowtext=[];MID=BROWSER+0x3ac+3*0x58
stub(u,0x813fe4a,lambda u:rowtext.append((u.reg_read(UC_ARM_REG_R0),string(u,u.reg_read(UC_ARM_REG_R1)).decode())))
LONG='Scenes Live Set October 28b';names=['Short',LONG];curname[0]='Short'
def scroll(dt):u.mem_write(BROWSER+0x6e5,b'\0');del rowtext[:];mainpass(dt=dt);return list(rowtext),u.mem_read(BROWSER+0x6e5,1)[0]
page(0x18);w32(u,APP+0x2b4,BROWSER);w32(u,BROWSER+0x614,1);scroll(10)
check(scroll(1000)==([],0),'long name: held at the start')
check(scroll(600)==([(MID,LONG[1:])],1),'after 1.5 s it moves one character (middle row, browser marked dirty)')
steps=[scroll(260)[0] for _ in range(len(LONG)-24)]
check(steps==[[(MID,LONG[i:])] for i in range(2,len(LONG)-22)],'then one character every 250 ms up to the end (%d steps)'%(len(LONG)-23))
check(scroll(260)==([],0) and scroll(1000)==([],0),'held at the end')
check(scroll(300)==([(MID,LONG)],1),'then back to the start')
scroll(1600);w32(u,BROWSER+0x614,0)
check(scroll(10)==([(MID,'* Short')],1),'another selection: the start of the row text is put back')
check(scroll(5000)==([],0),'a name that fits never scrolls')
w32(u,BROWSER+0x614,1);scroll(10);scroll(1600);page(8)
check(scroll(10)==([(MID,LONG)],1),'leaving the list puts the start back')
check(scroll(5000)==([],0),'and nothing more off the list')

print(H.checks,'scenes checks passed')
