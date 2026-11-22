"""S7a: saved synth settings are re-sent to the audio engine after boot.
Real native settings registration, per-track defaults and model setter; only
the IPC send (0x08108784) and allocation are stubbed."""
from firmware_harness import *
import firmware_harness as H
S=M['symbols']['4']
APP=0x300173d0;TMP=0x24008000;IPC=0x3000a894;UI_NOW=0x3001eb3c
IDS=[0x1bf,0x1c0,0x1c1,0x1c2,0x1c3,0x1c4,0x1c5,0x1c6,0x1c8,0x1c9,0x1ca,0x1cb,0x1cc,0x1cd,0x1ce,0x1cf,0x1d0,0x1d1,0x1d2,0x1d4,0x1d7,0x1d8,0x1d9,0x1da,0x1db,0x1dd,0x3] # S18: Reso; S10: LFO2 rate..sync; S15: track Gain
u=machine();heap=[0xc0010000]
def allocate(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,allocate);call(u,0x8134cb8,0);call(u,0x8120abc)
for ch in range(12):
 u.mem_write(TMP,b'\1');call(u,0x81202b8,APP+0xc08+ch*0x30,TMP)
commands=[];stub(u,0x8108784,lambda q:commands.append(tuple(q.reg_read(r) for r in (UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))))
def setting(ch):return APP+0xc08+ch*0x30
# Tracks 2 and 9 are Cluster synths with non-default values, as after a project load.
saved={2:{0x1bf:1,0x1c0:3,0x1c3:120,0x1c9:3000,0x1d2:21,0x1d4:11},9:{0x1bf:1,0x1c0:1,0x1c4:900,0x1d1:3,0x1d4:30}}
for ch,values in saved.items():
 for pid,value in values.items():call(u,0x8120264,setting(ch),pid,value)
u.mem_write(APP+0x73a1,b'\0');w32(u,IPC,0x30040000)
w32(u,UI_NOW,1000)
for frame in range(10):call(u,S['synth_resync_tick'])
check(commands==[],'no resync during the first seconds after boot')
w32(u,UI_NOW,6000);per_frame=[]
for frame in range(12*len(IDS)//4+4):
 before=len(commands);call(u,S['synth_resync_tick']);per_frame.append(len(commands)-before)
check(max(per_frame)<=5,'at most four synth values per display frame, plus the track input after Synth On (S14)')
sent={}
for ch,pid,value in commands:sent.setdefault(ch,{})[pid]=value
inputs={ch:sent[ch].pop(0x112,None) for ch in sent} # S14: each Synth On send is followed by the track input
check(sorted(sent)==[2,9],'only enabled synth tracks are re-sent')
for ch in (2,9):
 check(sorted(sent[ch])==sorted(IDS),'every DSP synth setting of track '+str(ch)+' is re-sent')
 check(all(sent[ch][pid]==call(u,0x8120234,setting(ch),pid) for pid in IDS),'re-sent values equal the saved model on track '+str(ch))
check(all(inputs[ch] is not None for ch in (2,9)),'S14: the track input (stand-in for None) is re-sent with Synth On')
check(sent[2][0x1d4]==11 and sent[2][0x1d2]==21 and sent[9][0x1c0]==1,'seed, remembered-division Free sync and engine are carried exactly')
check(u.mem_read(APP+0x73a1,1)==b'\0','resync never marks the project dirty')
w32(u,IPC,0);commands.clear();call(u,S['synth_resync_tick'])
check(commands==[],'no send before the native IPC object exists')
print(H.checks,'synth resync checks passed; device boot timing still needs hardware.')
