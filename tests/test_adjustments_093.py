"""Return-only tape level, density and scoped knob color integration."""
from delay_fixture import *
import os,importlib.util
prevpath=(ROOT/'tests'/'references'/'v0.9.5')/'patch_bluebox.py'
s=importlib.util.spec_from_file_location('before',prevpath);before=importlib.util.module_from_spec(s);s.loader.exec_module(before)
a,_,_=dsetup(before.transform(stock));b,_,_=dsetup();same=True;ratios=True
for n in range(1000):
 ev=INITIAL+[(0x1a0,2),(0x1a7,1000),(0x1ac,6000),(0x39,650),(0x3d,1000)] if n==0 else []
 oa,_=dblock(a,signal(.2,n,703),signal(.13,n,443),ev)
 ob,_=dblock(b,signal(.2,n,703),signal(.13,n,443),ev)
 if n==0:steady(a);steady(b)
 same &= a.mem_read(DOBJ,0x278)==b.mem_read(DOBJ,0x278)
 # Inherited return compensation is retained exactly against v0.9.4.
 if n>200:
  aa=struct.unpack('<64f',oa);bb=struct.unpack('<64f',ob)
  d0=f32(b,0x38800200+40);d1=f32(b,0x38800200+44)
  ratios &= oa==ob
check(same,'return-level adjustment leaves actual native Tape object state unchanged')
for i in range(6):
 line=DOBJ+0x30+i*28;size=u32(a,line+20);ptr=u32(a,line)
 check(a.mem_read(ptr,4*size)==b.mem_read(ptr,4*size),'native Tape feedback ring remains bit-identical to0.9.4, line'+str(i))
check(all(abs(x-y)<1e-6 for x,y in zip(struct.unpack('<64f',b.mem_read(DS+60,256)),struct.unpack('<64f',ob))),'To Rev captures the compensated Tape output once')
check(ratios,'WILD-off Tape PCM remains bit-identical to v0.9.4')
# Color is stored as an explicit read-only constant, not a replacement of white.
S=M['symbols']['4'];color=S['theme_knob_rgb'];base=machine();custom_image=p.transform(stock,theme={'knob':'#123456'});custom=machine(custom_image)
for u in [base,custom]:call(u,0x81004d4,0x814d230,0x30000000,0x300000ec)
check(u32(base,color)==0xff09d7f5 and u32(custom,color)==0xff123456,'knob color has its own guarded RGB constant')
check(call(custom,0x8101064,0,15)==0xffffffff and call(custom,0x8101064,0,6)==0xffff0000,'shared white and status-red palette entries stay stock')
# Compare the actual native knob renderer masks against stock white, using
# both sizes, five positions, normal/rotated ARGB descriptors and real draw calls.
W=0x24010000;CTXD=0x24020000;SURF=CTXD+0x100;FB=0xc0180000;MODEL=0x300173d0

def fixture(image):
 u=machine(image);heap=[0xc0010000]
 def alloc(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,alloc);call(u,0x8134cb8,0);call(u,0x81004d4,0x814d230,0x30000000,0x300000ec)
 stub(u,0x813a1f8,lambda u:None)
 return u
native=fixture(stock);themed=fixture(custom_image)
for compact in [0,1]:
 for value in [0,250,500,750,1000]:
  for rotation in [1,2,3]:
   results=[]
   for u in [native,themed]:
    u.mem_write(W,bytes(0x3d0));u.mem_write(W+0x32,struct.pack('<H',20));u.mem_write(W+0x3c9,bytes([compact]));call(u,0x8127b84,W,0x39,value,1)
    u.mem_write(W+4,struct.pack('<4i',100,80,48,44));u.mem_write(CTXD,b'\1\1\0\0'+struct.pack('<I',SURF));u.mem_write(SURF,struct.pack('<IHHBBH',FB,320,240,1,rotation,4));u.mem_write(FB,bytes(320*240*4))
    call(u,0x812808c,W,CTXD)
    pixels=struct.unpack('<76800I',u.mem_read(FB,320*240*4));results.append({i:v for i,v in enumerate(pixels) if v})
   check(bool(results[0]) and results[0].keys()==results[1].keys() and set(results[0].values())=={0xffffffff} and set(results[1].values())=={0xff123456},'native knob geometry preserved with independent color '+str((compact,value,rotation)))
# Indexed surfaces use the native primitive with the dedicated knob index34.
u=machine(custom_image);SURF=0x24050000;FB=0xc0180000
for fmt,bpp in [(3,2),(4,2),(5,1)]:
 u.mem_write(SURF,struct.pack('<IHHBBH',FB,320,240,fmt,1,bpp));u.mem_write(FB,bytes(320*240*bpp));call(u,S['theme_knob_pixel'],SURF,10,20,15);a=bytes(u.mem_read(FB,320*240*bpp));u.mem_write(FB,bytes(320*240*bpp));call(u,0x813723c,SURF,10,20,34)
 check(a==u.mem_read(FB,320*240*bpp),'indexed format preserves raster while selecting dedicated color '+str(fmt))

W=0x24010000;CTXD=0x24020000;SURF=CTXD+0x100;FB=0xc0180000
# Full indexed knob renderer: preserve mask while both bitmap and pointer
# use only entry34; the table resolves that entry to the selected RGB.
for fmt,bpp in [(3,2),(4,2),(5,1)]:
 for compact in [0,1]:
  for value in [0,500,1000]:
   results=[]
   for u in [native,themed]:
    u.mem_write(W,bytes(0x3d0));u.mem_write(W+0x32,struct.pack('<H',20));u.mem_write(W+0x3c9,bytes([compact]));call(u,0x8127b84,W,0x39,value,1)
    u.mem_write(W+4,struct.pack('<4i',100,80,48,44));u.mem_write(CTXD,b'\1\1\0\0'+struct.pack('<I',SURF));u.mem_write(SURF,struct.pack('<IHHBBH',FB,320,240,fmt,1,bpp));u.mem_write(FB,bytes(320*240*bpp))
    call(u,0x812808c,W,CTXD)
    raw=bytes(u.mem_read(FB,320*240*bpp));results.append({i:v for i,v in enumerate(raw) if v})
   check(bool(results[0]) and results[0].keys()==results[1].keys() and set(results[0].values())=={15} and set(results[1].values())=={34},'full indexed knob mask and dedicated color '+str((fmt,compact,value)))
# Real DMA2D setup must consume the extended table on both layers.
u=machine(custom_image);HANDLE=0x24040000;HW=0x24041000;PALETTE=M['symbols']['4']['theme_knob_palette'];w32(u,HANDLE,HW)
# The wrapper has five AAPCS arguments; create a tiny caller with layer on stack.
for layer in [0,1]:
 u.reg_write(UC_ARM_REG_R4,layer)
 # Call wrapper directly with a one-instruction hook supplying arg5 at entry.
 def fifth(u,a,n,d):w32(u,u.reg_read(UC_ARM_REG_SP),layer)
 h=u.hook_add(UC_HOOK_CODE,fifth,begin=S['theme_knob_clut'],end=S['theme_knob_clut'])
 call(u,S['theme_knob_clut'],HANDLE,0x30000000,0,33);u.hook_del(h)
 regptr=HW+(0x30 if layer==0 else 0x2c);regcfg=HW+(0x24 if layer==0 else 0x1c)
 check(u32(u,regptr)==PALETTE and (u32(u,regcfg)>>8)&255==34,'native DMA2D layer'+str(layer)+' loads35 entries from the dedicated flash CLUT')
# Preserve unrelated setup arguments and the native busy return.
w32(u,HANDLE+0x60,0);PALETTE_OTHER=0x24056000
h=u.hook_add(UC_HOOK_CODE,lambda u,a,n,d:w32(u,u.reg_read(UC_ARM_REG_SP),0),begin=S['theme_knob_clut'],end=S['theme_knob_clut'])
call(u,S['theme_knob_clut'],HANDLE,PALETTE_OTHER,0,3);u.hook_del(h)
check(u32(u,HW+0x30)==PALETTE_OTHER and (u32(u,HW+0x24)>>8)&255==3,'unrelated CLUT setup keeps native arguments')
u.mem_write(HANDLE+0x60,b'\1')
h=u.hook_add(UC_HOOK_CODE,lambda u,a,n,d:w32(u,u.reg_read(UC_ARM_REG_SP),0),begin=S['theme_knob_clut'],end=S['theme_knob_clut'])
result=call(u,S['theme_knob_clut'],HANDLE,0x30000000,0,33);u.hook_del(h)
check(result==2,'native busy CLUT status is preserved')
# Actual FX toggle constructor retains the active button role, independent of
# knob color. Observe the native final fill boundary, not a preview mockup.
image=p.transform(stock,theme={'knob':'#abcdef','button_active':'#2468ac','control_active':'#13579b'})
u=fixture(image);W=0x24010000;CTXD=0x24020000;SURF=CTXD+0x100
call(u,0x8126fc0,W);u.mem_write(W+4,struct.pack('<4i',20,40,48,44));u.mem_write(W+0x130,b'\1')
call(u,0x8138220,W,1)
u.mem_write(CTXD,b'\1\1\0\0'+struct.pack('<I',SURF));u.mem_write(SURF,struct.pack('<IHHBBH',0xc0180000,320,240,1,1,4))
seen=[];stub(u,0x8101408,lambda u:seen.append(u.reg_read(UC_ARM_REG_R1)))
call(u,0x8127104,W,CTXD)
check(bytes(u.mem_read(W+0x8d,1))==b'\5' and 0xff2468ac in seen,'real active FX toggle renders its configured active-button fill')
check(u32(u,S['theme_knob_rgb'])==0xffabcdef,'active button fill is independent of knob graphics color')
# Expanded FX toggle's child label uses native role27, distinct from fill5.
DATA=0x24058000;u.mem_write(DATA,b'\1\1\0\0ON\0')
call(u,0x812b678,W+0x100,DATA)
check(bytes(u.mem_read(W+0x187,1))==b'\x1b' and call(u,0x8101064,0,27)==0xff13579b,'real expanded FX active label uses its separately configured role27')
check(u32(u,S['theme_knob_palette']+27*4)==0xff13579b,'indexed conversion mirrors the selected FX active-label color')
print(str(H.checks)+' v1.0.0-alpha.4 integration checks passed; hardware color delivery and listening remain device checks.')
