"""MIXER sends page: pressing the visible Gate button again switches the row to
per-track Sat and back. Native coordinate hit, button press, selector table,
column binder, encoder and model setter execute; only allocation, IPC and
notification fan-out boundaries are substituted (gate_ui_fixture)."""
from gate_ui_fixture import *
TID=0x1ba
def half(a):return struct.unpack('<H',u.mem_read(a,2))[0]
def hidden(a):return bytes(u.mem_read(a+0x30,1))!=b'\0'
def label(b):return bytes(u.mem_read(b+0x65,8)).split(b'\0')[0]
check(struct.unpack('<H',patched[0x0814b7f6-0x08040000:][:2])[0]==TID and stock[0x0814b7f6-0x08040000:][:2]==bytes(2),'eighth sends selector: spare stock halfword now selects Sat')
def press(group,button):
 x,y,w,h=struct.unpack('<4i',u.mem_read(button+4,16))
 u.mem_write(TMP,struct.pack('<2i',x+w//2,y+h//2));w32(u,TMP+16,0)
 check(call(u,0x813a25e,group,TMP,TMP+16)==1 and u32(u,TMP+16)==button,'native coordinate hit resolves the Gate/Sat button')
 call(u,0x8137de0,button);call(u,0x8137e44,button)
for mode_value in (1,0):
 call(u,0x8120264,APP+0xe78,0x18c,mode_value)
 call(u,S['gate_mix_setup'],P,0);call(u,S['gate_mix_select'],P,0)
 call(u,S['gate_mix_setup'],P,1)
 group=[g for g in (P+0xcb7c,P+0xcef4) if not hidden(g)][0];seven=group==P+0xcef4
 button=P+(0xbfe4 if seven else 0xc3a4);names=(b'NG',b'SAT') if seven else (b'Gate',b'Sat')
 tag='seven buttons' if seven else 'six buttons'
 call(u,S['gate_mix_select'],P,1)  # start from OUT1, not a restored Gate row
 press(group,button)
 check(half(P+0xbca0)==0x1ae and label(button)==names[0],tag+': first press selects Gate, labelled '+names[0].decode())
 press(group,button)
 check(half(P+0xbca0)==TID and label(button)==names[1],tag+': second press switches the row to Sat, labelled '+names[1].decode())
 for ch in (0,3,11):
  call(u,0x8120264,APP+0xc08+ch*0x30,TID,250)
  u.mem_write(TMP,struct.pack('<6I',0x63,0x8148664,ch,0,0,0))
  call(u,S['gate_mix_event'],P,TMP);call(u,0x8133194,T,TMP)
  w32(u,P+0xbcbc,ch//2);u.mem_write(P+0xbcc0,b'\0')
  k=P+0xbe0+ch*0xd98+0x8f0
  check(half(k+0x94)==TID and val(k)==250,tag+': column binds the stored Sat value for track '+str(ch+1))
  commands.clear();notifications.clear();parents.clear()
  clock[0]+=1000;w32(u,0x3001eb3c,clock[0])
  u.mem_write(TMP,struct.pack('<6I',0x32,0x8148664,0,ch%2,160,0))
  call(u,S['gate_mix_event'],P,TMP);v=val(k)
  check(v>250 and commands==[(ch,TID,v)],tag+': encoder emits exactly one Sat edit to the audio core, track '+str(ch+1))
  flush()
  check(call(u,0x8120234,APP+0xc08+ch*0x30,TID)==v,tag+': shared model stores the Sat edit, track '+str(ch+1))
  # Child (touch) edit commits once, clamped, through the native setter.
  commands.clear()
  u.mem_write(TMP,struct.pack('<6I',0x6e,0x8148664,ch,TID,5000,0))
  call(u,S['gate_mix_local'],P,TMP)
  check(commands==[(ch,TID,1000)],tag+': touch edit commits once and clamps to 100%, track '+str(ch+1))
 main=P+0xbe0+12*0xd98+0x8f0
 check(bytes(u.mem_read(main+0x2d4,1))==b'\1','Main column stays disabled on the Sat row')
 commands.clear();u.mem_write(TMP,struct.pack('<6I',0x6e,0x8148664,12,TID,500,0))
 call(u,S['gate_mix_event'],P,TMP);check(commands==[],'Main Sat edits are ignored')
 # Native page rebuild restores Sat (saved index 7), not Gate.
 u.mem_write(TMP,struct.pack('<6I',0xd4,0x8148664,0,0,0,0))
 call(u,S['gate_mix_event'],P,TMP)
 check(half(P+0xbca0)==TID and label(button)==names[1],tag+': native page rebuild keeps the Sat row and label')
 call(u,S['gate_mix_setup'],P,1)
 check(half(P+0xbca0)==TID and label(button)==names[1],tag+': re-entering the sends page keeps the Sat row and label')
 press(group,button)
 check(half(P+0xbca0)==0x1ae and label(button)==names[0],tag+': third press returns to Gate')
 u.mem_write(TMP,struct.pack('<6I',0xd4,0x8148664,0,0,0,0));call(u,S['gate_mix_event'],P,TMP)
 check(half(P+0xbca0)==0x1ae,tag+': native page rebuild keeps Gate (no accidental switch)')
 # Other sends buttons are unaffected: OUT1 then Gate selects Gate, not Sat.
 call(u,S['gate_mix_press'],P,1);call(u,S['gate_mix_press'],P,5 if not seven else 6)
 check(half(P+0xbca0)==0x1ae,tag+': Gate pressed from another send selects Gate first')
print(str(H.checks)+' MIXER Gate/Sat row checks passed; physical touch and display require hardware tests.')
