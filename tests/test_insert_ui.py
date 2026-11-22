"""Native TRACK widget banks, input bubbling and model/IPC commits.
Notification fanout is a boundary, as in the released Gate fixture. Actual
constructors, bindings, encoder arithmetic, local handlers and model run.
"""
from gate_ui_fixture import *
import firmware_harness as H
IU=0x388001e0;OFFSETS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48]
IDS=list(range(0x1ba,0x1bf));EV=TMP+0x400
global_fn=u32(u,0x814bb70);local_fn=u32(u,0x814bb6c)
stub(u,0x8102a4c,lambda q:None);stub(u,0x8108950,lambda q:None)
def binding(i):return struct.unpack('<H2xi',u.mem_read(T+OFFSETS[i]+0x94,8))
def bind_notify(group):
 call(u,S['insert_track_group'],T,group)
 u.mem_write(EV,struct.pack('<6I',0x67,0x8148664,0,0,0,0));call(u,global_fn,T,EV)
def press_a(action):
 u.mem_write(APP+0x739a,bytes([5 if action==2 else 6]));notifications.clear()
 u.mem_write(EV,struct.pack('<6I',12,0x8148664,0,0,0,0));call(u,0x8123862,APP,EV)
 return notifications[:]
setup(0,7,-96000);normal=[binding(i)[0] for i in range(8)]
check(normal==[4,3,0x63,0x15d,0xda,0xdb,0x15e,0x1ae],'all native TRACK knobs and Gate retain original binding order before insert banks')
e=press_a(2)
check(len(e)==1 and struct.unpack_from('<H',e[0])[0]==0x8c and u32(u,IU+4)==0,'A from native left retains the normal bank-cycle event')
# Native action3/group1 is the resulting notification fanout boundary.
u.mem_write(APP+0x739a,b'\3');bind_notify(1)
e=press_a(3)
check(len(e)==1 and struct.unpack_from('<H',e[0])[0]==0x8c and struct.unpack_from('<I',e[0],12)[0]==0 and u32(u,IU+4)==1 and bytes(u.mem_read(APP+0x739a,1))==b'\5','A after normal right opens insert left via original native page action')
bind_notify(0)
check([binding(i)[0] for i in range(5)]==IDS,'actual native TRACK widgets bind Insert FX, Drive, Tone, Mix and Output')
check([binding(i)[1] for i in range(5)]==[0,350,650,1000,0],'native insert widgets show the selected track defaults')
check(all(u.mem_read(T+OFFSETS[i]+0x30,1)==b'\1' for i in range(5,8)),'three unused native knobs are hidden and cannot receive insert-bank input')
# Touch focus goes through native routing with correct bank, without editing
# any original parameter or rewriting the original child event.
parents.clear();u.mem_write(EV,struct.pack('<6I',0x16a,0x8148664,7,IDS[0],0,0));call(u,local_fn,T,EV)
check(bytes(u.mem_read(EV,24))==struct.pack('<6I',0x16a,0x8148664,7,IDS[0],0,0),'insert touch focus preserves the original child event')
commands.clear();notifications.clear()
for encoder,pid in enumerate(IDS[:4]):
 old=call(u,0x8120234,APP+0xc08+7*0x30,pid);commands.clear();notifications.clear()
 # Slow native precision, several raw units to cross discrete/percentage steps.
 delta=400 if encoder==0 else 80 if encoder<3 else -80
 w32(u,0x3001eb3c,1000000+encoder*10000)
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,encoder,delta&0xffffffff,0));call(u,global_fn,T,EV)
 now=call(u,0x8120234,APP+0xc08+7*0x30,pid)
 print('INFO encoder',encoder,'old/new',old,now,'commands',commands,'widget',binding(encoder))
 # The first type pick on a fresh track also commits INSRT Off once; it
 # lands on SAT (second DRV type), which also sets its starting Drive / CURVE / Mix.
 check(now!=old and commands==([(7,0x1d5,0)] if encoder==0 else [])+[(7,pid,now)]+([(7,0x1bb,300),(7,0x1bc,0),(7,0x1bd,1000)] if encoder==0 else []),'real hardware encoder edits only its new parameter and sends one exact IPC command '+hex(pid))
 flush()
 check(binding(encoder)[1]==(now if now<0x80000000 else now-0x100000000),'native notification refresh retains insert binding/value '+hex(pid))
check(call(u,0x8120234,APP+0xc08+7*0x30,4)==0 and call(u,0x8120234,APP+0xc08+7*0x30,0x1ae)==((-96000)&0xffffffff),'insert encoder turns leave native Vol and Gate untouched')
# A from insert left keeps insert mode while cycling native focus right.
e=press_a(2);check(len(e)==1 and struct.unpack_from('<H',e[0])[0]==0x8c and u32(u,IU+4)==1,'A cycles insert left to insert right through native group notification')
u.mem_write(APP+0x739a,b'\3');bind_notify(1);commands.clear();notifications.clear()
u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,0,(-1000)&0xffffffff,0));call(u,global_fn,T,EV)
check(commands and commands[-1][0:2]==(7,IDS[4]) and call(u,0x8120234,APP+0xc08+7*0x30,IDS[4])>0xffff0000,'insert right encoder0 edits negative Output through native dB control')
commands.clear()
for encoder in (1,2,3):
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,encoder,10,0));call(u,global_fn,T,EV)
check(not commands,'unused insert right encoders never change hidden native controls')
# Selecting a different track via native global notifications rebinds all five.
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
check([binding(i)[1] for i in range(5)]==[0,350,650,1000,0] and all(struct.unpack('<H',u.mem_read(T+OFFSETS[i]+0x32,2))[0]==2 for i in range(5)),'changing selected track shows its own saved insert state and encoder targets')
# Complete selector travel reaches consecutive choices, while percentages
# retain native fine travel. Touch-focus stays on its actual bank.
bind_notify(0)
FRACTION={0:0.,1:0.,2:0.,15:1/3.,3:2/3.,4:1.,5:0.,6:1/3.,7:2/3.,8:1.,9:0.,10:.2,11:.4,12:.6,13:.8,14:1.}
GB=u32(u,0x38800c08)  # the constructed DRV / FLTR toggle
def group_label():return b'FLTR' if b'FLTR' in bytes(u.mem_read(GB,0xf8)) else b'SPEC' if b'SPEC' in bytes(u.mem_read(GB,0xf8)) else b'DRV' if b'DRV' in bytes(u.mem_read(GB,0xf8)) else b''
def tap_group():
 commands.clear();notifications.clear()
 call(u,u32(u,u32(u,GB)+0x18),GB,u32(u,GB+4)+10,u32(u,GB+8)+10);flush()
def text(i):return bytes(u.mem_read(T+OFFSETS[i]+0xd1,32)).split(b'\0')[0]
# TRACK knobs draw one text line (+0xa0, buffer +0xd1): the caption, except
# Drive in filter types, which shows the cutoff value.
caption=text
def saved(pid):v=call(u,0x8120234,APP+0xc08+2*0x30,pid);return v-(1<<32) if v>=1<<31 else v
# A fresh track stores 0 and shows CLIP; there is no Off position. The type
# encoder stays in the shown group; the DRV / FLTR button ('G') switches group.
check(GB and u.mem_read(GB+0x30,1)==b'\0' and group_label()==b'DRV' and struct.unpack('<H',u.mem_read(GB+0xfa,2))[0]==0x1d6,'insert page shows the DRV group button')
steps=[(-400,0),(400,15),(400,3),(400,4),(400,4),(-400,3),(-400,15),(-400,1),(-400,1),('G',5),(400,6),(400,7),(400,8),(400,8),(-400,7),(-400,6),(-400,5),(-400,5),
       ('G',9),(400,10),(400,11),(400,12),(400,13),(400,14),(400,14),(-400,13),(-400,12),(-400,11),(-400,10),(-400,9),(-400,9),
       ('G',1),(400,15),('G',5),('G',9),('G',1)]
def want_group(m):return b'FLTR' if 5<=m<=8 else b'SPEC' if 9<=m<=14 else b'DRV'
previous=0
for delta,expected in steps:
 commands.clear();notifications.clear()
 if delta=='G':tap_group()
 else:
  u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,0,delta&0xffffffff,0));call(u,global_fn,T,EV);flush()
 check(binding(0)[1]==expected and group_label()==want_group(expected),'Insert selector steps inside DRV (CLIP / SAT / FOLD / WRAP), FLTR (LP / BP / HP / DJ) and SPEC (SHFT / SPRD / RING / HARM / CHRD / BELL), no Off '+str((delta,expected)))
 call(u,S['dly_knob_fraction'],T+OFFSETS[0])
 check(abs(struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]-FRACTION[expected])<1e-6,'Insert knob spans its group on the full visual scale '+str(expected))
 if 9<=expected<=11:
  check([caption(i) for i in (1,2,3)]==[b'SHIFT',b'FDBK',b'MIX'],'shifter captions SHIFT / FDBK / MIX while idle '+str((expected,[caption(i) for i in (1,2,3)])))
  if not 9<=previous<=11:check(saved(0x1bb)==750 and saved(0x1bc)==0 and saved(0x1bd)==500,'entering the shifter: +14 Hz, no feedback, half mix '+str(expected))
  else:check(saved(0x1bb)==750 and saved(0x1bc)==0 and saved(0x1bd)==500,'shifter types keep their values '+str(expected))
 elif expected==15:
  check([caption(i) for i in (1,2,3)]==[b'DRIVE',b'CURVE',b'MIX'] and saved(0x1bb)==300 and saved(0x1bc)==0 and saved(0x1bd)==1000,'SAT (PR #7 exploration): Tone reads CURVE; entering SAT starts at ~11 dB drive, Soft curve, full mix '+str([caption(i) for i in (1,2,3)]))
 elif expected>=12:
  check([caption(i) for i in (1,2,3)]==[b'PITCH',b'DECAY',b'MIX'],'resonator captions PITCH / DECAY / MIX while idle '+str((expected,[caption(i) for i in (1,2,3)])))
  check(saved(0x1bb)==167 and saved(0x1bc)==547 and saved(0x1bd)==600,'resonator types start (and stay) at C2, 1 s decay, 60% mix '+str(expected))
 elif expected>=5:
  opened={5:1000,6:500,7:0,8:500}[expected]
  check(saved(0x1bb)==opened and binding(1)[1]==opened,'filter type opens its cutoff '+str(expected))
  check([caption(i) for i in (2,3)]==[b'RES',b'ENV'],'filter captions RES / ENV '+str(expected))
  check(saved(0x1bd)==500,'Env starts at centre (off) '+str(expected))
  if expected==5:check(saved(0x1bc)==300 and text(1)==b'20K','LP opens at 20 kHz with Resonance 30%')
  if expected==7:check(text(1)==b'20','HP opens at 20 Hz')
  if expected==8:check(text(1)==b'OFF','DJ starts at its dry centre')
 elif expected in (1,3,4):
  check([caption(i) for i in (1,2,3)]==[b'DRIVE',b'TONE',b'MIX'] and saved(0x1bb)==350 and saved(0x1bc)==650 and saved(0x1bd)==1000,'drive types use drive labels and drive defaults '+str(expected))
 previous=expected
# F4: SPEC Shift / Pitch / Decay show their value while turning (the native
# edit path formats with caption 0), keep it through a rebinding, and show
# the caption again ~1 s after the last turn (periodic UI tick).
CLOCK=0x3001eb3c
def turn_text(i):call(u,S['insert_knob_text'],T+OFFSETS[i],0);return text(i)
w32(u,CLOCK,5000)
for mode,pid,value,want in [(9,0x1bb,500,b'0'),(9,0x1bb,510,b'0'),(9,0x1bb,512,b'+0.1'),(9,0x1bb,879,b'+204'),(9,0x1bb,121,b'-204'),(9,0x1bb,1000,b'+2.5K'),(10,0x1bb,0,b'-2.5K'),(11,0x1bb,121,b'204'),(9,0x1bb,600,b'+0.6'),
                            (12,0x1bb,167,b'C2'),(12,0x1bb,0,b'C1'),(12,0x1bb,1000,b'C7'),(13,0x1bb,500,b'C4'),(12,0x1bb,264,b'G2'),(14,0x1bb,250,b'F#2'),
                            (12,0x1bc,0,b'50MS'),(12,0x1bc,250,b'197MS'),(12,0x1bc,547,b'1.0S'),(14,0x1bc,1000,b'12S')]:
 call(u,0x8120264,APP+0xc08+2*0x30,0x1ba,mode);call(u,0x8120264,APP+0xc08+2*0x30,pid,value)
 i=pid-0x1ba;w32(u,CLOCK,u32(u,CLOCK)+5000);u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
 # F6: a value that changed under the page (same track and type) shows
 # first; the caption is back after the tick.
 w32(u,CLOCK,u32(u,CLOCK)+1100);call(u,S['insert_caption_tick'],TMP+0x800)
 idle=text(i);shown=turn_text(i)
 check(idle==({0x1bb:b'SHIFT' if mode<12 else b'PITCH',0x1bc:b'DECAY'}[pid]) and shown==want,'SPEC knob: caption idle, value while turning '+str((mode,pid,value,idle,shown)))
# The value stays through a rebinding inside the second, then the caption returns.
call(u,0x8120264,APP+0xc08+2*0x30,0x1ba,12);call(u,0x8120264,APP+0xc08+2*0x30,0x1bb,264)
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV);turn_text(1)
w32(u,CLOCK,u32(u,CLOCK)+400);u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
PAGES=TMP+0x800;u.mem_write(PAGES,bytes(0x40));call(u,S['insert_caption_tick'],PAGES)
check(text(1)==b'G2' and u.mem_read(PAGES+0x38,1)==b'\0','within the second a rebinding and the UI tick keep the value')
w32(u,CLOCK,u32(u,CLOCK)+700);call(u,S['insert_caption_tick'],PAGES)
check(text(1)==b'PITCH' and u.mem_read(PAGES+0x38,1)==b'\1' and u32(u,0x38800c0c)==0,'after ~1 s the UI tick restores the caption and asks for a redraw')
call(u,S['insert_caption_tick'],PAGES);check(text(1)==b'PITCH','the tick is idle afterwards')
# A real encoder turn on Pitch goes through the native edit path (caption 0).
u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,1,80,0));call(u,global_fn,T,EV);flush()
check(text(1)!=b'PITCH' and text(1)[:1] in b'CDEFGAB','a real Pitch turn shows the note '+str(text(1)))
w32(u,CLOCK,u32(u,CLOCK)+1100);call(u,S['insert_caption_tick'],PAGES);check(text(1)==b'PITCH','and the caption returns after it')
# F6: a mapped CC moving Pitch / Decay on the shown track arrives as a value
# change and native event 0x66 (rebinds the page with captions): the moved
# knobs show their values, each returns to its caption ~1 s later.
CUR=struct.unpack('<H',u.mem_read(T+0xbc9c,2))[0]
def cc_move(pid,value):
 call(u,0x8120264,APP+0xc08+CUR*0x30,pid,value)
 u.mem_write(EV,struct.pack('<6I',0x66,0x8148664,CUR,0,0,0));call(u,global_fn,T,EV)
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,CUR,0,0,0));call(u,global_fn,T,EV)
call(u,0x8120264,APP+0xc08+CUR*0x30,0x1ba,13);call(u,0x8120264,APP+0xc08+CUR*0x30,0x1bb,167);call(u,0x8120264,APP+0xc08+CUR*0x30,0x1bc,547)
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,CUR,0,0,0));call(u,global_fn,T,EV)
w32(u,CLOCK,u32(u,CLOCK)+5000);u.mem_write(PAGES,bytes(0x40));call(u,S['insert_caption_tick'],PAGES)
check([text(i) for i in (1,2,3)]==[b'PITCH',b'DECAY',b'MIX'],'CHRD idle before the CC '+str([text(i) for i in (1,2,3)]))
cc_move(0x1bb,264)
check(text(1)==b'G2' and text(2)==b'DECAY','a CC on Pitch shows the note on the shown track '+str((text(1),text(2))))
w32(u,CLOCK,u32(u,CLOCK)+300);cc_move(0x1bb,278)
check(text(1)==b'G#2' and text(2)==b'DECAY','further CC moves follow '+str((text(1),text(2))))
w32(u,CLOCK,u32(u,CLOCK)+300);cc_move(0x1bc,1000);cc_move(0x1bd,800)
print('INFO mix CC text',text(3))
check(text(1)==b'G#2' and text(2)==b'12S' and text(3)==b'80%','a second and third knob show their values too '+str((text(1),text(2),text(3))))
w32(u,CLOCK,u32(u,CLOCK)+800);u.mem_write(PAGES,bytes(0x40));call(u,S['insert_caption_tick'],PAGES)
check(text(1)==b'PITCH' and text(2)==b'12S' and text(3)!=b'MIX' and u.mem_read(PAGES+0x38,1)==b'\1','each knob returns ~1 s after its own last move '+str((text(1),text(2))))
w32(u,CLOCK,u32(u,CLOCK)+400);call(u,S['insert_caption_tick'],PAGES)
check([text(i) for i in (1,2,3)]==[b'PITCH',b'DECAY',b'MIX'] and u32(u,0x38800c0c)==0,'then all captions are back '+str([text(i) for i in (1,2,3)]))
u.mem_write(EV,struct.pack('<6I',0x66,0x8148664,CUR,0,0,0));call(u,global_fn,T,EV)
check([text(i) for i in (1,2,3)]==[b'PITCH',b'DECAY',b'MIX'],'a rebind with no value change keeps the captions')
# F6: CHRD's chord knob beside Output (right page, second encoder), only in CHRD.
def shown(i):return u.mem_read(T+OFFSETS[i]+0x30,1)==b'\0'
check(shown(5) and binding(5)[0]==0x1de and text(5)==b'MIN9','CHRD shows the chord knob (MIN9 by default) '+str((binding(5),text(5))))
u.mem_write(APP+0x739a,b'\3');bind_notify(1);commands.clear()
names=[b'MIN9',b'MAJ9',b'MIN7',b'MAJ7',b'SUS',b'5THS',b'4THS',b'DIM']
seen=[]
for d in [400]*8+[-400]*9:
 commands.clear();u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,2,d&0xffffffff,0));call(u,global_fn,T,EV);flush()
 seen.append((text(5),call(u,0x8120234,APP+0xc08+CUR*0x30,0x1de),commands[:]))
check([x[0] for x in seen]==names[1:]+[b'DIM']+names[-2::-1]+[b'MIN9']*2 and all(x[1]==names.index(x[0]) for x in seen),'the top-right encoder steps the 8 chords and stops at each end '+str([x[0] for x in seen]))
check(seen[0][2]==[(CUR,0x1de,1)] and seen[7][2]==[],'each step commits one chord command; past the end nothing')
for enc in (1,3):
 commands.clear();u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,enc,400,0));call(u,global_fn,T,EV);flush()
 check(not commands,'F7: right encoder %d (bottom row) does not change the chord'%enc)
w32(u,CLOCK,u32(u,CLOCK)+100);call(u,0x8120264,APP+0xc08+CUR*0x30,0x1de,6)
u.mem_write(EV,struct.pack('<6I',0x66,0x8148664,CUR,0,0,0));call(u,global_fn,T,EV)
check(text(5)==b'4THS','a chord CC shows the new chord name')
bind_notify(0)
for mode in (9,12,14,5,1):
 call(u,0x8120264,APP+0xc08+CUR*0x30,0x1ba,mode)
 u.mem_write(EV,struct.pack('<6I',0x66,0x8148664,CUR,0,0,0));call(u,global_fn,T,EV)
 check(not shown(5),'no chord knob outside CHRD '+str(mode))
call(u,0x8120264,APP+0xc08+CUR*0x30,0x1ba,13);u.mem_write(EV,struct.pack('<6I',0x66,0x8148664,CUR,0,0,0));call(u,global_fn,T,EV)
check(shown(5) and text(5)==b'4THS','back in CHRD the chord is kept')
call(u,0x8120264,APP+0xc08+CUR*0x30,0x1de,0)
# Drive's text follows Cutoff / DJ; Env stays captioned ENV at any depth.
for mode,pid,value,want in [(5,0x1bd,900,b'ENV'),(5,0x1bd,100,b'ENV'),(5,0x1bb,500,b'632'),(5,0x1bb,800,b'5.0K'),(5,0x1bb,1000,b'20K'),(8,0x1bb,250,b'LP 1.1K'),(8,0x1bb,750,b'HP 420'),(8,0x1bb,1000,b'HP 10K'),(8,0x1bb,0,b'LP 50')]:
 call(u,0x8120264,APP+0xc08+2*0x30,0x1ba,mode);call(u,0x8120264,APP+0xc08+2*0x30,pid,value)
 i=pid-0x1ba;u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
 check(text(i)==want,'filter value text '+str((mode,pid,value,want,text(i))))
# Captions survive page refreshes and Res/Env turns (turns never rewrite them).
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
check([text(i) for i in (1,2,3)]==[b'LP 50',b'RES',b'ENV'],'page refresh in a filter type keeps RES / ENV '+str([text(i) for i in (1,2,3)]))
for enc,delta in ((2,80),(3,-80),(3,80),(1,80)):
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,enc,delta&0xffffffff,0));call(u,global_fn,T,EV);flush()
check(text(2)==b'RES' and text(3)==b'ENV' and text(1).startswith(b'LP ') and text(1)!=b'LP 50','turning Res and Env keeps their captions; Cutoff shows its new value '+str(text(1)))
call(u,0x8120264,APP+0xc08+2*0x30,0x1ba,0)
for pid,v in ((0x1bb,350),(0x1bc,650),(0x1bd,1000)):call(u,0x8120264,APP+0xc08+2*0x30,pid,v)
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
for quad in (0,1):
 call(u,0x8120264,APP+0xe78,0x18c,quad)
 for ch in range(12):
  u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,ch,0,0,0));call(u,global_fn,T,EV)
  rects=[struct.unpack('<4i',u.mem_read(T+off+4,16)) for off in OFFSETS[:5]]
  check(all(x>=0 and y>=0 and x+w<=320 and y+h<=240 and w>0 and h>0 for x,y,w,h in rects) and [binding(i)[0] for i in range(5)]==IDS,'all five native insert controls fit and retarget correctly '+str((quad,ch)))
# INSRT: the native toggle under Output, touch only; its lit state shows On.
B=T+0xe814;ON=0x1d5
def lit():return u.mem_read(B+0x96,1)[0]
def button_label():return b'INSRT' if b'INSRT' in bytes(u.mem_read(B,0xf8)) else b''
def tap():
 commands.clear();notifications.clear()
 call(u,u32(u,u32(u,B)+0x18),B,u32(u,B+4)+10,u32(u,B+8)+10);flush()
u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
out_rect=struct.unpack('<4i',u.mem_read(T+OFFSETS[4]+4,16));b_rect=struct.unpack('<4i',u.mem_read(B+4,16))
def fresh():
 call(u,0x8120264,APP+0xc08+2*0x30,0x1ba,0);call(u,0x8120264,APP+0xc08+2*0x30,ON,1)
 u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,2,0,0,0));call(u,global_fn,T,EV)
fresh()
check(u.mem_read(B+0x30,1)==b'\0' and struct.unpack('<H',u.mem_read(B+0xfa,2))[0]==ON and button_label()==b'INSRT' and lit()==0 and text(0)==b'CLIP','a fresh track shows CLIP with INSRT unlit (Off by default)')
rec_rect=struct.unpack('<4i',u.mem_read(T+0xe298+4,16));sc_rect=struct.unpack('<4i',u.mem_read(T+0xe9e8+4,16))
check(b_rect[0]==out_rect[0] and b_rect[1]+b_rect[3]==out_rect[1],'INSRT sits directly under Output '+str((out_rect,b_rect)))
g_rect=struct.unpack('<4i',u.mem_read(GB+4,16))
check(g_rect[1:]==b_rect[1:] and g_rect[0]==b_rect[0]+48,'DRV / FLTR sits beside INSRT '+str((b_rect,g_rect)))
# S8e: same 1 px border grid as the stock buttons (REC beside DRV).
check(b_rect[1:]==rec_rect[1:] and g_rect[1:]==rec_rect[1:] and rec_rect[0]==g_rect[0]+48,'INSRT and DRV / FLTR use the stock lower-row button geometry '+str((b_rect,g_rect,rec_rect)))
check(b_rect[2]==sc_rect[2] and sc_rect[0]-sc_rect[2]==1+g_rect[0],'1 px gaps between INSRT, DRV / FLTR and the stock buttons, as between REC and SOLO '+str((b_rect,g_rect,sc_rect)))
tap();check((2,0x1ba,1) in commands and saved(0x1ba)==1 and saved(ON)==1 and lit()==1,'touching INSRT on a fresh track starts its CLIP '+str(commands))
tap();check(commands==[(2,ON,0)] and saved(ON)==0 and button_label()==b'INSRT' and lit()==0,'touch switches the insert Off once through saved model/IPC')
tap();check(commands==[(2,ON,1)] and saved(ON)==1 and button_label()==b'INSRT' and lit()==1,'touch switches it back On')
bind_notify(1)
for delta in (-100,-3,1,100,-1000):
 commands.clear();notifications.clear()
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,1,delta&0xffffffff,0));call(u,global_fn,T,EV);flush()
 check(saved(ON)==1 and not commands and lit()==1,'right encoder 1 never switches the insert (encoder jitter) '+str(delta))
check(saved(0x1ba)==1 and saved(0x1bb)==350,'On/Off leaves the chosen type and its settings alone')
bind_notify(0);fresh();commands.clear();notifications.clear()
u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,0,400,0));call(u,global_fn,T,EV);flush()
check(saved(0x1ba)==15 and saved(ON)==0 and lit()==0 and (2,ON,0) in commands,'picking a type on a fresh track keeps INSRT Off '+str(commands))
fresh();tap_group()
check(saved(0x1ba)==5 and saved(ON)==0 and lit()==0 and group_label()==b'FLTR' and all(c[1]!=0x1d6 for c in commands),'FLTR on a fresh track picks LP, keeps INSRT Off and never stores the button id '+str(commands))
bind_notify(1)
bind_notify(1)
e=press_a(3);check(len(e)==1 and u32(u,IU+4)==0 and bytes(u.mem_read(APP+0x739a,1))==b'\5','fourth A bank returns to native left')
bind_notify(0)
check([binding(i)[0] for i in range(8)]==normal and all(u.mem_read(T+OFFSETS[i]+0x30,1)==b'\0' for i in (5,6,7)),'native FX2, OUT2 and Gate widgets restore after leaving insert banks')
check(struct.unpack('<H',u.mem_read(B+0xfa,2))[0]==0x173 and u.mem_read(B+0x30,1)==b'\1' and b'GAIN' in bytes(u.mem_read(B,0xf8)),'the toggle returns to the native hidden GAIN button')
check(u.mem_read(GB+0x30,1)==b'\1','the DRV / FLTR button is hidden outside the insert page')
notifications.clear();u.mem_write(EV,struct.pack('<6I',7,0x8148664,0,0,0,0));call(u,0x8123862,APP,EV)
check(len(notifications)==1 and struct.unpack_from('<H',notifications[0])[0]==0x8d,'TRACK B retains its original input-configuration event')
print(H.checks,'insert TRACK UI/input/model checks passed; physical button, touch and LCD checks remain.')
