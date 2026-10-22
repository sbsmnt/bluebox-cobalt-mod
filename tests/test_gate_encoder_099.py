"""Physical MIXER encoder routing with populated native base bindings.
Unlike v0.9.8's fixture, parent edits reach the native model and native active
panel notifications execute on the same emulator stack. IPC remains captured.
"""
from gate_ui_fixture import *
u.hook_del(parent_hook);u.hook_del(notify_hook)
active=[S['gate_mix_event']]
def root(q,a,n,_):
 e=bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24));parents.append(e)
 if struct.unpack_from('<H',e)[0]==0x6e:
  q.reg_write(UC_ARM_REG_R0,APP);q.reg_write(UC_ARM_REG_PC,0x8125d45)
 else:q.reg_write(UC_ARM_REG_R0,0);q.reg_write(UC_ARM_REG_PC,q.reg_read(UC_ARM_REG_LR))
def notify(q,a,n,_):
 notifications.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24)))
 q.reg_write(UC_ARM_REG_R0,P);q.reg_write(UC_ARM_REG_PC,active[0]|1)
u.hook_add(UC_HOOK_CODE,root,begin=0x80dfe00,end=0x80dfe00)
u.hook_add(UC_HOOK_CODE,notify,begin=0x813c384,end=0x813c384)
def turn(index,delta):
 clock[0]+=1000;w32(u,0x3001eb3c,clock[0])
 u.mem_write(TMP,struct.pack('<6I',0x32,0x8148664,0,index&0xffffffff,delta&0xffffffff,0))
 call(u,active[0],P,TMP)
def bind_base():
 for i,off in enumerate((0x40,0x328,0x610,0x8f8)):
  call(u,0x8120264,APP+0xc08+i*0x30,4,(-10000)&0xffffffff)
  call(u,0x813bbc4,4,(-10000)&0xffffffff,P+off)
def refresh_track():
 # Inactive TRACK receives the saved threshold when opened/refreshed.
 for e in notifications[:]:
  u.mem_write(TMP,e);call(u,0x8133194,T,TMP)
 notifications.clear()
# Reproduce the native dispatch defect underlying the v0.9.8 wrapper: it
# delegates all encoder turns to 0x0812E754, which runs base AND column paths.
setup(0,0,-60000);bind_base();active[0]=0x812e754
for i in range(200):turn(0,1)
check(any(pid==4 for ch,pid,v in commands),'old native MIXER route emits Vol edits while Gate is selected')
active[0]=S['gate_mix_event'];setup(0,0,-60000);bind_base()
for i in range(200):turn(0,1)
check(bool(commands) and all(ch==0 and pid==0x1ae for ch,pid,v in commands),'dedicated Gate hardware route emits only Gate, including slow one-unit travel')
check(call(u,0x8120234,APP+0xc08,4)==((-10000)&0xffffffff),'Gate hardware turns do not change native Vol')
refresh_track();check(val(T+0xde48)==val(P+0xbe0+0x8f0)>-60000,'slow MIXER turns persist and refresh TRACK through native model notifications')
# All visible banks, encoder positions, output modes, and native last-bank mask.
for quad in (0,1):
 for bank in range(0,7):
  for one in (0,1):
   for index in range(4):
    ch=bank*2+((index&1) if one else index)
    if ch>12:continue
    setup(quad,min(ch,11),-60000);bind_base()
    w32(u,P+0xbcbc,bank);u.mem_write(P+0xbcc0,bytes([one]))
    commands.clear();before=val(T+0xde48);turn(index,160)
    if ch==12:
     check(not commands,'Main remains excluded from physical Gate turns '+str((quad,bank,one,index)))
    else:
     refresh_track();expected=val(T+0xde48)
     check(commands==[(ch,0x1ae,expected&0xffffffff)] and expected>before,'physical Gate turn commits correct slot and TRACK value '+str((quad,bank,one,index)))
# Native flags / child consumption must not leave MIXER with a cosmetic edit.
for consumed in (False,True):
 setup(0,3,-60000);bind_base();w32(u,P+0xbcbc,0);u.mem_write(P+0xbcc0,b'\0')
 k=P+0xbe0+3*0xd98+0x8f0;parent_saved=u32(u,k+0x2c)
 u.mem_write(k+0x2d4,b'\1');u.mem_write(k+0x30,b'\1')
 if consumed:w32(u,k+0x2c,0)
 turn(3,160);w32(u,k+0x2c,parent_saved);refresh_track()
 check(len(commands)==1 and commands[0][0:2]==(3,0x1ae) and val(T+0xde48)>-60000,'MIXER threshold commits once despite native disabled/hidden state and child consumption '+str(consumed))
# Use an actual hardware turn's command to drive native M7 Gate/EQ audio.
setup(0,0,-72000);bind_base();turn(0,160)
ch,pid,v=commands[-1];v=struct.unpack('<i',struct.pack('<I',v))[0]
a,_,_=gsetup(ch=ch)
for i in range(200):pcm,_=gblock(a,[.0001]*32,[.0001]*32,[(pid,v)] if i==0 else [],ch=ch)
check(max(map(abs,struct.unpack('<64f',pcm)))<1e-10,'physical MIXER command closes real native-queue audio for -80dB signal')
# Native screen bar state, separate from Gate's threshold marker: equal
# incoming values must populate the same peak-bar fields in Vol and Gate.
bar_states=[]
for mode,index in ((0,0),(1,6)):
 call(u,S['gate_mix_setup'],P,mode);call(u,S['gate_mix_select'],P,index)
 for r,value in ((UC_ARM_REG_S0,-56.),(UC_ARM_REG_S1,-61.)):
  u.reg_write(r,struct.unpack('<I',struct.pack('<f',value))[0])
 call(u,0x812d432,P+0xbe0)
 bar_states.append(bytes(u.mem_read(P+0xbe0+0x284,0x140)))
check(bar_states[0]==bar_states[1],'native Vol/Gate peak bar widgets receive identical values and scale state')
print(str(H.checks)+' v1.0.0-alpha.4 hardware-encoder routing checks passed; physical device retest required.')
