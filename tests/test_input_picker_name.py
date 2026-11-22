"""Input-picker header follows selection on tracks without an input.
Native populate (0x08131ea4) queries the selected track's input parameter
0x112 and, when it is zero, skips both the source-row highlight and the
header name write: the picker header kept the previously selected track's
name (the name editor always shows the real one). The fix retargets the cbz
at 0x08131eee so the name write runs for every selection while the highlight
stays correctly skipped. Runs the real well constructor, selection global
handler, model query and name getter on the stock and patched images.
"""
from gate_ui_fixture import *

W=0x24040000;EV2=TMP+0x300;NAME=0xc0100000

def build_well(image):
    u=machine(image);heap=[0xc0010000]
    def alloc(u):
        a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
    stub(u,0x8103128,alloc);call(u,0x8134cb8,0);call(u,0x8120abc)
    for ch in range(12):
        u.mem_write(TMP,b'\1');call(u,0x81202b8,APP+0xc08+ch*0x30,TMP)
    w32(u,APP+0xe78+4,0x24058000)
    u.mem_write(APP+0xe78+10,struct.pack('<HH',1,68))
    u.mem_write(0x24058000,struct.pack('<H2xi',0x18c,0))
    # get_name (0x08121f4c) scans 0x400 name-reference slots at model+0xf08
    # (stride 0x14) and dereferences each slot's string pointer.
    u.mem_write(NAME,b'\0')
    for i in range(0x400):w32(u,APP+0xf08+i*0x14,NAME)
    call(u,0x81320ec,W)
    check(u32(u,W)==0x814bafc,'well constructor leaves its own vtable')
    return u
def header(u):return bytes(u.mem_read(W+0xbd21,0x20)).split(b'\0')[0]
def channel(u):return struct.unpack('<H',u.mem_read(W+0xbc9c,2))[0]
def select(u,ch):
    u.mem_write(EV2,struct.pack('<6I',0x63,0x8148664,ch,0,0,0))
    call(u,u32(u,u32(u,W)+0x34),W,EV2)
def setin(u,ch,v):call(u,0x8120264,APP+0xc08+ch*0x30,0x112,v)
def setname(u,ch,name):u.mem_write(APP+0xc16+ch*0x30,name.ljust(9,b'\0')[:9])

for image,tag in ((stock,'STOCK'),(patched,'PATCHED')):
    u=build_well(image)
    for ch in range(12):setname(u,ch,b'TRK%02dNAM'%ch)
    setin(u,3,1);setin(u,7,0)
    select(u,3)
    check(header(u)==b'TRK03NAM' and channel(u)==3,tag+' picker shows the selected track name when it has an input '+str(header(u)))
    select(u,7)
    if image is stock:
        check(header(u)==b'TRK03NAM',tag+' baseline: header keeps the previous name when the track has no input '+str(header(u)))
    else:
        check(header(u)==b'TRK07NAM' and channel(u)==7,tag+' header follows selection even without input '+str(header(u)))
    select(u,3)
    check(header(u)==b'TRK03NAM',tag+' header still updates going back to a track with input '+str(header(u)))
    # A renamed track shows its new name on the next selection.
    setname(u,7,b'RENAMED!');select(u,7)
    want=b'TRK03NAM' if image is stock else b'RENAMED!'
    check(header(u)==want,tag+' selection picks up the edited name '+str(header(u)))
