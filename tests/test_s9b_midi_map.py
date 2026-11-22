"""S9b: MIDI-mapped CCs for Cobalt's track parameters, on the M7. Real code:
the stock definition build (0x0805342c) plus learn_defs_build, the map add
for an M4 0x7c message (0x0804f364) and the CC resolver (0x0804f0c8, through
the patched post at 0x0804f1ca). Only the M7 allocator and the event-queue
push (0x0804e1e8) are substituted."""
from firmware_harness import *
import firmware_harness as H
G=M['symbols']['gate7']
THIS=0xc0100000;CTX=0x24040000;MSG=0x24030000;MIDI=0x24030100;DEFS=0x24008e50
u=machine();heap=[0xc0400000]
def alloc(q):
 a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(u,0x80413d4,alloc)
posts=[]
def push(q):
 a=q.reg_read(UC_ARM_REG_R0);e=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))
 queue=(a-CTX-4)//0x604 if CTX<=a<CTX+4+0x38*0x604 and (a-CTX-4)%0x604==0 else None
 posts.append((queue,e[0],struct.unpack_from('<I',e,8)[0],struct.unpack_from('<H',e,12)[0],struct.unpack_from('<i',e,16)[0]))
stub(u,0x804e1e8,push)
# Stock table alone, for comparison.
s=machine(stock);sheap=[0xc0400000]
def salloc(q):
 a=sheap[0];sheap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(s,0x80413d4,salloc);call(s,0x805342c,0)
stock_defs=bytes(s.mem_read(u32(s,DEFS),0x3934))
check(call(u,G['learn_defs_build'],0) is not None and u32(u,DEFS)!=0,'learn_defs_build runs the stock build')
defs=bytes(u.mem_read(u32(u,DEFS),0x3934))
OURS=[0x1ae,*range(0x1ba,0x1d6),*range(0x1d7,0x1df)] # S10: LFO2; S18: Reso; F6: Chord
COBALT=[0x43,0x3a,0x3d,0x4a,0x4b,0x155,*range(0x1a0,0x1ba)] # Cobalt FX ids, checked in test_midi_map.py
check(all(defs[i*28:i*28+28]==stock_defs[i*28:i*28+28] for i in range(0x20b) if i not in OURS+COBALT),'every stock M7 definition is unchanged')
check(all(struct.unpack_from('<H',stock_defs,i*28)[0]==0 for i in OURS),'the stock M7 table has no Gate / insert / synth entries (why CCs gave 0)')
def m7_range(i):
 d=defs[i*28:i*28+28];lo=struct.unpack_from('<i',d,16)[0]
 return lo,struct.unpack_from('<i',d,12)[0]-1 if d[2] in (5,6) else struct.unpack_from('<i',d,20)[0]
# Same ranges as the M4 table (sat_defs / gate_default).
m4=machine();m4heap=[0xc0010000]
def m4alloc(q):
 a=m4heap[0];m4heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
stub(m4,0x8103128,m4alloc);call(m4,0x8134cb8,0);T4=u32(m4,0x3001eb30)
def m4_range(i):
 d=bytes(m4.mem_read(T4+i*28,28));lo=struct.unpack_from('<i',d,16)[0]
 return lo,struct.unpack_from('<i',d,12)[0]-1 if d[2] in (5,6) else struct.unpack_from('<i',d,20)[0]
DIVS=(0x1ca,0x1d2,0x1d7,0x1db) # S18: the four LFO speed ids take the raw CC 0..127
bad=[hex(i) for i in OURS if i not in DIVS and (m7_range(i)!=m4_range(i) or defs[i*28+2]!=bytes(m4.mem_read(T4+i*28+2,1))[0])]
check(all(m7_range(i)==(0,127) for i in DIVS),'S18: LFO / LFO2 speed CCs arrive as the raw CC, so the mode decides divisions or rate at full resolution')
check(not bad,'M7 ranges and types equal the M4 definitions for all 35 ids '+str(bad))
# Map items as the M4 sends them after Learn or on project load (0x7c).
def add_map(chan,ctrl,param,cell):
 u.mem_write(MSG,struct.pack('<BxHII',0x7c,cell,0,0x19)+struct.pack('<IIHH',chan,0xb0,ctrl,param))
 call(u,0x804f364,THIS,MSG)
def cc(chan,ctrl,value):
 posts.clear();u.mem_write(MIDI,bytes([0xb0|chan,ctrl,value,0,3]));call(u,0x804f0c8,THIS,MIDI,CTX,0)
 return posts[:]
u.mem_write(THIS,bytes(0x14000))
for chan,ctrl,param,cell in [(2,20,0x1c3,7),(0,30,0x1bb,3),(0,31,0x1be,3),(0,34,0x1d5,3),(0,35,0x1ae,3),(1,7,0x4,5),(0,40,0x1c0,7),(0,50,0x1d9,4),(0,51,0x1d7,4)]:add_map(chan,ctrl,param,cell)
def track(p,q,t):return [x for x in p if x[0]==q and x[2]==t]
p=cc(2,20,127)
check(track(p,0xc,7)==[(0xc,0x39,7,0x1c3,1000)] and track(p,7,7)==[(7,0x39,7,0x1c3,1000)],'CC 127 sets Timbre of track 8 to 1000, on queue 0x0c and on the track\'s own queue '+str(p))
p=cc(2,20,64);check(track(p,7,7)==[(7,0x39,7,0x1c3,504)],'CC 64 -> Timbre 504 (round(1000*64/127))')
p=cc(0,30,0);q=cc(0,30,127)
check(track(p,3,3)[0][4]==0 and track(q,3,3)[0][4]==1000,'Drive follows CC 0..127 as 0..1000 on track 4')
p=cc(0,31,0);q=cc(0,31,127)
check(track(p,3,3)[0][4]==-18000 and track(q,3,3)[0][4]==6000,'Output follows CC as -18.0..+6.0 dB')
p=cc(0,34,10);q=cc(0,34,100)
check(track(p,3,3)[0][4]==0 and track(q,3,3)[0][4]==1,'INSRT is Off below CC 64 and On above')
p=cc(0,35,127);check(track(p,3,3)[0][4]==-12000,'Gate threshold maps over -96..-12 dB')
p=cc(0,40,127);q=cc(0,40,0);check(track(p,7,7)[0][4]==1 and track(q,7,7)[0][4]==0,'synth Engine maps over its two engines (F2: VA 0, Cluster 1)')
p=cc(0,50,0);q=cc(0,50,127)
check(track(p,4,4)[0][4]==0 and track(q,4,4)[0][4]==1000,'S10: LFO2 Amount follows CC 0..127 as 0..1000 on track 5 own queue')
p=cc(0,51,127);check(track(p,4,4)[0][4]==20000,'S10: LFO2 Rate maps up to 20 Hz')
p=cc(1,7,127)
check([x for x in p if x[0]==5]==[] and any(x[0]==0xc and x[3]==4 for x in p),'a stock parameter (track Level) still goes to queue 0x0c only, as before')
check(all(any(x[0] is None for x in cc(*a)) for a in [(2,20,90),(0,30,90)]),'the value is still echoed to the M4 as before')
# S16: log controls follow the encoder curve by CC, and the M4 echo agrees.
for chan,ctrl,param,cell in [(0,60,0x1c9,7),(0,61,0x1ca,7),(0,62,0x1d2,7),(0,63,0x1d0,7)]:add_map(chan,ctrl,param,cell)
def cutoff(v):p=cc(0,60,v);return track(p,7,7)[0][4],[x[4] for x in p if x[0] is None]
lo,echo=cutoff(0);check(lo==20 and echo==[20],'Cutoff CC 0 -> 20 Hz')
one,echo=cutoff(1);check(20<one<=25 and echo==[one],'Cutoff CC 1 moves one log step (%d Hz), not to ~177 Hz; the echo shows the same'%one)
mid,_=cutoff(64);check(600<=mid<=700,'Cutoff CC 64 is the geometric middle (%d Hz)'%mid)
top,_=cutoff(127);check(top==20000,'Cutoff CC 127 -> 20 kHz')
rates=[track(cc(0,61,v),7,7)[0][4] for v in (0,1,64,127)]
check(rates[0]==10 and rates[1]<60 and 1000<=rates[2]<=4000 and rates[3]==20000,'LFO rate by CC follows the S15 encoder curve '+str(rates))
# S17: an LFO speed CC acts on the speed of the LFO's current mode (read
# from the live voice), whichever knob it was learned on; never the mode.
add_map(0,64,0x1d7,7);add_map(0,65,0x1db,7)
BANK=0x38800180;VOICE=0x24078000
def voice(sync1,sync2):
 u.mem_write(VOICE,bytes(1200));w32(u,VOICE,0x53594e3a)
 u.mem_write(VOICE+6+2*(0x1d2-0x1bf-1),struct.pack('<h',sync1)) # value[SYN_INDEX(SYN_LFO_SYNC)]
 u.mem_write(VOICE+856+2*(0x1db-0x1d7),struct.pack('<h',sync2))  # lfo2[SYN_LFO2_INDEX(SYN_LFO2_SYNC)]
 w32(u,BANK,0x53594e3a);u.mem_write(BANK+4,bytes(48));w32(u,BANK+4+7*4,VOICE)
def speed(ctrl,v):
 p=[x for x in cc(0,ctrl,v) if x[0]==7 and x[2]==7];echo=[x for x in cc(0,ctrl,v) if x[0] is None]
 return p[0][3],p[0][4],echo[0][3],echo[0][4]
w32(u,BANK,0)
check(speed(62,127)[:2]==(0x1ca,20000),'no live synth voice: a division CC changes the rate, never the mode')
voice(5,0)
r=[speed(62,v) for v in (0,64,127)]
check([x[:2] for x in r]==[(0x1d2,1),(0x1d2,7),(0x1d2,12)] and all(x[2:]==x[:2] for x in r),'LFO in Sync: a division CC walks 1/64..1 bar and stays in Sync (echo agrees) '+str(r))
r=[speed(61,v) for v in (0,127)]
check([x[:2] for x in r]==[(0x1d2,1),(0x1d2,12)],'LFO in Sync: a CC learned on LFO S (Free) also walks the divisions')
voice(17,0)
r=[speed(62,v) for v in (0,64,127)]
check([x[0] for x in r]==[0x1ca]*3 and r[0][1]==10 and r[2][1]==20000 and r[1][2:]==r[1][:2],'LFO in Free: a CC learned on the division changes the rate and stays in Free '+str(r))
check(speed(61,127)[:2]==(0x1ca,20000),'LFO in Free: a rate CC changes the rate')
voice(0,9)
check(speed(65,127)[:2]==(0x1db,12) and speed(64,0)[:2]==(0x1db,1),'LFO2 in Sync: both LFO2 speed CCs walk LFO2 divisions')
voice(0,0)
check(speed(65,127)[:2]==(0x1d7,20000) and speed(64,127)[:2]==(0x1d7,20000),'LFO2 in Free: both LFO2 speed CCs change the LFO2 rate')
w32(u,BANK,0)
atk=[track(cc(0,63,v),7,7)[0][4] for v in (0,1,127)]
check(atk[0]==0 and atk[1]<=15 and atk[2]==5000,'Env Attack by CC follows its curve '+str(atk))
print(H.checks,'S9b M7 MIDI map checks passed; controller response needs hardware.')
