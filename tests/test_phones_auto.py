"""Phones Src AUTO (DJ-style PFL): a real mixer object initialised by the
stock code, with cue sends set by the stock setters, feeds the Cobalt output
wrapper; only the native output and mixer node bodies are stubbed to record
the Main-copy flag. Static checks cover the patched pointers and the list."""
from firmware_harness import *
import firmware_harness as H
CS=M['symbols']['cue7'];OUT=0xc0000000;NATIVE=0x804e21c;STATE=0x38800660
CH=12;Q=CTX+CH*0x604+4;FLAG=OUT+0x88;MIX=0xc0010000;CUEBUS=CTX+0x150e4+15*20
STRIP=lambda i:MIX+0x20+i*0x130

def u8(u,a):return u.mem_read(a,1)[0]
def setup():
 u=machine(patched);seen=[]
 def native(u):seen.append(u8(u,FLAG));u.reg_write(UC_ARM_REG_R0,1)
 stub(u,NATIVE,native);stub(u,0x804fbac,lambda u:u.reg_write(UC_ARM_REG_R0,0))
 w32(u,OUT+0x18,CH)
 u.mem_write(MIX,bytes(0xe80));call(u,0x804f674,MIX)   # stock mixer init
 w32(u,MIX+0xe68,12)
 call(u,CS['cue_mixer_process'],MIX,CTX)              # wrapper records it
 return u,seen
def cue(u,i,level,on=1,pre=True):
 # level in dB, as the stock setter takes it; None is fully down.
 if level is None:level=-200.
 send=STRIP(i)+(0xa0 if pre else 0xb8)
 u.reg_write(UC_ARM_REG_S0,struct.unpack('<I',struct.pack('<f',level))[0])
 call(u,0x805211c,send);call(u,0x805216c,send,on)     # stock level, on/off
def inputs(u,i,on):u.mem_write(STRIP(i)+6,bytes([on]))
def block(u,src=None,flag=None):
 evs=[] if src is None else [(0x178,src)]
 for k,(pid,v) in enumerate(evs):u.mem_write(Q+k*24,struct.pack('<B7xIHHiI',0x39,12,pid,0,v,0))
 w32(u,Q+0x600,len(evs))
 u.mem_write(CUEBUS,struct.pack('<IIIIBBxx',32,0,BUF,BUF+0x100,1,1))
 if flag is not None:u.mem_write(FLAG,bytes([flag]))
 return call(u,CS['cue_out_process'],OUT,CTX)
def masks(u):return struct.unpack('<II',u.mem_read(STATE+16,8))

# Static patch layout.
m=machine(patched)
check(u32(m,0x806aa00)==CS['cue_out_process']|1,'output node vtable calls the AUTO wrapper')
check(u32(m,0x806af38)==CS['cue_mixer_process']|1,'mixer node vtable calls the recording wrapper')
check(struct.unpack('<II',stock[0x806aa00-0x8040000:][:4]+stock[0x806af38-0x8040000:][:4])==(NATIVE|1,0x804fbad),'stock slots were the native output and mixer nodes')
names=[bytes(m.mem_read(u32(m,CS['cue_phones_names']+4*i),5)).split(b'\0')[0] for i in range(3)]
check(names==[b'CUE',b'MAIN',b'AUTO'],'Phones Src list reads CUE, MAIN, AUTO')
check(u32(m,0x8135b28)==CS['cue_phones_names'],'Phones Src registration uses the new list')
check(bytes(m.mem_read(0x81358dc,2))==bytes.fromhex('0323'),'Phones Src registers three choices')

# CUE and MAIN are untouched: the wrapper never writes the native flag.
u,seen=setup();cue(u,0,0.)
block(u,src=0,flag=0);block(u,flag=0)
check(seen==[0,0],'CUE leaves the native flag alone, even with a cued track')
block(u,src=1,flag=1);block(u,flag=1)
check(seen[2:]==[1,1],'MAIN leaves the native flag alone, even with a cued track')

# AUTO behaves like PFL: the cue state counts, not audio or inputs.
u,seen=setup()
check(block(u,src=2,flag=0)==1,'wrapper returns the native result')
check(seen[-1]==1,'AUTO without any cued track plays Main')
cue(u,2,None);block(u)
check(seen[-1]==1,'AUTO: a cue turned to zero keeps Main')
cue(u,2,0.,on=0);block(u)
check(seen[-1]==1,'AUTO: a cue toggled off keeps Main')
cue(u,2,0.);block(u)
check(seen[-1]==0,'AUTO: a cued track without an input still selects cue')
cue(u,2,None);cue(u,5,-6.,pre=False);block(u)
check(seen[-1]==0,'AUTO: a post-fader cue send also selects cue')
cue(u,5,None,pre=False)
for _ in range(149):block(u)
check(seen[-1]==0,'AUTO holds cue for 149 blocks (99 ms) after the last cue goes off')
block(u)
check(seen[-1]==1,'AUTO returns to Main 100 ms after the last cue goes off')
check(struct.unpack('<Ii',u.mem_read(STATE,8))==(0x43554534,2),'AUTO state kept in backup SRAM')

# Masks for the header icons and dimming, independent of Phones Src.
u,seen=setup();block(u,src=0)
check(masks(u)==(0,0),'no cue and no inputs after the stock mixer init')
cue(u,3,0.);inputs(u,0,1);inputs(u,11,1);block(u)
check(masks(u)==(1<<3,(1<<0)|(1<<11)),'cue mask marks track 4; input mask tracks 1 and 12')
cue(u,3,0.,on=0);u.mem_write(STRIP(11)+0xa,b'\1');inputs(u,11,0);block(u)
check(masks(u)==(0,(1<<0)|(1<<11)),'cue off clears its bit; either input flag counts')
w32(u,MIX+0xe68,4);cue(u,7,0.);block(u)
check(masks(u)==(0,1),'strips past the mixer strip count are ignored')
w32(u,MIX,0);block(u)
check(masks(u)==(0,0),'an object that is not the mixer is never read')

# Phones Src events for other buses are ignored; back to CUE restores stock.
u,seen=setup();block(u,src=2)
u.mem_write(Q,struct.pack('<B7xIHHiI',0x39,13,0x178,0,0,0));w32(u,Q+0x600,1)
call(u,CS['cue_out_process'],OUT,CTX)
check(struct.unpack('<i',u.mem_read(STATE+4,4))[0]==2,'Phones Src events for other buses are ignored')
block(u,src=0,flag=0);block(u,flag=0)
check(seen[-2:]==[0,0],'switching back to CUE restores stock behaviour')
print('Phones AUTO checks:',H.checks)
