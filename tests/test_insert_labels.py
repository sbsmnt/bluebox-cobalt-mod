"""Check actual native on-screen caption strings, not just parameter IDs."""
from gate_ui_fixture import *
import firmware_harness as H
IU=0x388001e0;EV=TMP+0x400;OFFS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08]
def text(i):return bytes(u.mem_read(T+OFFS[i]+0xd1,32)).split(b'\0')[0].decode()
def refresh(ch):
 u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,ch,0,0,0));call(u,u32(u,0x814bb70),T,EV)
setup(0,7,-96000)
for quad in (0,1):
 call(u,0x8120264,APP+0xe78,0x18c,quad)
 for ch in (0,7,11):
  w32(u,IU+4,0);refresh(ch)
  check([text(i) for i in range(3)]==['VOL','GAIN','PAN'],'original visible custom captions before insert '+str((quad,ch)))
  w32(u,IU+4,1);refresh(ch)
  check([text(i) for i in range(4)]==['CLIP','DRIVE','TONE','MIX'],'insert visible labels (fresh track shows CLIP) replace original VOL/GAIN/PAN captions '+str((quad,ch)))
  for delta in (80,-80,400,-400):
   u.mem_write(EV,struct.pack('<6I',0x32,0x8148664,0,2,delta&0xffffffff,0));call(u,u32(u,0x814bb70),T,EV);flush()
   check(text(2)=='TONE','visible Tone label survives actual encoder editing/rebind '+str((quad,ch,delta)))
  w32(u,IU+4,0);refresh(ch)
  check([text(i) for i in range(3)]==['VOL','GAIN','PAN'],'return restores original visible captions '+str((quad,ch)))
print(H.checks,'native visible caption checks passed; physical display remains a device check.')
