"""MIXER native layout preservation and border-safe abbreviated TRACK Gate."""
from gate_ui_fixture import *
from PIL import Image
D=0x38800680;FB=0xc0100000;UIROOT=0x24068000;PAGES=0x24069000
ch=2;setup(1,ch,-60000);w=T+0xde48
w32(u,0x3000d9fc,UIROOT)
def report(gain,threshold=-60000,seq=2):
 u.mem_write(D+64*ch,struct.pack('<IIi4fI',0x47444131,seq,threshold,.001,.0005,.001,gain,1))
def control(fmt,bpp):
 h=u.hook_add(UC_HOOK_CODE,lambda q,a,n,d:w32(q,q.reg_read(UC_ARM_REG_SP),w),begin=S['gate_ux_control'],end=S['gate_ux_control'])
 call(u,S['gate_ux_control'],FB,320,fmt,bpp);u.hook_del(h)
# Native layout setting 0x114 chooses single versus paired rows. Use the
# native renderer's real layout traversal, not fabricated widget geometry.
for single in (0,1):
 for count in (6,12):
  call(u,0x8120264,APP+0xe78,0x113,count);call(u,0x8120264,APP+0xe78,0x114,single)
  u.mem_write(TMP,struct.pack('<4i',0,0,320,220));call(u,u32(u,u32(u,P)+0x20),P,TMP)
  call(u,0x812c914,P);w32(u,UIROOT+0x2b4,P)
  native=bytes(u.mem_read(P,0xd280));frame=bytes((i*13+7)%256 for i in range(320*240*4))
  u.mem_write(FB,frame);call(u,S['gate_ux_draw'],FB,320,1,4)
  check(bytes(u.mem_read(FB,len(frame)))==frame,'no Gate overlay changes native channel headers/selection/thresholds '+str((single,count)))
  check(bytes(u.mem_read(P,0xd280))==native,'native MIXER widget state is untouched '+str((single,count)))
  u.mem_write(PAGES,bytes(64));clock[0]+=1000;w32(u,0x3001eb3c,clock[0]);call(u,S['gate_ux_tick'],PAGES)
  check(bytes(u.mem_read(PAGES+0x38,2))==b'\0\0','MIXER keeps native redraw scheduling '+str((single,count)))
# Capture the strings actually sent into the compiled ARM text renderer.
texts=[]
def text_hook(q,a,n,d):
 ptr=u32(q,q.reg_read(UC_ARM_REG_SP)+12)
 texts.append(bytes(q.mem_read(ptr,54)).split(b'\0')[0].decode())
h=u.hook_add(UC_HOOK_CODE,text_hook,begin=S['gx_center'],end=S['gx_center'])
u.mem_write(w+4,struct.pack('<4i',170,1,48,44));w32(u,UIROOT+0x2b4,T)
images=[]
for i,(gain,expected) in enumerate([(1.,'OPN'),(.4,'OPN'),(0.,'CLS')]):
 report(gain,seq=4+2*i);texts.clear();u.mem_write(FB,bytes(320*240*4));control(1,4)
 check(texts==['GATE','-60.0',expected],'TRACK renders only GATE, threshold and abbreviated state '+expected+str(gain))
 images.append(bytes(u.mem_read(FB,320*240*4)))
check(images[0]==images[1],'partial release displays OPN identically; no FADE state or amber light')
report(1.,-96000,12);call(u,0x8127b84,w,0x1ae,(-96000)&0xffffffff,1)
texts.clear();control(1,4);check(texts==['GATE','OFF','OFF'],'Off stays explicit and distinct from a closed active gate')
u.hook_del(h)
# Seed native selection/background borders with nonuniform sentinel pixels.
call(u,0x8127b84,w,0x1ae,(-60000)&0xffffffff,1);report(0.,seq=14)
x,bottom,width,height=170,1,48,44;top=240-bottom-height
for fmt,bpp in ((1,4),(2,3),(3,2),(4,2)):
 frame=bytes((i*29+11)%256 for i in range(320*240*bpp));u.mem_write(FB,frame);control(fmt,bpp)
 after=bytes(u.mem_read(FB,len(frame)));changed=False;safe=True
 for yy in range(240):
  for xx in range(320):
   i=(yy*320+xx)*bpp
   if after[i:i+bpp]!=frame[i:i+bpp]:
    changed=True;safe &= x+2<=xx<x+width-2 and top+2<=yy<top+height-2
 check(changed and safe,'native bottom and selection perimeter preserved byte-for-byte, format '+str(fmt))
# Internal preview with native border pixels visible around each control.
u.mem_write(FB,bytes(320*240*4))
for col,(gain,title) in enumerate([(1.,'OPN'),(.4,'OPN'),(0.,'CLS')]):
 x=30+col*96;bottom=100;top=96
 u.mem_write(w+4,struct.pack('<4i',x,bottom,48,44));report(gain,seq=20+2*col)
 for yy in range(top,top+44):
  for xx in range(x,x+48):
   if yy>=top+42 or xx<x+2 or xx>=x+46 or yy<top+2:
    w32(u,FB+(yy*320+xx)*4,0xff9455fa if col==1 else 0xffcccccc)
 control(1,4)
Image.frombytes('RGBA',(320,240),bytes(u.mem_read(FB,320*240*4)),'raw','BGRA').convert('RGB').resize((960,720),Image.Resampling.NEAREST).save(ROOT/'tests/gate-alpha4-preview.png')
print(str(H.checks)+' alpha.4 native MIXER row/identity/selection preservation and TRACK label/border checks passed; hardware retest required.')
