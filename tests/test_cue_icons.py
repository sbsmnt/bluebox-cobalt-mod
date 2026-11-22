"""Track header cue state: the real MIXER and TRACK pages are built by their
native constructors (gate_ui_fixture); only the back-buffer getter and the
rest of the present chain are stubbed. Cue icons and input dimming must land
in each tile's own rectangle in the theme colours, follow the published masks
and trigger one redraw per change."""
from gate_ui_fixture import *
import firmware_harness as H
C4=M['symbols']['cue4'];C7=M['symbols']['cue7']
STATE=0x38800660;FAKEAPP=0x24070000;DESC=0x24071000;FB=0xc1000000
TILE0=0x14d0;STRIP0=0xbe0;STRIDE=0xd98;VT=0x814af64
ICON=[0x07c,0x082,0x101,0x101,0x183,0x183,0x183]
FILL=u32(u,0x814d230+5*4)&0xffffff;BG=u32(u,0x814d230+2*4)&0xffffff
PAINT=0x123456   # what the native UI "drew" before the overlay
def bit(r,c):return 0<=r<7 and 0<=c<9 and bool(ICON[r]>>(8-c)&1)

w32(u,DESC,FB);u.mem_write(DESC+4,struct.pack('<HHBxH',320,240,1,4))
stub(u,0x810139a,lambda u:u.reg_write(UC_ARM_REG_R0,DESC))
presented=[];stub(u,C4['pc_present'],lambda u:presented.append(1))
ticks=[];stub(u,C4['gate_ui_tick'],lambda u:ticks.append(1))
w32(u,0x3000d9fc,FAKEAPP)
def show(page):w32(u,FAKEAPP+0x2b4,page)
def masks(cued,inputs=0xfff,magic=0x43554534):u.mem_write(STATE,struct.pack('<IiIIIII',magic,0,0,0,cued,inputs,1))
def present():
 u.mem_write(FB,struct.pack('<I',0xff000000|PAINT)*(320*240));call(u,C4['cue_present'],0,1,0,0)
 return bytes(u.mem_read(FB,320*240*4))
def px(fb,x,y):return struct.unpack_from('<I',fb,(y*320+x)*4)[0]&0xffffff
def changed(fb):return sum(1 for i in range(0,len(fb),4) if struct.unpack_from('<I',fb,i)[0]&0xffffff!=PAINT)
def tiles(page):
 out={}
 for ch in range(13):
  t=page+TILE0+ch*STRIDE
  if u32(u,t)!=VT:continue
  x,b,w,h=struct.unpack('<4i',u.mem_read(t+4,16))
  if w>0:out[ch]=(x,240-b-h,w,h)
 return out
def icon_ok(fb,x0,y0,under=PAINT):
 for r in range(-1,8):
  for c in range(-1,10):
   body=bit(r,c);edge=not body and any(bit(r+a,c+b) for a in (-1,0,1) for b in (-1,0,1))
   want=FILL if body else BG if edge else under
   if px(fb,x0+c,y0+r)!=want:return False
 return True
CELL=sum(1 for r in range(-1,8) for c in range(-1,10) if bit(r,c) or any(bit(r+a,c+b) for a in (-1,0,1) for b in (-1,0,1)))
def dimmed(x,y):return BG if (x+y)%2==0 else PAINT

# Patch wiring.
# The redraw check runs the DJ view tick first, which chains to the cue tick.
check(M['symbols']['dj4']['cue_ui_tick']==C4['cue_ui_tick'],'DJ view tick chains to the cue icon tick')
for at,new in ((0x8137496,C4['cue_present']),(0x813408e,M['symbols']['dj4']['dj_ui_tick'])):
 h1,h2=struct.unpack('<HH',u.mem_read(at,4));s=(h1>>10)&1
 off=(s<<24)|((1-(((h2>>13)&1)^s))<<23)|((1-(((h2>>11)&1)^s))<<22)|((h1&1023)<<12)|((h2&2047)<<1)
 check(at+4+(off-(1<<25) if s else off)==new,'call at '+hex(at)+' now reaches the cue hook')
check(FILL==0x09d7f5 and BG==0x141414,'icon colours come from the theme palette (active fill, background)')

for name,page in (('MIXER',P),('TRACK',T)):
 tl=tiles(page);show(page)
 # The page layout shows the strips of the 8 tracks and the master.
 for ch in range(13):u.mem_write(page+STRIP0+ch*STRIDE+0x30,bytes([0 if ch<8 or ch==12 else 1]))
 check(sorted(c for c in tl if c<12)==list(range(8)) and 12 in tl,name+': eight visible track tiles (8-track mode) and the master at page+0x14d0')
 masks(0);fb=present()
 check(changed(fb)==0 and presented,name+': no cue and all inputs set: nothing drawn, present chain continues')
 masks((1<<0)|(1<<5));fb=present()
 ok=all(icon_ok(fb,tl[c][0]+tl[c][2]-11,tl[c][1]+2) for c in (0,5))
 check(ok and changed(fb)==2*CELL,name+': outlined icons in the top-right corner of tracks 1 and 6 only')
 for c in (0,5):
  x,y,w,h=tl[c]
  check(x<=x+w-12 and y+1>=y and y+10<=y+h,name+': track '+str(c+1)+' icon cell inside its tile')
 u.mem_write(page+TILE0+5*STRIDE+0x30,b'\1');fb=present()
 check(changed(fb)==CELL,name+': hidden tiles get nothing');u.mem_write(page+TILE0+5*STRIDE+0x30,b'\0')
 u.mem_write(page+STRIP0+5*STRIDE+0x30,b'\1');fb=present()
 check(changed(fb)==CELL,name+': tiles in a hidden strip get nothing');u.mem_write(page+STRIP0+5*STRIDE+0x30,b'\0')
 masks(1<<9,0xfff);fb=present()
 check(changed(fb)==0,name+': cue on a track without a tile draws nothing')
 masks(0,0xfff&~(1<<2));fb=present();x,y,w,h=tl[2]
 inside=all(px(fb,x+c,y+r)==dimmed(x+c,y+r) for r in range(h) for c in range(w))
 check(inside and changed(fb)==sum(1 for r in range(h) for c in range(w) if (x+c+y+r)%2==0),name+': a track without an input is greyed out with a stipple, only its tile')
 call(u,C4['cue_present'],0,1,0,0);again=bytes(u.mem_read(FB,320*240*4))
 check(again==fb,name+': greying is stable when the UI does not repaint between frames')
 masks(1<<2,0xfff&~(1<<2));fb=present()
 body=all(px(fb,x+w-11+c,y+2+r)==(FILL if bit(r,c) else BG if any(bit(r+a,c+b) for a in (-1,0,1) for b in (-1,0,1)) else dimmed(x+w-11+c,y+2+r)) for r in range(-1,8) for c in range(-1,10))
 check(body,name+': a cued track without an input is greyed and still shows its icon')

# S21: Tools "Dim No Input" Off leaves tracks without an input undimmed;
# cue icons still draw.
SETS=APP+0x737c;PROJ=APP+0xe78;CLKSET=APP+0xe68   # OUT2 Mode in cell 12's set; PROJ B (Clk Out) in the kind-8 project set (S23: S21/S22 read +0xe78)
def get(s,i):return call(u,0x8120234,s,i)
# The fixture's sets: settings get Dim No Input, the project the two Clk Out rows.
w32(u,SETS+4,0x24059000);u.mem_write(SETS+10,struct.pack('<HH',1,8));u.mem_write(0x24059000,struct.pack('<H2xi',0x290,0))
u.mem_write(TMP+0x700,b'\x08');call(u,0x81202b8,CLKSET,TMP+0x700)   # the real kind-8 project defaults
check(get(CLKSET,0x110)==0 and get(CLKSET,0x111)==0 and get(CLKSET,0xce)==0,'Clk Out L/R live in the kind-8 project set (with Metronome), default Off')
def put(s,i,v):call(u,0x8120264,s,i,v&0xffffffff)
check(get(SETS,0x290)==0,'Dim No Input defaults On (0)')
show(T);tl=tiles(T);masks(0,0xfff&~(1<<2))
on=changed(present());put(SETS,0x290,1);off=changed(present())
masks(1<<2,0xfff&~(1<<2));icon=present()
check(on>0 and off==0 and icon_ok(icon,tl[2][0]+tl[2][2]-11,tl[2][1]+2) and changed(icon)==CELL,'Dim No Input Off: no stipple on tracks without an input, icons stay')
put(SETS,0x290,0)

# S21: a Clk Out row on: MIXER sends tiles showing that Out 2 send look muted
# grey (S22: knob colour, active fill and text -> idle fill); nothing else.
TEXT=u32(u,0x814d230+3*4)&0xffffff;IDLE=u32(u,0x814d230+1*4)&0xffffff
KNOBC=M['symbols']['4']['theme_knob_palette']+34*4
check(u32(u,KNOBC)&0xffffff==0x09d7f5,'knob theme colour read from the cm4 CLUT entry 34')
u.mem_write(KNOBC,struct.pack('<I',0xff31a2c4));KNOB=0x31a2c4   # a theme whose knobs differ from the active fill (S21 missed these)
def painted(page):
 u.mem_write(FB,struct.pack('<I',0xff000000|PAINT)*(320*240))
 for ch,(x,y,w,h) in tiles(page).items():
  for r in range(h):u.mem_write(FB+((y+r)*320+x)*4,b''.join(struct.pack('<I',0xff000000|(FILL,TEXT,KNOB,PAINT)[c%4]) for c in range(w)))
 return bytes(u.mem_read(FB,320*240*4))
def mixer_frame(mono,send,l,r):
 put(PROJ,0x18c,0 if mono else 1);put(CLKSET,0x110,l);put(CLKSET,0x111,r)
 call(u,S['gate_mix_setup'],P,1);call(u,S['gate_mix_select'],P,send)
 show(P);masks(0);before=painted(P);call(u,C4['cue_present'],0,1,0,0)
 return before,bytes(u.mem_read(FB,320*240*4))
def muted_tracks(before,after,page):
 out=[]
 for ch,(x,y,w,h) in tiles(page).items():
  if ch>=12:continue
  cells=[(px(before,x+c,y+r),px(after,x+c,y+r)) for r in range(h) for c in range(w)]
  if all(b==a for b,a in cells):continue
  if all(a==(IDLE if b in (FILL,TEXT,KNOB) else b) for b,a in cells):out.append(ch)
  else:out.append(-1)
 return out
for ch in range(13):u.mem_write(P+STRIP0+ch*STRIDE+0x30,bytes([0 if ch<8 or ch==12 else 1]))
check(muted_tracks(*mixer_frame(1,4,0,0),P)==[],'both clocks Off: OUT2 tiles unchanged')
check(muted_tracks(*mixer_frame(1,4,3,0),P)==list(range(8)),'Mono, Clk Out L on: every OUT2 tile looks muted')
check(muted_tracks(*mixer_frame(1,5,3,0),P)==[],'Mono, Clk Out L on: OUT3 tiles (Out 2 right) unchanged')
check(muted_tracks(*mixer_frame(1,5,0,1),P)==list(range(8)) and muted_tracks(*mixer_frame(1,4,0,1),P)==[],'Mono, Clk Out R on: OUT3 tiles muted, OUT2 tiles not')
check(muted_tracks(*mixer_frame(0,4,0,1),P)==list(range(8)),'Stereo, either clock on: the OUT2 tiles look muted')
check(muted_tracks(*mixer_frame(1,3,3,3),P)==[] and muted_tracks(*mixer_frame(1,0,3,3),P)==[],'other sends (CUE, Pan) unchanged with clocks on')
before,after=mixer_frame(1,4,3,0);call(u,C4['cue_present'],0,1,0,0)
check(bytes(u.mem_read(FB,320*240*4))==after,'muted look is stable when the UI does not repaint')
check(get(PROJ,0x110)==0 and get(PROJ,0x111)==0,'the clock rows are not in the OUT2 Mode set (S21/S22 read them there)')
# S23: TRACK page: the OUT2 knob (seventh) greys like the MIXER tiles; the others stay.
def knob_rect(off):
 x,b,w,h=struct.unpack('<4i',u.mem_read(T+off+4,16));return x,240-b-h,w,h
def track_frame(l,r):
 put(CLKSET,0x110,l);put(CLKSET,0x111,r);show(T);masks(0)
 u.mem_write(FB,struct.pack('<I',0xff000000|PAINT)*(320*240))
 for off in (0xda78,0xd2d8):
  x,y,w,h=knob_rect(off)
  for rr in range(h):u.mem_write(FB+((y+rr)*320+x)*4,b''.join(struct.pack('<I',0xff000000|(FILL,TEXT,KNOB,PAINT)[c%4]) for c in range(w)))
 before=bytes(u.mem_read(FB,320*240*4));call(u,C4['cue_present'],0,1,0,0);after=bytes(u.mem_read(FB,320*240*4))
 def grey(off):
  x,y,w,h=knob_rect(off);cells=[(px(before,x+c,y+rr),px(after,x+c,y+rr)) for rr in range(h) for c in range(w)]
  return all(a==(IDLE if b in (FILL,TEXT,KNOB) else b) for b,a in cells) and any(a!=b for b,a in cells)
 return grey(0xda78),grey(0xd2d8),sum(1 for i in range(0,len(before),4) if before[i:i+3]!=after[i:i+3])
u.mem_write(TMP,struct.pack('<6I',0x63,0x8148664,3,0,0,0));call(u,0x8133194,T,TMP)   # TRACK shows track 4
check(struct.unpack('<H',u.mem_read(T+0xda78+0x94,2))[0]==0x15e,'TRACK seventh knob is OUT2')
g_out2,g_other,n=track_frame(3,0);x,y,w,h=knob_rect(0xda78)
check(g_out2 and not g_other and n==sum(1 for rr in range(h) for c in range(w) if c%4<3),'TRACK, Clk Out L on: only the OUT2 knob greys')
check(track_frame(0,0)[2]==0,'TRACK, clocks Off: nothing changes')
put(CLKSET,0x110,0);put(CLKSET,0x111,0);put(PROJ,0x18c,0)

# EQ page with its real layout: normal view shows the strip, the full-screen
# graph (second EQ press) hides every track strip, so no icon or greying.
EQP=0xc0400000;call(u,0x8129d84,EQP)
u.mem_write(0x24058008,struct.pack('<H2xi',0x113,8));u.mem_write(APP+0xe78+10,struct.pack('<H',2))
show(EQP);tl=tiles(EQP)
for full,want in ((0,True),(1,False),(0,True)):
 u.mem_write(EQP+0xbcbc,bytes([full]));call(u,0x812a600,EQP)
 masks(1<<3,0xfff&~(1<<6));fb=present()
 drawn=icon_ok(fb,tl[3][0]+tl[3][2]-11,tl[3][1]+2) and changed(fb)>CELL
 check(drawn==want and (want or changed(fb)==0),'EQ '+('full screen: no icons or greying over the graph' if full else 'normal view: icon and greying on the track strip'))

# A page without the track strip is left alone; an old or missing state too.
show(0x24060000);masks(1,0);check(changed(present())==0,'pages without track tiles get nothing')
show(T);masks(1,0,magic=0);check(changed(present())==0,'no published state, nothing drawn')
masks(1,0,magic=0x43554533);check(changed(present())==0,'state from the previous layout is ignored')

# Redraw exactly once per mask change, then defer to the Gate/GR tick.
PAGES=0x24072000
def tick():
 u.mem_write(PAGES,bytes(0x40));call(u,C4['cue_ui_tick'],PAGES,PAGES+0x100)
 return bytes(u.mem_read(PAGES+0x38,2))
masks(0);tick()
check(tick()==b'\0\0' and ticks,'unchanged masks: no forced redraw, Gate/GR tick still runs')
masks(1<<2);check(tick()==b'\1\1','cue on: both page buffers redraw')
check(tick()==b'\0\0','no repeated redraw while the masks are unchanged')
masks(1<<2,0xffe);check(tick()==b'\1\1','input change: redraw')
masks(0,0xffe);check(tick()==b'\1\1','cue off: redraw removes the icon')
tick()
put(CLKSET,0x110,2);check(tick()==b'\1\1','Clk Out L on: redraw');check(tick()==b'\0\0','then no repeat')
put(CLKSET,0x110,0);check(tick()==b'\1\1','Clk Out L off: redraw')
put(SETS,0x290,1);check(tick()==b'\1\1','Dim No Input changed: redraw');put(SETS,0x290,0);tick()
print('Cue icon checks:',H.checks)
