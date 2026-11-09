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
print('Cue icon checks:',H.checks)
