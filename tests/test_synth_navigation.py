"""Four synth groups through physical A -> real model queue -> native root UI.
No synthesized group notifications. Display/time and transport are boundaries.
"""
from gate_ui_fixture import *
import firmware_harness as H
R=0xc0800000;T=R+0x2ae18;EV=TMP+0x400;IU=0x388001e0
OFFS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xda78,0xd2d8,0xde48]
BUTTON=T+0xe814
call(u,S['insert_track_construct'],T);call(u,0x8132ed4,T);w32(u,T+0x2c,ROOTOBJ)
u.hook_del(notify_hook)
for address in (0x8133de8,0x8102a4c,0x8108950,0x8126578):stub(u,address,lambda q:None)
w32(u,APP+0xc00,0);w32(u,APP+0xc04,0);u.mem_write(APP+0x7398,struct.pack('<H',7))
def drain():call(u,0x81343c4,R)
def press(kind=12,index=0):
 u.mem_write(EV,struct.pack('<6I',kind,0x8148664,0,index,0,0));call(u,0x8123862,APP,EV);drain()
def binding(i):
 if i==0 and not u32(u,IU+28):return struct.unpack('<H',u.mem_read(BUTTON+0xfa,2))[0]
 return struct.unpack('<H',u.mem_read(T+OFFS[i]+0x94,2))[0]
def hidden(i):return u.mem_read(T+OFFS[i]+0x30,1)[0]!=0
call(u,0x8122508,APP,5,0,0);drain()
# Start from the ordinary Sidechain action; TRACK must enter Synth on the left.
u.mem_write(APP+0x739a,b'\x16');press(0xf9,1)
check(u32(u,IU+4)==2 and u32(u,T+0xebbc)==0 and u32(u,IU+28)==0,'physical TRACK enters Synth at first left group')
call(u,u32(u,u32(u,BUTTON)+0x18),BUTTON,u32(u,BUTTON+4)+10,u32(u,BUTTON+8)+10);drain()
check(call(u,0x8120234,APP+0xc08+7*0x30,0x1bf)==1,'native Synth button enables the track through the real root queue')
GROUPS=[(0,0,[0x1bf,0x1c3,0x1c2,0x1c4]),(1,0,[0x1c5,0x1c9,0x1c6,0x1dd]),(0,1,[0x1ca,0x1d7,0x1cc,0x1d9]),(1,1,[0x1ce,0x1d0,0x1cf,0])] # S13: LFOs left (LFO top, LFO2 bottom), envelope right; 0 = empty slot
for cycle in range(3):
 for group,page,ids in GROUPS:
  check(u32(u,T+0xebbc)==group and u32(u,IU+12)==group and u32(u,IU+28)==page,'native queue presents correct focus/page '+str((cycle,group,page)))
  offset=group*4
  check([binding(offset+i) for i in range(len(ids)) if ids[i]]==[x for x in ids if x],'visible encoder bindings match requested group '+str((cycle,group,page)))
  if page:
   check(all(not hidden(i) for i in range(7)) and hidden(7) and u.mem_read(BUTTON+0x30,1)==b'\1','second page keeps four left parameters and three right ones (Env Attack, Env Amount, Env Decay) with no stale Synth button')
  # Run the native source list widget input for every active encoder. Source
  # selection is a touch button, so the first left encoder remains disabled.
  for enc,pid in enumerate(ids):
   commands.clear()
   value=call(u,0x8120234,APP+0xc08+7*0x30,pid)
   delta=-400 if (pid==0x1c8 and value) or (pid==0x1c9 and value>=20000) or cycle%2 else 400
   u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,enc,delta&0xffffffff,0));call(u,u32(u,0x814bb70),T,EV);drain()
   if pid==0x1bf:check(not commands,'On/Off remains a touch button, not a numeric encoder')
   elif pid==0:check(not commands,'the empty right slot edits nothing')
   else:check(commands and commands[-1][:2]==(7,pid),'real root/encoder cycle targets '+hex(pid))
  press()
check(u32(u,T+0xebbc)==0 and u32(u,IU+28)==0,'fourth A wraps to first left group after repeated full cycles')
# Entry must also start left when Synth is already On and the previous focus
# was right. This was a visible difference in the previous release.
call(u,S['insert_track_group'],T,1);u.mem_write(APP+0x739a,b'\x16');w32(u,IU+4,0);press(0xf9,1)
check(u32(u,T+0xebbc)==0 and u32(u,IU+28)==0,'re-entering an enabled synth always starts on the left')
print(H.checks,'native synth A-cycle checks passed; tactile/display behavior needs hardware verification.')
