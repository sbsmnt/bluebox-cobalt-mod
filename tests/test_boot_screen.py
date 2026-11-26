"""Boot screen version label: the native start-up page constructor
(0x081418a4) runs for real; only adding the labels to the page is skipped.
Its version label reads Cobalt's text in a 160 px right-aligned label ending
at the right edge; the bluebox / by 1010music labels and the shared 1.5.2
string (TOOLS) are unchanged. How it looks on the LCD is a hardware test."""
from firmware_harness import *
import firmware_harness as H
PAGE=0x24060000
def boot(image):
 u=machine(image);stub(u,0x813a344,lambda q:None);call(u,0x81418a4,PAGE)
 text=lambda off:bytes(u.mem_read(PAGE+off+0x31,32)).split(b'\0')[0]
 rect=lambda off:struct.unpack('<4i',u.mem_read(PAGE+off+4,16))
 return text,rect,u
t,r,u=boot(patched);ts,rs,us=boot(stock)
check(ts(0x1ec)==b'1.5.2' and rs(0x1ec)==(200,0,120,20),'stock: version label "1.5.2" at x 200, width 120')
check(t(0x1ec)==b'1.5.2 Cobalt 1.3' and r(0x1ec)==(160,0,160,20),'Cobalt: version label "%s" at %s (x 160, width 160, ends at 320)'%(t(0x1ec).decode(),r(0x1ec)))
check(u.mem_read(PAGE+0x1ec+0x51,1)==us.mem_read(PAGE+0x1ec+0x51,1)==b'\x03','version label keeps its native alignment style (3)')
check(t(0x34)==ts(0x34)==b'bluebox' and t(0x8c)==ts(0x8c)==b'by 1010music' and r(0x34)==rs(0x34) and r(0x8c)==rs(0x8c),'bluebox and by 1010music labels unchanged')
check(bytes(u.mem_read(0x08146bd0,6))==b'1.5.2\0','the shared 1.5.2 string (TOOLS, others) is untouched')
print(H.checks,'boot screen checks passed; the look on the LCD is a hardware test.')
