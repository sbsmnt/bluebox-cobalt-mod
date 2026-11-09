"""Projects saved before the DJ EQ: their parameter sets have no DJ entries,
and the stock model setter then only sends the value to the audio core, so
nothing is stored or shown. The real setter runs here (no stub) on sets
without the DJ ids; the DJ code must add the entries before editing."""
from gate_ui_fixture import *
import firmware_harness as H
D=M['symbols']['dj4'];MAIN=APP+0xe78;FAKEAPP=0xc0700000;UI=0x388001ec
def get(set,id):return struct.unpack('<i',struct.pack('<I',call(u,0x8120234,set,id)))[0]
def has(set,id):
 items=u32(u,set+4);n=struct.unpack('<H',u.mem_read(set+10,2))[0]
 return any(struct.unpack('<H',u.mem_read(items+8*i,2))[0]==id for i in range(n))
def count(set):return struct.unpack('<H',u.mem_read(set+10,2))[0]

# The fixture's Main set is like an old project's: no DJ settings.
check(not any(has(MAIN,0x1c0+i) for i in range(4)),'an old project\'s Main set has no DJ settings')
CELL=0x24079000;u.mem_write(CELL,struct.pack('<H',0x0c))
call(u,0x8124b44,CELL and APP,CELL,0x1c2,1500,0)
check(get(MAIN,0x1c2)==0 and commands[-1]==(12,0x1c2,1500),'stock setter alone: the audio core gets 1500, the set keeps nothing (the bug)')
# Through the DJ settings page path the entry is added first, then stored.
u.mem_write(UI,bytes(20));w32(u,0x3000d9fc,FAKEAPP)
call(u,D['dj_knob'],APP,CELL,0x1c2,1500,0)
check(get(MAIN,0x1c2)==1500 and (12,0x1c2,1500) in commands[-4:],'DJ settings edit on an old project: stored, read back and sent')
check(has(MAIN,0x1c0) and get(MAIN,0x1c1)==200 and get(MAIN,0x1c3)==5000,'the other DJ settings are added with their defaults (ISO, 200 Hz, 5 kHz)')
n=count(MAIN);call(u,D['dj_knob'],APP,CELL,0x1c2,1600,0)
check(count(MAIN)==n and get(MAIN,0x1c2)==1600,'existing entries are updated, never duplicated')

# Track sets: drop the DJ entries the fixture's defaults appended (old project).
SET=APP+0xc08+4*0x30
u.mem_write(SET+10,struct.pack('<H',count(SET)-6))
check(not has(SET,0x1bd),'an old project\'s track 5 has no DJ entries')
TMP2=0x2407b000
u.mem_write(TMP2,struct.pack('<IIIIII',0x6e,0,4,0x1bd,(-60)&0xffffffff,0))
call(u,D['dj_track_local'],T,TMP2)
check(get(SET,0x1bd)==-60 and has(SET,0x1ba) and get(SET,0x1ba)==0,'a DJ knob edit on an old project stores the value (others added neutral)')
# Main with Cobalt's saturation holds 68 entries: full at the stock capacity
# of 68 the session allocates at boot, so the DJ settings could not be added.
SITES=(0x811e164,0x811e174,0x811e186,0x811e198,0x811e26e,0x811e27e,0x811e290,0x811e2a2,0x81202be,0x8126cce,0x8126ce2,0x8126cf6,0x8126d0a)
check(all(stock[a-0x8040000]==0x44 and patched[a-0x8040000]==0x60 for a in SITES),'every boot-time set allocation reserves 96 entries instead of 68')
FULL=0x2407e000;ITEMS=0xc0600000
def fill(cap):
 u.mem_write(ITEMS,bytes(8*96))
 for i in range(68):u.mem_write(ITEMS+8*i,struct.pack('<H2xi',0x300+i,i))
 u.mem_write(FULL,struct.pack('<IIHHH',0,ITEMS,0,68,cap))
fill(68);u.mem_write(UI,bytes(20))
w32(u,FULL,0)
# Run the DJ add on this set by pointing Main at it.
saved=bytes(u.mem_read(MAIN,16));u.mem_write(MAIN,bytes(u.mem_read(FULL,16)))
call(u,D['dj_knob'],APP,CELL,0x1c1,300,0)
check(get(MAIN,0x1c1)==0,'at the old capacity a full Main cannot take the DJ settings (the bug)')
fill(96);u.mem_write(MAIN,bytes(u.mem_read(FULL,16)))
call(u,D['dj_knob'],APP,CELL,0x1c1,300,0)
check(get(MAIN,0x1c1)==300 and count(MAIN)==77,'with 96 it takes them: stored and read back (77 of 96)')
u.mem_write(MAIN,saved)
print('DJ old project checks:',H.checks)
