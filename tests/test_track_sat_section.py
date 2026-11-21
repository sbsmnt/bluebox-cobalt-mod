"""TRACK third control section: A cycles first square -> second square ->
Sat square (SAT + CURVE pots) -> first. The real A dispatcher, native page switch, section
layout, knob binder, encoder router and model setter execute; the posted
page-entry event (0x8c) is delivered to the patched section call site, and
LED/IPC/notification boundaries are substituted (gate_ui_fixture)."""
from gate_ui_fixture import *
TID=0x1ba;CID=0x1bb
SESSION=0x300173d0;EV=TMP+0x200
OTHERS=[0xcb38,0xcf08,0xd2d8,0xd6a8,0xda78,0xde48]
def half(a):return struct.unpack('<H',u.mem_read(a,2))[0]
def hid(off):return u.mem_read(T+off+0x30,1)[0]
def label(w):return bytes(u.mem_read(w+0xd1,8)).split(b'\0')[0]
def page():return u.mem_read(SESSION+0x739a,1)[0]
stub(u,0x8123818,lambda u:None);stub(u,0x8102a4c,lambda u:None)  # button LED boundaries
for at,name in [(0x0812390e,'ts_track_a'),(0x0812391a,'ts_track_a'),(0x081345fe,'ts_track_section')]:
 o=at-0x08040000
 t=(S[name]&~1)-(at+4);sg=(t>>24)&1
 bl=struct.pack('<HH',0xf000|(sg<<10)|((t>>12)&1023),0xd000|(((1-((t>>23)&1))^sg)<<13)|(((1-((t>>22)&1))^sg)<<11)|((t>>1)&2047))
 check(patched[o:o+4]==bl and stock[o:o+2]!=bl[:2],'call site '+hex(at)+' enters '+name)
def press_a():
 notifications.clear()
 u.mem_write(SESSION+0x73b0,b'\0')
 u.mem_write(EV,struct.pack('<H2x5I',0x0c,0,0,0,0,0));call(u,0x8123855,SESSION,EV)
 entered=[e for e in notifications if struct.unpack_from('<H',e)[0]==0x8c]
 check(len(entered)==1,'A posts exactly one TRACK section entry (page '+str(page())+')')
 call(u,S['ts_track_section'],T,struct.unpack_from('<I',entered[0],12)[0])
def bind(ch):
 u.mem_write(INFO,struct.pack('<IH2x',0x8148664,ch));call(u,S['gate_track_bind'],T,INFO)
def encoder(index,delta):
 commands.clear();clock[0]+=1000;w32(u,0x3001eb3c,clock[0])
 u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,index,delta,0));call(u,0x8133194,T,EV)
 return commands[:]
def native_first(tag):
 check(u32(u,T+0xebbc)==0 and half(T+0xc398+0x94)==4 and label(T+0xc398)==b'VOL' and half(T+0xc768+0x94)==0x63 and label(T+0xc768)==b'PAN' and hid(0xc768)==0 and not any(hid(o) for o in OTHERS),tag+': native first square (VOL and PAN back, nothing hidden)')
w32(u,0x38800ce0,0)
for ch in range(12):call(u,0x8120264,APP+0xc08+ch*0x30,TID,100*ch);call(u,0x8120264,APP+0xc08+ch*0x30,CID,50*ch)
bind(3);u.mem_write(SESSION+0x739a,b'\5');call(u,S['ts_track_section'],T,0)
native_first('start')
press_a()
check(page()==6 and u32(u,T+0xebbc)==1 and not any(hid(o) for o in OTHERS),'first A press: second square, as stock')
press_a()
w=T+0xc398;cw=T+0xc768
check(page()==5 and u32(u,T+0xebbc)==0,'second A press: Sat section on the first-section encoders')
check(half(w+0x94)==TID and struct.unpack('<i',u.mem_read(w+0x98,4))[0]==300 and half(w+0x32)==3 and hid(0xc398)==0 and label(w)==b'SAT','Sat pot (labelled SAT) takes the first slot with track 4 stored value')
check(half(cw+0x94)==CID and struct.unpack('<i',u.mem_read(cw+0x98,4))[0]==150 and half(cw+0x32)==3 and hid(0xc768)==0 and label(cw)==b'CURVE','Curve pot (labelled CURVE) sits right of SAT with track 4 stored value')
check(all(hid(o) for o in OTHERS),'every other pot of both squares is hidden')
c=encoder(0,160)
v=struct.unpack('<i',u.mem_read(w+0x98,4))[0]
flush()
check(c==[(3,TID,v)],'encoder 1 sends exactly one Sat command to the audio core')
check(v>300 and call(u,0x8120234,APP+0xc08+3*0x30,TID)==v,'encoder 1 edits Sat and the shared model stores it')
c=encoder(2,160)
cv=struct.unpack('<i',u.mem_read(cw+0x98,4))[0]
flush()
check(c==[(3,CID,cv)],'encoder 3 sends exactly one Curve command to the audio core')
check(cv>150 and call(u,0x8120234,APP+0xc08+3*0x30,CID)==cv and call(u,0x8120234,APP+0xc08+3*0x30,TID)==v,'encoder 3 edits Curve only and the shared model stores it')
for i in (1,3):check(encoder(i,160)==[],'encoder '+str(i+1)+' reaches only a hidden pot: no edit')
for i in range(400):encoder(2,160)
flush()
check(call(u,0x8120234,APP+0xc08+3*0x30,CID)==1000,'Curve stops at full hard clip (1000)')
bind(7)
check(half(w+0x94)==TID and half(w+0x32)==7 and half(cw+0x94)==CID and half(cw+0x32)==7 and struct.unpack('<i',u.mem_read(cw+0x98,4))[0]==350 and all(hid(o) for o in OTHERS),'track change keeps Sat and Curve on the new track')
press_a()
check(page()==5,'third A press stays on the first section page')
native_first('third A press')
check(half(T+0xde48+0x94)==0x1ae and half(T+0xde48+0x32)==7,'Gate pot restored beside OUT2')
press_a();check(page()==6,'cycle repeats: second square');press_a()
check(half(w+0x94)==TID,'cycle repeats: Sat section')
call(u,S['ts_track_section'],T,1)
check(u32(u,T+0xebbc)==1 and half(w+0x94)==4 and half(cw+0x94)==0x63 and label(cw)==b'PAN' and not any(hid(o) for o in OTHERS),'auto-flip or page entry leaves the Sat section cleanly')
press_a();press_a()
check(half(w+0x94)==TID,'Sat section again before Main')
bind(12)
native_first('selecting Main (no Sat)')
press_a();check(page()==6,'after Main, A resumes the native cycle')
print(str(H.checks)+' TRACK Sat section checks passed; display, touch and button feel require hardware tests.')
