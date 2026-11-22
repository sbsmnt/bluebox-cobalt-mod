"""Real native TRACK encoder messages, model commits and rebinding at raw
travel sizes >1. Catch I2's delta*400 endpoint jump and acceleration issues.
"""
from gate_ui_fixture import *
import firmware_harness as H
IU=0x388001e0;EV=TMP+0x500;W=T+0xc398;PID=0x1ba
setup(0,7,-96000);call(u,S['insert_track_group'],T,0)
w32(u,IU+4,1)
u.mem_write(EV,struct.pack('<6I',0x67,0x8148664,0,0,0,0));call(u,u32(u,0x814bb70),T,EV)
def get():return call(u,0x8120234,APP+0xc08+7*0x30,PID)
def set_mode(value):
 call(u,0x8120264,APP+0xc08+7*0x30,PID,value)
 u.mem_write(EV,struct.pack('<6I',0x66,0x8148664,7,PID,value,0));call(u,u32(u,0x814bb70),T,EV)
 w32(u,IU+16,0xffffffff);commands.clear();notifications.clear()
def turn(delta):
 commands.clear();notifications.clear()
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,0,delta&0xffffffff,0));call(u,u32(u,0x814bb70),T,EV);flush()
 return get(),commands[:]
def one(edits,mode):
 # One type edit; entering/leaving a filter adds only its Cutoff/Res/Env defaults.
 return edits[:1]==[(7,PID,mode)] and all(e[0]==7 and e[1] in (0x1bb,0x1bc,0x1bd) for e in edits[1:])
# No Off position, and travel stays in the shown group: DRV runs CLIP, FOLD,
# WRAP; FLTR runs LP, BP, HP, DJ.
set_mode(1)
now,edits=turn(20)
check(now==1 and not edits,'ordinary multi-unit raw message cannot jump CLIP to WRAP')
for expected in (3,4):
 for i in range(19 if expected==3 else 20):now,edits=turn(20)
 check(now==expected and one(edits,expected),'slow travel reaches each consecutive type with exactly one model/IPC edit '+str(expected))
for i in range(5):now,edits=turn(32767)
check(now==4 and not edits,'DRV travel stops at WRAP (never enters the filters)')
for expected in (3,1):
 for i in range(20):now,edits=turn(-20)
 check(now==expected and one(edits,expected),'slow reverse travel reaches every intermediate type '+str(expected))
set_mode(8)
for i in range(5):now,edits=turn(32767)
check(now==8 and not edits,'FLTR travel stops at DJ')
for expected in (7,6,5):
 now,edits=turn(-32768)
 check(now==expected and one(edits,expected),'FLTR reverse travel reaches every filter type '+str(expected))
for i in range(5):now,edits=turn(-32768)
check(now==5 and not edits,'FLTR travel stops at LP (never enters the drives)')
# Huge bursts and native clock-dependent acceleration never skip a choice.
for dt in (1,10,500,2000):
 set_mode(1)
 for expected in (3,4):
  w32(u,0x3001eb3c,100000+dt*expected);now,edits=turn(32767)
  check(now==expected and one(edits,expected),'one forward choice maximum despite large/fast raw input '+str((dt,expected)))
 for expected in (3,1):
  now,edits=turn(-32768)
  check(now==expected and one(edits,expected),'one reverse choice maximum despite large raw input '+str((dt,expected)))
# Tiny inputs accumulate over time. No pause threshold discards a deliberate turn.
set_mode(1)
for i in range(399):
 w32(u,0x3001eb3c,1000000+i*2000);now,edits=turn(1)
check(now==1 and not edits,'399 one-unit inputs accumulate without premature changes even with long pauses')
now,edits=turn(1);check(now==3 and edits==[(7,PID,3)],'400th raw unit selects FOLD')
# Direction, endpoint, external edits and channel changes discard stale travel.
turn(200);now,_=turn(-200);check(now==3,'direction reversal resets old positive partial movement')
now,_=turn(-200);check(now==1,'reversed movement reaches CLIP at its own travel threshold')
for i in range(5):now,edits=turn(-32768)
check(now==1 and not edits,'CLIP endpoint (no Off) never accumulates a reverse dead zone')
now,_=turn(400);check(now==3,'forward travel responds immediately after repeated turns at CLIP')
# A fresh track (stored 0) shows CLIP: left does nothing, right picks FOLD and
# keeps INSRT Off.
set_mode(0);call(u,0x8120264,APP+0xc08+7*0x30,0x1d5,1)
now,edits=turn(-400);check(now==0 and not edits,'fresh track: left from its CLIP does nothing')
now,edits=turn(400);check(now==3 and edits==[(7,0x1d5,0),(7,PID,3)],'fresh track: first type pick commits INSRT Off, then FOLD '+str(edits))
turn(200)
call(u,0x8120264,APP+0xc08+7*0x30,PID,4)
u.mem_write(EV,struct.pack('<6I',0x66,0x8148664,7,PID,4,0));call(u,u32(u,0x814bb70),T,EV)
now,_=turn(120);check(now==4,'external model change discards previous partial travel')
# Selection notifications switch channel ownership without cross-track carry.
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,u32(u,0x814bb70),T,EV)
commands.clear();notifications.clear()
u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,0,100,0));call(u,u32(u,0x814bb70),T,EV);flush()
check(call(u,0x8120234,APP+0xc08+2*0x30,PID)==0 and get()==4 and not commands,'partial travel does not follow a different selected track')
check(call(u,0x8120234,APP+0xc08+7*0x30,4)==0 and call(u,0x8120234,APP+0xc08+7*0x30,0x1ae)==((-96000)&0xffffffff),'selector does not change original Volume or Gate')
print(H.checks,'raw encoder travel/native edit checks passed; physical tactile feel remains a device check.')
