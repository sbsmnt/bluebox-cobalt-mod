"""Native visible group -> coordinate hit -> button -> encoder -> shared Gate.
The old tests synthesized a selector event for an assumed output-mode mapping.
This test derives the active group from native visibility, without assuming
which setting value means Quad. Optional --source DIR checks older builds.
"""
import sys,pathlib
if '--source' in sys.argv:
 i=sys.argv.index('--source');sys.path.insert(0,str(pathlib.Path(sys.argv[i+1]).resolve()/'tests'))
from gate_ui_fixture import *
def half(a):return struct.unpack('<H',u.mem_read(a,2))[0]
def hidden(a):return bytes(u.mem_read(a+0x30,1))!=b'\0'
for mode_value in (1,0,1):
 # Setup selects the first page initially, as it does when arriving from MAIN.
 call(u,0x8120264,APP+0xe78,0x18c,mode_value)
 call(u,S['gate_mix_setup'],P,0);call(u,S['gate_mix_select'],P,0)
 check(half(P+0xbca0)==4,'native first page remains Vol, setting '+str(mode_value))
 call(u,S['gate_mix_setup'],P,1)
 visible=[g for g in (P+0xcb7c,P+0xcef4) if not hidden(g)]
 check(len(visible)==1,'native setup exposes exactly one sends group')
 group=visible[0];is_six_outputs=group==P+0xcef4
 button=P+(0xbfe4 if is_six_outputs else 0xc3a4)
 check(not hidden(button),'Gate child and its actual parent are both visible')
 x,y,w,h=struct.unpack('<4i',u.mem_read(button+4,16))
 u.mem_write(TMP,struct.pack('<2i',x+w//2,y+h//2));w32(u,TMP+16,0)
 check(call(u,0x813a25e,group,TMP,TMP+16)==1 and u32(u,TMP+16)==button,'native coordinate hit resolves the displayed Gate button')
 call(u,0x8137de0,button);call(u,0x8137e44,button)
 check(half(P+0xbca0)==0x1ae,'press/release on visible Gate selects Gate, never OUT3')
 for ch in (0,2,11):
  call(u,0x8120264,APP+0xc08+ch*0x30,0x1ae,(-60000)&0xffffffff)
  u.mem_write(TMP,struct.pack('<6I',0x63,0x8148664,ch,0,0,0))
  call(u,S['gate_mix_event'],P,TMP);call(u,0x8133194,T,TMP)
  # Bank maps encoder 0/1 to alternating stereo-track slots.
  w32(u,P+0xbcbc,ch//2);u.mem_write(P+0xbcc0,b'\0')
  k=P+0xbe0+ch*0xd98+0x8f0
  check(half(k+0x94)==0x1ae and val(k)==-60000,'visible selection binds Gate threshold for track '+str(ch+1))
  commands.clear();notifications.clear();parents.clear()
  clock[0]+=1000;w32(u,0x3001eb3c,clock[0])
  u.mem_write(TMP,struct.pack('<6I',0x32,0x8148664,0,ch%2,160,0))
  call(u,S['gate_mix_event'],P,TMP);v=val(k)
  check(v>-60000 and commands==[(ch,0x1ae,v&0xffffffff)],'MIXER physical encoder emits exactly one Gate edit for track '+str(ch+1))
  flush()
  check(val(T+0xde48)==v and call(u,0x8120234,APP+0xc08+ch*0x30,0x1ae)==v&0xffffffff,'TRACK and shared model reflect the visible MIXER edit')
  # Reverse direction through native TRACK local handler and refresh fan-out.
  target=v-1000
  u.mem_write(TMP,struct.pack('<6I',0x6e,0x8148664,ch,0x1ae,target&0xffffffff,0))
  call(u,S['gate_track_local'],T,TMP);flush()
  check(val(k)==target and val(T+0xde48)==target,'TRACK edit refreshes the selected MIXER Gate')
 if is_six_outputs:
  # Its penultimate control must remain OUT3, not be remapped to Gate.
  call(u,0x8137e44,P+0xc764)
  check(half(P+0xbca0)==0x15f,'six-output group retains a separate OUT3 button')
 # Native D4 rebuild must restore the saved Gate selection.
 call(u,S['gate_mix_select'],P,6)
 u.mem_write(TMP,struct.pack('<6I',0xd4,0x8148664,0,0,0,0))
 call(u,S['gate_mix_event'],P,TMP)
 check(half(P+0xbca0)==0x1ae,'native page rebuild retains Gate selection')
print(str(H.checks)+' visible-button selection and shared-edit checks passed; hardware retest required.')
