"""Complete native FX panel construction/population/layout across style changes.
Reproduces alpha.1's leaked Shoegaze controls, then checks stock/SG switching.
"""
from firmware_harness import *
import firmware_harness as H
import importlib.util
APP=0x300173d0;P=0x24010000;TMP=0x24008000
def setup(image,syms):
 u=machine(image);heap=[0xc0010000]
 def alloc(q):
  a=heap[0];heap[0]=(a+q.reg_read(UC_ARM_REG_R0)+15)&~15;q.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
 for kind,off in [(4,0xe48),(5,0xe58),(8,0xe78)]:
  u.mem_write(TMP,bytes([kind]));call(u,0x81202b8,APP+off,TMP)
  vt=0x24007000+kind*0x20;fn=0x80dfd00+kind*4;w32(u,APP+off,vt);w32(u,vt,fn|1)
  stub(u,fn,lambda q,k=kind:q.reg_write(UC_ARM_REG_R0,k))
 stub(u,0x8121f4c,lambda q:q.reg_write(UC_ARM_REG_R0,0))
 call(u,0x812a98c,P)
 u.mem_write(P+4,struct.pack('<4i',0,0,320,90));w32(u,P+0x38,320)
 return u
def show(u,syms,style,expanded=0,slot=21):
 call(u,0x8120264,APP+0xe58,0x155,style)
 u.mem_write(P+0xc398,bytes([expanded]));u.mem_write(TMP,struct.pack('<H',slot))
 call(u,syms['dly_populate'],P,TMP);call(u,syms['dly_layout'],P)
 visible=[]
 for i in range(13):
  k=P+0xc3b0+i*0x3d0;t=P+0x100b0+i*0x1d4
  if u.mem_read(k+0x30,1)==b'\0':visible.append((u32(u,k+0x94)&65535,struct.unpack('<4i',u.mem_read(k+4,16))))
  if u.mem_read(t+0x30,1)==b'\0':visible.append((u32(u,t+0x94)&65535,struct.unpack('<4i',u.mem_read(t+4,16))))
 return visible
# An optional baseline source is a local regression fixture, not distributed.
baseline=ROOT.parent/'bluebox-shoegaze-alpha1'/'patch_bluebox.py'
if baseline.exists():
 spec=importlib.util.spec_from_file_location('sg_alpha1',baseline);old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
 osyms=old.MANIFEST['symbols']['4'];q=setup(old.transform(stock),osyms)
 show(q,osyms,15);bad=show(q,osyms,0)
 check(any(0x1af<=pid<=0x1b5 for pid,_ in bad),'alpha.1 reproduction: stock style incorrectly displays Shoegaze controls')
S=M['symbols']['4'];u=setup(patched,S)
for expanded in [0,1]:
 for style in range(15):
  custom=show(u,S,15,expanded);native=show(u,S,style,expanded)
  check(len(custom)==8 and {pid for pid,_ in custom}==set(range(0x1af,0x1b6))|{0x15a},'full native Shoegaze panel binds exactly eight controls '+str((expanded,style)))
  check(native and len(native)<=8 and all(not 0x1af<=pid<=0x1b5 for pid,_ in native),'stock panel contains only native controls after leaving Shoegaze '+str((expanded,style)))
  if not expanded:
   rects=[r for _,r in native]
   check(all(0<=x and 0<=y and x+w<=320 and y+h<=90 for x,y,w,h in rects),'stock compact controls stay inside the screen '+str(style))
   overlap=False
   for i,(x,y,w,h) in enumerate(rects):
    for xx,yy,ww,hh in rects[i+1:]:overlap |= x<xx+ww and xx<x+w and y<yy+hh and yy<y+h
   check(not overlap,'stock compact controls do not overlap '+str(style))
# Restore FX1 and return to FX2 using the same fully initialized native panel.
for mode in [2,1,5,4]:
 call(u,0x8120264,APP+0xe48,0x1a0,mode);show(u,S,15,0,20)
 native=show(u,S,14)
 check(all(pid<0x1af or pid>0x1b5 for pid,_ in native),'FX1-to-stock-FX2 switch does not leak custom controls, mode '+str(mode))
print(str(H.checks)+' complete native style-switch control layout checks passed; hardware display confirmation remains.')
