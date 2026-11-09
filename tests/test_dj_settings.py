"""DJ settings with the real model setter on the real TRACK page: band count,
presets (crossovers + ISO; manual edits switch to Custom), the 3-band knob
layout (fixed places), rebinds after native changes and the settings rows that follow the band count."""
from gate_ui_fixture import *
import firmware_harness as H
D=M['symbols']['dj4'];MAIN=APP+0xe78;FAKEAPP=0xc0700000;UI=0x388001ec;VIEWOBJ=0x24074000
KNOBS=[0xc398,0xc768,0xcb38,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48]
def get(id):return struct.unpack('<i',struct.pack('<I',call(u,0x8120234,MAIN,id)))[0]
def h16(a):return struct.unpack('<H',u.mem_read(a,2))[0]
def text(a,n=8):return bytes(u.mem_read(a,n)).split(b'\0')[0].decode()
def rec(i):return struct.unpack('<HBBIIIiiI',u.mem_read(0xc0010000+i*28,28))
def key(i):return text(rec(i)[8],16)
CELL=0x24079000;u.mem_write(CELL,struct.pack('<H',0x0c))
def edit(id,v):call(u,D['dj_knob'],APP,CELL,id,v,0)

check([key(0x1c0+i) for i in range(9)]==['djcurve','djxlo','djxmid','djxhi','djbands','djpreset4','djpreset3','djslope','djresmax'],
      'settings defined: Curve, Xover Lo/Mid/Hi, Bands, Preset (4 and 3 bands), Filter Slope, Res Max')
check(text(rec(0x1c4)[3],16)=='Bands:' and text(rec(0x1c5)[3],16)=='Preset:' and text(rec(0x1c7)[3],16)=='Filter Slope:','English row labels')

# Presets (old project: the first edit adds the settings with their defaults).
u.mem_write(UI,bytes(20));w32(u,0x3000d9fc,FAKEAPP)
edit(0x1c5,2)
check([get(0x1c1),get(0x1c2),get(0x1c3),get(0x1c0)]==[250,840,2500,0],'4-band preset Xone:92: 250 / 840 / 2500 Hz, ISO')
edit(0x1c5,4)
check([get(0x1c1),get(0x1c2),get(0x1c3)]==[200,690,2000],'4-band preset DJM-V10: 200 / 690 / 2000 Hz')
edit(0x1c2,800)
check(get(0x1c2)==800 and get(0x1c5)==0,'a crossover changed by hand switches the preset to Custom')
edit(0x1c0,1);edit(0x1c5,1)
check([get(0x1c1),get(0x1c2),get(0x1c3),get(0x1c0)]==[200,1000,5000,0],'Default preset restores 200 / 1000 / 5000 Hz and ISO')

# 3 bands: the band count applies the 3-band preset; its list is separate.
edit(0x1c4,1)
check(get(0x1c4)==1 and [get(0x1c1),get(0x1c3)]==[300,3000],'3 Bands applies the 3-band Default preset: 300 / 3000 Hz')
edit(0x1c6,3)
check([get(0x1c1),get(0x1c3)]==[710,1800] and get(0x1c5)==1,'3-band preset Xone:42: 710 / 1800 Hz; the 4-band preset is untouched')
edit(0x1c5,2)
check([get(0x1c1),get(0x1c3)]==[710,1800],'the 4-band preset list does not act while 3 bands are selected')

# Settings rows follow the band count.
u.mem_write(UI,struct.pack('<I',0x444a5534)+bytes(16));u.mem_write(UI+18,bytes([1]))
LIST=0x24078000;u.mem_write(LIST,bytes(0x100))
rows=[];rh=stub(u,0x8138546,lambda u:rows.append(u.reg_read(UC_ARM_REG_R1)))
call(u,D['dj_page_rows'],APP,CELL,LIST)
check(rows==[0x1c4,0x1c6,0x1c0,0x1c1,0x1c3,0x1c7,0x1c8],'3-band rows: Bands, Preset (3-band list), Curve, Xover Lo, Xover Hi, Filter Slope, Res Max')
u.hook_del(rh)

# 3-band knob layout on the real TRACK page: block 1 High, Mid, Low, Filter.
setup(0,2,-40000)
w32(u,FAKEAPP+0x2b4,T);u.mem_write(VIEWOBJ+0x739a,bytes([5]))
u.mem_write(UI,struct.pack('<III',0x444a5534,VIEWOBJ,0)+bytes([0,0,5,0xff,1,1,0,0]))
stub(u,D['cue_ui_tick'],lambda u:None);call(u,D['dj_ui_tick'],0x24072000,0x24072100)
SHOWN=[0,1,3,4,5];ids=[h16(T+KNOBS[k]+0x94) for k in SHOWN];names=[text(T+KNOBS[k]+0xf8) for k in SHOWN]
check(ids==[0x1ba,0x1bb,0x1bd,0x1be,0x1bf] and names==['HIGH','MID','LOW','FILTER','RES'],'3 bands: knobs keep their places (HIGH, MID, -, LOW | FILTER, RES)')
check(all(u.mem_read(T+KNOBS[k]+0x30,1)[0]==1 for k in (2,6,7)),'the Lo Mid place and the last two knobs are hidden')
BTN=[0xe9e8,0xe46c,0xe298,0xe640]
check([text(T+o+0x65)+' '+text(T+o+0xd1) for o in (0xe9e8,0xe46c,0xe640)]==['Kill High','Kill Mid','Kill Low'] and u.mem_read(T+0xe298+0x30,1)[0]==1,
      '3 bands: kill buttons HI / MD / LO in the knob places, the Lo Mid button is hidden')
# A native rebind (Gate shows its knob again on return from a menu) is undone.
u.mem_write(T+KNOBS[7]+0x30,bytes(1));call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check(u.mem_read(T+KNOBS[7]+0x30,1)[0]==1,'a knob shown again by a native rebind is hidden on the next tick')
# Back to 4 bands rebinds the DJ view.
edit(0x1c4,0);call(u,D['dj_ui_tick'],0x24072000,0x24072100)
check([h16(T+k+0x94) for k in KNOBS[:6]]==[0x1ba+i for i in range(6)] and text(T+KNOBS[1]+0xf8)=='HI MID','4 bands again: HIGH, HI MID, LO MID, LOW, FILTER, RES')
check([get(0x1c1),get(0x1c2),get(0x1c3)]==[250,840,2500],'and the remembered 4-band preset (Xone:92, chosen above) is applied again')
print('DJ settings checks:',H.checks)
