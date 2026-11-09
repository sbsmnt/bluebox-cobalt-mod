"""DJ EQ on the M4: real parameter definitions, real channel/Main defaults
and the real TRACK page (gate_ui_fixture) in DJ mode. Only show-view, the
LED call and the model setter are recorded instead of run."""
from gate_ui_fixture import *
import firmware_harness as H
D=M['symbols']['dj4'];FAKEAPP=0xc0700000;  # the app lives in SDRAM on the device
VIEWOBJ=0x24074000;UI=0x388001ec;ACTIVE=UI+16
KNOBS=[0xc398,0xc768,0xcb38,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48]
HIDE=[0xbcbc,0xc358];BUTTONS=[0xe9e8,0xe46c,0xe298,0xe640,0xe814];AUX=0x388001d0
def h16(a):return struct.unpack('<H',u.mem_read(a,2))[0]
def text(a,n=8):return bytes(u.mem_read(a,n)).split(b'\0')[0].decode()

# Definitions (fixture already built the table) and defaults.
defs=u32(u,0x3001eb30) if u32(u,0x3001eb30) else 0xc0010000
def rec(i):return struct.unpack('<HBBIIIiiI',u.mem_read(defs+i*28,28))
def key(i):return text(rec(i)[8],16)
want={0x1ba:(1,-100,100,'djhigh'),0x1bb:(1,-100,100,'djhimid'),0x1bc:(1,-100,100,'djlomid'),
      0x1bd:(1,-100,100,'djlow'),0x1be:(1,-100,100,'djfilter'),0x1bf:(8,0,1000,'djres'),
      0x1c1:(1,40,1000,'djxlo'),0x1c2:(1,300,3000,'djxmid'),0x1c3:(1,1500,12000,'djxhi')}
ok=all(rec(i)[0]==i and rec(i)[1]==t and rec(i)[6]==lo and rec(i)[7]==hi and key(i)==k for i,(t,lo,hi,k) in want.items())
check(ok,'DJ parameters defined: four bipolar bands (centre 0 dB), Filter -100..100, Res 0..1000, crossovers in Hz')
check(rec(0x1c0)[0]==0x1c0 and key(0x1c0)=='djcurve','DJ Curve defined as a choice (ISO / EQ)')
check(rec(0x1ae)[0]==0x1ae and key(0x1ae)=='ngthreshold','Cobalt definitions before ours are kept (Gate)')
def ids(table):
 out={};i=0
 while True:
  buf=0x24073000;r=call(u,0x8120a24,table,i,buf,buf+4)
  if not r:break
  out[h16(buf)]=struct.unpack('<i',u.mem_read(buf+4,4))[0];i+=1
 return out
ch2=ids(APP+0xc08+2*0x30)
check(all(ch2.get(0x1ba+i)==0 for i in range(6)) and 0x1ae in ch2,'every track gets the six DJ parameters at 0 (neutral), Gate default kept')
SET=0x24075000;u.mem_write(SET,bytes(0x40));w32(u,SET+4,0x24076000);u.mem_write(SET+10,struct.pack('<HH',0,64))
call(u,D['dj_master_default'],SET,0x160,0)
mst=ids(SET)
check([mst.get(0x1c0+i) for i in range(9)]==[0,200,1000,5000,0,1,1,0,1000] and 0x43 in mst,
      'Main defaults: ISO, 200 Hz / 1 kHz / 5 kHz, 4 bands, Default presets, 12 dB, Res Max 100 %, saturation kept')

# A real Main set from the stock builder (kind 2) has room for them all.
REAL=0x24077000;u.mem_write(REAL,bytes(0x20));u.mem_write(TMP,b'\2');call(u,0x81202b8,REAL,TMP)
real=ids(REAL);n,cap=struct.unpack('<HH',u.mem_read(REAL+10,4))
check(all(i in real for i in range(0x1c0,0x1c9)) and 0x1b8 in real and n<=cap==96,
      'stock Main set holds the DJ settings after the saturation rows (%d of %d)'%(n,cap))

# DJ view on the real TRACK page (track 3 selected).
setup(0,2,-40000)
shown=[];leds=[];sets=[]
stub(u,0x8122508,lambda u:(shown.append(u.reg_read(UC_ARM_REG_R1)),u.mem_write(VIEWOBJ+0x739a,bytes([u.reg_read(UC_ARM_REG_R1)]))))
stub(u,0x8102a4c,lambda u:leds.append(u.reg_read(UC_ARM_REG_R1)))
STORE=[0]
def model_set(u):
 ch,id,v=h16(u.reg_read(UC_ARM_REG_R1)),u.reg_read(UC_ARM_REG_R2),struct.unpack('<i',struct.pack('<I',u.reg_read(UC_ARM_REG_R3)))[0]
 sets.append((ch,id,v))
 if ch<12 and STORE[0]:   # store it, as the real setter does, so reads see it
  cell=APP+0xc08+ch*0x30;items=u32(u,cell+4)
  for i in range(h16(cell+10)):
   if h16(items+8*i)==id:u.mem_write(items+8*i+4,struct.pack('<i',v))
stub(u,0x8124b44,model_set)
w32(u,0x3000d9fc,FAKEAPP);w32(u,FAKEAPP+0x2b4,T)
ticks=[];stub(u,D['cue_ui_tick'],lambda u:ticks.append(1))
for i,v in enumerate((30,0,-25,-100,-40,700)):call(u,0x8120264,APP+0xc08+2*0x30,0x1ba+i,v&0xffffffff)
u.mem_write(UI,bytes(16));u.mem_write(VIEWOBJ+0x739a,b'\x15')
# The page switch is asynchronous: the app still shows the EQ page when the
# press is handled; the TRACK page is current only on a later UI tick.
w32(u,FAKEAPP+0x2b4,0x24060000)
call(u,D['dj_eq_from_full'],VIEWOBJ,0x14,0,0)
check(shown[-1]==5,'EQ in full screen opens the TRACK view in DJ mode')
check(h16(T+KNOBS[0]+0x94)==0x4 and u.mem_read(ACTIVE,1)[0]==1,'before the page switch nothing is rebound, DJ mode stays pending')
w32(u,FAKEAPP+0x2b4,T);call(u,D['dj_ui_tick'],0x24072000,0x24072100)
# TRACK views light their LED through dj_led (view 5/6, also A's block switch).
leds.clear();call(u,D['dj_led'],0x30001f90,1)
check(leds==[3],'in the DJ view the TRACK views (and A) light EQ, never TRACK')
names=[text(T+k+0xf8) for k in KNOBS[:6]];bound=[h16(T+k+0x94) for k in KNOBS[:6]]
check(bound==[0x1ba+i for i in range(6)] and names==['HIGH','HI MID','LO MID','LOW','FILTER','RES'],
      'block 1 High / Hi Mid / Lo Mid / Low, block 2 Filter / Res, named')
vals=[struct.unpack('<i',u.mem_read(T+k+0x98,4))[0] for k in KNOBS[:6]]
check(vals==[30,0,-25,-100,-40,700] and all(h16(T+k+0x32)==2 for k in KNOBS[:6]),'knobs show the selected track\'s stored values')
check(all(u.mem_read(T+k+0x30,1)[0]==1 for k in KNOBS[6:]) and all(u.mem_read(T+o+0x30,1)[0]==1 for o in HIDE),
      'unused knobs and the level meter with its frame are hidden')
def label2(o):return text(T+o+0x65)+('/'+text(T+o+0xd1) if text(T+o+0xd1) else '')   # line 2 is drawn when it has text
def btn(o):return (h16(T+o+0xfa),h16(T+o+0xf8),label2(o),u.mem_read(T+o+0x30,1)[0],u.mem_read(T+o+0x96,1)[0])
def lit_color(o):return u.mem_read(T+o+0x8d,1)[0]
def knob_color(k):return u.mem_read(T+KNOBS[k]+0xa0+0x53,1)[0]
check([btn(o)[:4] for o in BUTTONS]==[(0x1f8,2,'Kill/High',0),(0x1f9,2,'Kill/Hi Mid',0),(0x1fa,2,'Kill/Lo Mid',0),(0x1fb,2,'Kill/Low',0),(0x1fc,2,'Filter/Reset',0)],
      'right block: kill buttons in block 1\'s knob order; the CUE button is the filter reset')
check(struct.unpack('<i',u.mem_read(T+0xe814+4,4))[0]==128 and struct.unpack('<i',u.mem_read(T+0xe814+0x38,4))[0]==152,
      'the filter reset sits above the Filter knob (moved with its label)')
check([btn(o)[4] for o in BUTTONS]==[0,0,0,1,1],'lit: Low is at kill, the filter is not Off')
check([lit_color(o) for o in BUTTONS]==[0x15]*4+[5],'kills light red (the red of REC), Filter Reset in the native lit colour')
check([knob_color(k) for k in range(4)]==[3,3,3,0x15],'the killed Low knob label has the lit colour, the others the native one')
# Presses: the native button posts 0x6e with its id; kill toggles between kill and the value before.
def press(o):
 w32(u,TMP,0x6e);w32(u,TMP+8,2);w32(u,TMP+12,h16(T+o+0xfa));w32(u,TMP+16,1);return call(u,D['dj_track_local'],T,TMP)
STORE[0]=1;n=len(sets);r=press(0xe9e8)
check(r==0 and sets[n:]==[(2,0x1ba,-100)] and btn(0xe9e8)[4]==1 and knob_color(0)==0x15 and struct.unpack('<i',u.mem_read(T+KNOBS[0]+0x98,4))[0]==-100,
      'Kill High on High +1.8 dB kills it: lit, knob at kill in the same colour')
press(0xe9e8);check(sets[-1]==(2,0x1ba,30) and btn(0xe9e8)[4]==0 and knob_color(0)==3,'pressed again it returns to +1.8 dB (30), unlit')
press(0xe640);check(sets[-1]==(2,0x1bd,0),'Kill Low on a Low killed by its knob returns to 0 dB')
press(0xe640);check(sets[-1]==(2,0x1bd,-100),'and kills again')
press(0xe814)
check(sets[-1]==(2,0x1be,0) and btn(0xe814)[4]==0 and (2,0x1bf,0) not in sets[-2:],'Filter Reset puts the filter to Off, Res stays')
w32(u,TMP+12,0x1bd);w32(u,TMP+16,(-50)&0xffffffff);call(u,D['dj_track_local'],T,TMP)
# Centre detent (only while an encoder turns: the turning flag of the aux record).
call(u,D['dj_ui_tick'],0x24072000,0x24072100)
def turn_to(k,v):
 n=len(sets);w32(u,TMP,0x6e);w32(u,TMP+8,2);w32(u,TMP+12,0x1ba+k);w32(u,TMP+16,v&0xffffffff);call(u,D['dj_track_local'],T,TMP)
 return sets[-1][2] if len(sets)>n else None
turn_to(0,5);u.mem_write(AUX+10,bytes([1]))
seq=[turn_to(0,v) for v in (3,1,-2)]
check(seq==[3,1,0],'turning High down across 0 stops at 0 dB')
seq=[turn_to(0,-1) for i in range(4)]
check(seq==[0,0,0,-1],'leaving 0 takes 3 more steps')
check(turn_to(0,1)==0 and turn_to(4,-5)==0,'from -1 up across 0 stops at 0; the filter at Off rests too')
u.mem_write(AUX+10,bytes([0]));check(turn_to(0,4)==4,'touch drags (not an encoder) have no detent')
turn_to(0,30);turn_to(4,-40)
u.mem_write(T+KNOBS[0]+0x98,struct.pack('<i',30));u.mem_write(T+KNOBS[4]+0x98,struct.pack('<i',-40))   # knobs as an encoder left them
STORE[0]=0
check(all(u.mem_read(T+k+0x30,1)[0]==0 for k in KNOBS[:6]),'the CUE slot knob is shown as Low')
# MIDI learn: in learn mode the app marks the knob whose (track, id) is the
# picked target (blue frame); the DJ knobs ask for their own ids.
u.mem_write(APP+0x73b3,bytes([1]));u.mem_write(APP+0x73bc,struct.pack('<H',2));u.mem_write(APP+0x73ba,struct.pack('<H',0x1bd))
call(u,D['dj_ui_tick'],0x24072000,0x24072100)
frames=[u32(u,T+k+0x2f4+0xc0) for k in KNOBS[:6]]
check(frames==[0x1a,0x1a,0x1a,0x1b,0x1a,0x1a],'MIDI learn: picking Low frames the Low knob only %s'%frames)
u.mem_write(APP+0x73b3,bytes([0]));call(u,D['dj_ui_tick'],0x24072000,0x24072100)

# Edits commit through the native model setter; other edits keep their path.
u.mem_write(TMP,struct.pack('<HHHHIII',0x6e,0,2,0,0x1bd,(-12000)&0xffffffff,0))
w32(u,TMP,0x6e);w32(u,TMP+8,2);w32(u,TMP+12,0x1bd);w32(u,TMP+16,(-50)&0xffffffff)
w32(u,0x3001eb3c,1000);r=call(u,D['dj_track_local'],T,TMP)
check(r==0 and sets[-1]==(2,0x1bd,-50),'turning Low commits track 3 Low = -50 through the model setter')
check(text(T+KNOBS[3]+0xa0+0x31)=='-13.0dB','turning shows the value in the knob label (-50 = -13.0 dB)')
def show_value(k,v):
 w32(u,TMP+12,0x1ba+k);w32(u,TMP+16,v&0xffffffff);call(u,D['dj_track_local'],T,TMP);return text(T+KNOBS[k]+0xa0+0x31)
check([show_value(0,100),show_value(0,0),show_value(3,-100),show_value(4,-40),show_value(4,0),show_value(4,25),show_value(5,700)]==['+6.0dB','0dB','KILL','LP 40','OFF','HP 25','70%'],
      'value texts: +6.0dB, 0dB, KILL (ISO), LP 40, OFF, HP 25, 70%')
w32(u,0x3001eb3c,1000+1300);call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check(text(T+KNOBS[5]+0xa0+0x31)=='RES','the name comes back 1.2 s after the last turn')
# A touch on a band knob selects block 1 (presented as VOL), others pass as they are.
seen=[];hk=stub(u,M['symbols']['dj4']['gate_track_local'],lambda u:(seen.append(h16(u.reg_read(UC_ARM_REG_R1)+12)),u.reg_write(UC_ARM_REG_R0,0)))
w32(u,TMP,0x16a);w32(u,TMP+12,0x1bc);call(u,D['dj_track_local'],T,TMP)
w32(u,TMP+12,0x1be);call(u,D['dj_track_local'],T,TMP)
u.hook_del(hk)
check(seen==[0x4,0x1be],'touching a band knob selects block 1, Filter/Res stay block 2')
# Encoders: block 1's fourth encoder lands on Low even with CUE Style Toggle.
turned=[];hk=stub(u,M['symbols']['dj4']['gate_turn'],lambda u:(turned.append(u.reg_read(UC_ARM_REG_R0)-T),u.reg_write(UC_ARM_REG_R0,0)))
for idx,w in ((3,0xc768),(2,0xc768),(0,0xc398)):
 u.reg_write(UC_ARM_REG_R4,idx);u.reg_write(UC_ARM_REG_R0,T+w);u.reg_write(UC_ARM_REG_R1,1)
 u.reg_write(UC_ARM_REG_SP,0x2407f000);u.reg_write(UC_ARM_REG_LR,0x080dfff1);u.emu_start(D['dj_turn_shim']|1,0x080dfff0,count=100000)
u.hook_del(hk)
check(turned==[0xd6a8,0xc768,0xc398],'encoder 4 of block 1 turns Low; encoders 3 and 1 keep Hi Mid and High')
check(text(T+0xc398+0xa0+0x31)=='+1.8dB' and text(T+0xc398+0xf8)=='+1.8dB','an encoder turn shows the value too (High 30 = +1.8 dB), also in the name copy the knob redraws from')
# Each edit makes the page rebind natively (0x66); the shown value must survive it.
for k,i in zip(KNOBS,(0x4,0x63,0x3,0x15d,0xda,0xdb,0x15e,0x1ae)):u.mem_write(T+k+0x94,struct.pack('<H',i))
call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check(h16(T+0xc398+0x94)==0x1ba and text(T+0xc398+0xa0+0x31)=='+1.8dB','a rebind while a value is on show keeps the value, no flashing name')
n=len(sets);w32(u,TMP+12,0x4);call(u,D['dj_track_local'],T,TMP)
check(len(sets)==n,'native parameters are not committed by the DJ path')

# Track change rebinds natively; DJ binding comes straight back.
for k,i in zip(KNOBS,(0x4,0x63,0x3,0x15d,0xda,0xdb,0x15e,0x1ae)):u.mem_write(T+k+0x94,struct.pack('<H',i))
call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check([h16(T+k+0x94) for k in KNOBS[:6]]==[0x1ba+i for i in range(6)] and ticks,'a native rebind in DJ mode is undone on the next UI tick, cue/gate ticks still run')

# EQ again: back to EQ; leaving the TRACK view also ends DJ mode.
u.mem_write(0x24058008,struct.pack('<H2xi',0x172,0));u.mem_write(APP+0xe78+10,struct.pack('<H',2))
call(u,D['dj_eq_other'],VIEWOBJ,0x15,0,0)
check(shown[-1]==0x14,'EQ in the DJ view goes back to EQ')
check([h16(T+k+0x94) for k in KNOBS]==[0x4,0x63,0x3,0x15d,0xda,0xdb,0x15e,0x1ae] and
      [text(T+k+0xf8) for k in KNOBS]==['VOL','PAN','GAIN','CUE','FX1','FX2','OUT2','GATE'],'native TRACK knobs and names restored')
check(all(u.mem_read(T+o+0x30,1)[0]==0 for o in HIDE+BUTTONS[:4]) and u.mem_read(T+0xe814+0x30,1)[0]==1,
      'buttons and meter shown again; CUE Style Fader keeps the CUE knob')
check([btn(o)[:3] for o in BUTTONS]==[(0x2b,2,'SIDE/CHAIN'),(0x160,2,'MUTE'),(0x162,2,'REC'),(0x161,2,'SOLO'),(0x173,2,'CUE')] and
      [lit_color(o) for o in BUTTONS]==[5,5,0x15,0x17,5] and all(knob_color(k)!=0x15 for k in range(8)) and
      struct.unpack('<i',u.mem_read(T+0xe814+4,4))[0]==80 and [u.mem_read(T+o+0xfc,1)[0] for o in BUTTONS]==[1,0,0,0,0],
      'native buttons restored: ids, labels, place of CUE')
call(u,D['dj_eq_other'],VIEWOBJ,0x15,0,0)
check(shown[-1]==0x15,'EQ elsewhere keeps the native choice')
u.mem_write(VIEWOBJ+0x739a,bytes([0x15]));call(u,D['dj_eq_from_full'],VIEWOBJ,0x14,0,0)
u.mem_write(VIEWOBJ+0x739a,bytes([0]));call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check(u.mem_read(ACTIVE,1)[0]==0 and h16(T+KNOBS[0]+0x94)==0x4,'switching to another page ends DJ mode and restores TRACK')
# The Gate knob on the plain TRACK page gets its learn frame too.
gate_ch=h16(T+0xde48+0x32);u.mem_write(APP+0x73b3,bytes([1]));u.mem_write(APP+0x73bc,struct.pack('<H',gate_ch));u.mem_write(APP+0x73ba,struct.pack('<H',0x1ae))
call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check(h16(T+0xde48+0x94)==0x1ae and u32(u,T+0xde48+0x2f4+0xc0)==0x1b,'MIDI learn: picking Gate frames the Gate knob on TRACK')
u.mem_write(APP+0x73b3,bytes([0]));call(u,D['dj_ui_tick'],0x24072000,0x24072100)
call(u,D['dj_eq_other'],VIEWOBJ,0x15,0,0)
check(shown[-1]==5 and u.mem_read(ACTIVE,1)[0]==1,'EQ from another page reopens the DJ view (EQ remembers its page)')
call(u,D['dj_eq_other'],VIEWOBJ,0x15,0,0)
check(shown[-1]==0x14 and u.mem_read(ACTIVE+1,1)[0]==0,'EQ in the DJ view goes to EQ and forgets DJ')
u.mem_write(VIEWOBJ+0x739a,bytes([0]));call(u,D['dj_eq_other'],VIEWOBJ,0x15,0,0)
check(shown[-1]==0x15,'after that, EQ from elsewhere keeps the native choice')
leds.clear();call(u,D['dj_led'],0x30001f90,1);check(leds==[1],'outside the DJ view the TRACK LED is untouched')
u.mem_write(VIEWOBJ+0x739a,bytes([0x15]));call(u,D['dj_eq_from_full'],VIEWOBJ,0x14,0,0)
call(u,D['dj_track_press'],VIEWOBJ,0x16,0,0)
check(shown[-1]==5 and leds[-1]==1 and u.mem_read(ACTIVE,1)[0]==0,'TRACK in the DJ view opens the plain TRACK page (not Side Chain), TRACK LED')
call(u,D['dj_track_press'],VIEWOBJ,0x16,0,0)
check(shown[-1]==0x16,'TRACK on the TRACK page still opens Side Chain')
# B: the DJ settings are the native MAIN settings view (9; encoders and edits
# are routed by the current view) showing only the DJ rows; B there back.
u.mem_write(VIEWOBJ+0x739a,bytes([0x15]));call(u,D['dj_eq_from_full'],VIEWOBJ,0x14,0,0)
n=len(leds);call(u,D['dj_b_press'],VIEWOBJ,7,0,0)
check(shown[-1]==9 and u.mem_read(ACTIVE,1)[0]==1 and u.mem_read(ACTIVE+2,1)[0]==1,'B in the DJ view opens MAIN settings (view 9) as the DJ settings, DJ mode stays')
w32(u,FAKEAPP+0x2b4,0x24060000);call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check(u.mem_read(ACTIVE,1)[0]==1 and u.mem_read(ACTIVE+2,1)[0]==1,'DJ mode survives view 9 while its settings are open')
LIST=0x24078000;u.mem_write(LIST,bytes(0x100))
rows=[];rh=stub(u,0x8138546,lambda u:rows.append((u.reg_read(UC_ARM_REG_R1),u.reg_read(UC_ARM_REG_R2))))
CELL=0x24079000;u.mem_write(CELL,struct.pack('<H',0x0c))
check(call(u,D['dj_page_rows'],APP,CELL,LIST)==1 and [r[0] for r in rows]==[0x1c4,0x1c5,0x1c0,0x1c1,0x1c2,0x1c3,0x1c7,0x1c8],'its rows: Bands, Preset, Curve, Xover Lo/Mid/Hi, Filter Slope, Res Max')
call(u,D['dj_knob'],APP,CELL,0x1c2,1500,0)
check((0x0c,0x1c2,1500) in sets[-2:] and sets[-1]==(0x0c,0x1c5,0),'DJ settings edits take the native Main path; a crossover edit sets the preset to Custom')
call(u,D['dj_b_main_settings'],VIEWOBJ,8,0,0)
check(shown[-1]==5 and u.mem_read(ACTIVE,1)[0]==1 and u.mem_read(ACTIVE+2,1)[0]==0,'B there returns to the DJ view')
rows.clear();call(u,D['dj_page_rows'],APP,CELL,LIST);u.hook_del(rh)
check([r[0] for r in rows]!=[0x1c4,0x1c5,0x1c0,0x1c1,0x1c2,0x1c3,0x1c7,0x1c8],'MAIN\'s own B page lists all Main rows again')
u.mem_write(VIEWOBJ+0x739a,bytes([5]));call(u,D['dj_eq_other'],VIEWOBJ,0x15,0,0)
call(u,D['dj_b_main_settings'],VIEWOBJ,8,0,0);check(shown[-1]==8,'B on MAIN\'s own settings still shows MAIN')
u.mem_write(VIEWOBJ+0x739a,bytes([0x15]));call(u,D['dj_eq_from_full'],VIEWOBJ,0x14,0,0);call(u,D['dj_b_press'],VIEWOBJ,7,0,0)
u.mem_write(VIEWOBJ+0x739a,bytes([9]));call(u,D['dj_track_other'],VIEWOBJ,5,0,0)
check(shown[-1]==5 and u.mem_read(ACTIVE,1)[0]==0,'TRACK from the DJ settings opens the plain TRACK page')
u.mem_write(VIEWOBJ+0x739a,bytes([0x15]));call(u,D['dj_eq_from_full'],VIEWOBJ,0x14,0,0);call(u,D['dj_b_press'],VIEWOBJ,7,0,0)
u.mem_write(VIEWOBJ+0x739a,bytes([9]));call(u,D['dj_eq_other'],VIEWOBJ,0x15,0,0)
check(shown[-1]==0x14 and u.mem_read(ACTIVE,1)[0]==0,'EQ from the DJ settings goes to EQ')
u.mem_write(VIEWOBJ+0x739a,bytes([5]));call(u,D['dj_b_press'],VIEWOBJ,7,0,0);check(shown[-1]==7,'B on the TRACK page still opens the inputs')
print('DJ UI checks:',H.checks)
