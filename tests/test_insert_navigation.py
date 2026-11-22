"""Execute native A dispatcher -> model queue -> root UI loop -> real page
selection/layout/group/bind. No manufactured action/group transitions or
notification fanout. LCD presentation/time and IPC remain hardware boundaries.
"""
from gate_ui_fixture import *
import firmware_harness as H
R=0xc0800000;T=R+0x2ae18;EV=TMP+0x400
OFFSETS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48]
IDS=list(range(0x1ba,0x1bf));NORMAL=[4,3,0x63,0x15d,0xda,0xdb,0x15e,0x1ae]
call(u,S['insert_track_construct'],T);call(u,0x8132ed4,T);w32(u,T+0x2c,ROOTOBJ)
u.hook_del(notify_hook) # real native queue + native root notification loop
stub(u,0x8133de8,lambda q:None) # screen/time refresh tick only
stub(u,0x8102a4c,lambda q:None);stub(u,0x8108950,lambda q:None)
stub(u,0x8126578,lambda q:None) # input/transport polling boundary
w32(u,APP+0xc00,0);w32(u,APP+0xc04,0)
u.mem_write(APP+0x7398,struct.pack('<H',7))
def drain():call(u,0x81343c4,R)
def bindings():return [struct.unpack('<H',u.mem_read(T+o+0x94,2))[0] for o in OFFSETS]
def start(action=5):call(u,0x8122508,APP,action,0,0);drain()
def press():
 u.mem_write(EV,struct.pack('<6I',12,0x8148664,0,0,0,0));call(u,0x8123862,APP,EV);drain()
call(u,0x8120264,APP+0xe78,0x18c,1)   # OUT2 Mode Stereo: no OUT3, so no third page (S25); Mono below
start()
check(bindings()==NORMAL,'native root queue presents all original TRACK controls on entry')
for cycle in range(3):
 for step,(active,group) in enumerate([(0,1),(1,0),(1,1),(0,0)]):
  press()
  expected=IDS+NORMAL[5:] if active else NORMAL
  check(bindings()==expected and u32(u,T+0xebbc)==group,'physical A dispatch + native queue restores correct bank '+str((cycle,step)))
  check(all(u.mem_read(T+o+0x30,1)==bytes([int(active and i>=5)]) for i,o in enumerate(OFFSETS)),'correct visibility through native layout/rebinding '+str((cycle,step)))
# S25, Mono: A from inserts right reaches the third page (the eighth knob
# alone, OUT3 since Track Knob 8 is Gate), then normal left.
call(u,0x8120264,APP+0xe78,0x18c,0)
for step,(active,group) in enumerate([(0,1),(1,0),(1,1),(4,1),(0,0)]):
 press()
 shown=[i for i,o in enumerate(OFFSETS) if u.mem_read(T+o+0x30,1)==b'\0']
 ok=u32(u,0x388001e4)==active and u32(u,T+0xebbc)==group
 if active==4:ok=ok and shown==[7] and bindings()[7]==0x15f
 if active==0:ok=ok and bindings()==NORMAL
 check(ok,'Mono: A cycle with the third page (OUT3 alone) '+str((step,active,group,shown,hex(bindings()[7]))))
call(u,0x8120264,APP+0xe78,0x18c,1)
# Include touch-driven group movement through the genuine group-observer hook.
call(u,S['insert_track_group'],T,1);press()
check(bindings()[:5]==IDS and u32(u,T+0xebbc)==0,'A uses the observed TRACK focus after touch-style right selection')
# Normal controls remain untouched by changing a saved insert mode and returning.
for mode in range(5):
 u.mem_write(EV,struct.pack('<6I',0x6e,0x8148664,7,IDS[0],mode,0));call(u,u32(u,0x814bb6c),T,EV);drain()
 check(call(u,0x8120234,APP+0xc08+7*0x30,IDS[0])==mode,'mode saved and rebound through real native model queue '+str(mode))
press();press()
check(bindings()==NORMAL,'native knobs restored after mode edits and two A presses')
# Exact original parameter encoders (including shared Gate), both groups.
turns=[]
u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:turns.append(struct.unpack('<H',q.mem_read(q.reg_read(UC_ARM_REG_R0)+0x94,2))[0]),begin=S['gate_turn'],end=S['gate_turn'])
for group in (0,1):
 if u32(u,T+0xebbc)!=group:press()
 for enc,pid in enumerate(([4,3,0x63,0x15d],[0xda,0x15e,0xdb,0x1ae])[group]):
  commands.clear();parents.clear();turns.clear()
  u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,enc,(-800)&0xffffffff,0));call(u,u32(u,0x814bb70),T,EV)
  edits=[struct.unpack('<6I',e) for e in parents if struct.unpack_from('<H',e)[0]==0x6e]
  check(turns==[pid],'restored encoder targets original parameter '+hex(pid))
  drain()
# Return left, then open the native B input action (queue only; target page
# intentionally not constructed, so do not present it in this fixture).
if u32(u,T+0xebbc):
 press();press();press() # normal right -> insert left/right -> normal left
u.mem_write(EV,struct.pack('<6I',7,0x8148664,0,0,0,0));call(u,0x8123862,APP,EV)
queue=TMP+0x600;ok=call(u,0x8121b48,APP,queue)
check(ok and struct.unpack('<H',u.mem_read(queue,2))[0]==0x8d,'B retains native input-configuration action')
# MIXER's independent A event remains stock and never changes insert banks.
state_before=bytes(u.mem_read(0x388001e0,16));u.mem_write(APP+0x739a,b'\2')
u.mem_write(EV,struct.pack('<6I',12,0x8148664,0,0,0,0));call(u,0x8123862,APP,EV)
ok=call(u,0x8121b48,APP,queue)
check(ok and struct.unpack('<H',u.mem_read(queue,2))[0]==0x164 and bytes(u.mem_read(0x388001e0,16))==state_before,'MIXER A remains the exact stock event and cannot toggle TRACK insert banks')
print(H.checks,'complete native navigation checks passed; physical LCD/touch still needs device testing.')
