"""S9: the synth/insert experiment on Cobalt v1.1.0 (scenes and MIDI).
Checks the places where the two lines meet: code layout, the chained flip and
parameter-definition hooks, scenes leaving synth/insert settings alone, and
one image carrying both sets of definitions. Real native code under Unicorn;
only allocation, the stock flip, the IPC send and the display are stubbed."""
from firmware_harness import *
import firmware_harness as H
S=M['symbols']['4'];PC=M['symbols']['pcm4']
BASE=0x08040000
def at(image,addr,n):return image[addr-BASE:addr-BASE+n]
# Layout: the PC map payload has its own cave, Cobalt's cm4 keeps 32 KB.
pc_code=(ROOT/'build/pcm4.bin').read_bytes();cm4=(ROOT/'build/cm4.bin').read_bytes()
check(at(patched,0x08090000,len(pc_code))==pc_code and at(stock,0x08090000,0x8000)==bytes(0x8000),'PC map code at 0x08090000, in flash that is zero in the stock image')
check(len(cm4)<=0x8000 and at(patched,0x080b8000,len(cm4))==cm4,'Cobalt cm4 (with the synth/insert UI, %d bytes) fits its 0x8000 cave'%len(cm4))
check(len(cm4)>0x4000,'cm4 is larger than the 16 KB v1.1.0 left it before 0x080BC000 (why the PC map moved)')
code=[v for k,v in PC.items() if k not in ('cpu_present','sat_defs') and not k.startswith('*ABS*')]
check(all(0x08090000<=v<0x08098000 for v in code),'every PC map symbol is inside its new cave')
# Both chained hooks reach Cobalt's cm4 functions, where S8 added its work.
check(PC['cpu_present']==S['cpu_present'] and PC['sat_defs']==S['sat_defs'],'PC map chains to the cm4 cpu_present (synth resync, CPU number) and sat_defs (synth/insert definitions)')
names={}
for p in M['patches']:names.setdefault(p['offset'],[]).append(p['name'])
check(names[0x08137496-BASE]==['Cue icons and Scenes feedback before Cobalt flip'] and names[0x08135fec-BASE]==['parameter definitions final call'],'one patch each on the flip call and the definitions call')
check(len(set(p['offset'] for p in M['patches']))==len(M['patches']),'no two patches share an address')
# Executed: the flip hook's PC map entry runs Cobalt's cpu_present, whose
# synth resync re-sends a saved synth track after boot.
APP=0x300173d0;TMP=0x24008000;IPC=0x3000a894;UI_NOW=0x3001eb3c
u=machine();heap=[0xc0010000]
def allocate(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate);call(u,0x8134cb8,0);call(u,0x8120abc)
for ch in range(12):
 u.mem_write(TMP,b'\1');call(u,0x81202b8,APP+0xc08+ch*0x30,TMP)
call(u,0x8120264,APP+0xc08+4*0x30,0x1bf,1);call(u,0x8120264,APP+0xc08+4*0x30,0x1c0,3)
commands=[];stub(u,0x8108784,lambda q:commands.append(tuple(q.reg_read(r) for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))))
flips=[];stub(u,0x810134c,lambda q:flips.append(q.reg_read(UC_ARM_REG_R1)))
FB=0xc1000000;DESC=0x24016000
u.mem_write(DESC,struct.pack('<IHHBBH',FB,320,240,4,0,2));stub(u,0x810139a,lambda q:q.reg_write(UC_ARM_REG_R0,DESC))
w32(u,IPC,0x30040000);w32(u,UI_NOW,6000)
for frame in range(30):call(u,PC['pc_present'],0x30001d80,1,0,0)
sent={(c,p) for c,p,v in commands}
check(len(flips)==30 and (4,0x1bf) in sent and (4,0x1c0) in sent and all(c==4 for c,p in sent),'flip hook: PC map band, then Cobalt cpu_present (synth resync of track 5), then the stock flip')
# S9a: the resync shares the unlocked M4->M7 command ring with the project
# loader task, so it stays silent while a load is pending, for 1 s after it,
# and while the ring is more than 32 commands behind.
NAME=TMP+0x600;RING=0x30040000
def frames(n=75): # one full round-robin: 12 tracks x 25 ids (S10: +5 LFO2), 4 per frame
 commands.clear()
 for frame in range(n):call(u,PC['pc_present'],0x30001d80,1,0,0)
 return len(commands)
u.mem_write(NAME,b'Song 2\0');w32(u,0x3001734c,NAME)
check(frames()==0,'no synth resync while a project load is pending (PresetMgr pending-load name set)')
u.mem_write(NAME,b'\0');w32(u,UI_NOW,6900)
check(frames()==0,'no synth resync 900 ms after the load finished')
w32(u,UI_NOW,7000)
check(frames()>0,'synth resync resumes 1 s after the load finished')
w32(u,RING+0x1804,1000);w32(u,RING+0x1800,1000-33)
check(frames()==0,'no synth resync while the M7 is more than 32 commands behind')
w32(u,RING+0x1800,1000-32)
check(frames()>0,'synth resync sends again once the ring has drained to 32')
w32(u,RING+0x1800,1000);w32(u,0x3001734c,0)
check(frames()>0,'no pending-load String at all reads as no load')
# One definitions table holds Cobalt, synth/insert and PC map entries.
check(call(u,0x8120234,APP+0xc08+4*0x30,0x1c0)==3 and call(u,0x8120234,APP+0xc08,0x1d5)==1 and call(u,0x8120234,APP+0xc08,0x1ba)==0,'track tables carry synth and insert settings with their defaults (Insert On 1, type 0)')
print(H.checks,'S9 integration checks passed; device behaviour of the combination needs hardware.')
