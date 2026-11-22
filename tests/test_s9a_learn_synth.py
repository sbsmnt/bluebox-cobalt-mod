"""S9a: MIDI Learn on the synth TRACK pages. Real TRACK view, native learn
info (0x08123d00), knob/toggle learn overlays, SessionMgr arm (event 0x13e)
and CC learn (0x08123e44). Only allocation, IPC, notification fan-out and the
M7 map message (0x0810884a) are substituted."""
from gate_ui_fixture import *
import firmware_harness as H
EV=TMP+0x500;MSG=TMP+0x700;MAP=0x24070000
OFFS=[0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xda78,0xd2d8,0xde48]
BUTTON=T+0xe814
SYN=[0x1bf,0x1c3,0x1c2,0x1c4,0x1c5,0x1c9,0x1c6,0x1dd] # S18: Reso in Drone's place
PAGE2=[0x1ca,0x1d7,0x1cc,0x1d9] # S13: LFO Rate, LFO2 Rate, LFO Amp, LFO2 Amp
setup(0,7,-96000);u.mem_write(APP+0x7398,struct.pack('<H',7));u.mem_write(APP+0x739a,b'\x05')
stub(u,0x8102a4c,lambda q:None);stub(u,0x8108950,lambda q:None)
sent=[];stub(u,0x810884a,lambda q:sent.append(bytes(q.mem_read(q.reg_read(UC_ARM_REG_R1),24))))
w32(u,APP+0x5ef8,MAP);w32(u,APP+0x5efc,0)
def input(kind,index=0,delta=0):
 u.mem_write(EV,struct.pack('<6I',kind,0x8148664,0,index,delta&0xffffffff,0));call(u,0x8123862,APP,EV)
def refresh(ch=7):
 u.mem_write(EV,struct.pack('<6I',0x63,0x8148664,ch,0,0,0));call(u,u32(u,0x814bb70),T,EV)
def tap_synth():
 call(u,u32(u,u32(u,BUTTON)+0x18),BUTTON,u32(u,BUTTON+4)+10,u32(u,BUTTON+8)+10);flush()
def knob(i):return T+OFFS[i]
def bound(i):return struct.unpack('<H',u.mem_read(knob(i)+0x94,2))[0]
# Learn overlay: knob +0x2f4, toggle +0x100; +0x30 hidden, +0xc0 0x1a (+1 armed), text at +0x65.
def overlay(w,toggle=False):
 o=w+(0x100 if toggle else 0x2f4)
 return u.mem_read(o+0x30,1)[0]==0,u32(u,o+0xc0)==0x1b,bytes(u.mem_read(o+0x65,16)).split(b'\0')[0]
def learn_tap(w,toggle=False):
 parents.clear();u.mem_write(EV,struct.pack('<6I',0x13e,0x8148664,0,0,0,0))
 call(u,0x8126f4c if toggle else 0x8127e98,w,EV)
 posted=[p for p in parents if struct.unpack_from('<H',p)[0]==0x13e]
 for p in posted:u.mem_write(EV,p);call(u,0x8125d44,APP,EV)
 refresh()
 return [(t,x,a&0xffff,b) for t,x,a,b in (struct.unpack_from('<HxxIII',p) for p in posted)] # cell is a u16 in that word (as test_s9c)
def learn_cc(cc,chan=2):
 sent.clear()
 u.mem_write(MSG,bytes(0x18));u.mem_write(MSG+0xc,struct.pack('<II',chan,0xb0));u.mem_write(MSG+0x14,struct.pack('<H',cc))
 call(u,0x8123e44,APP,MSG);refresh()
def items():
 return [struct.unpack_from('<HHHHH',bytes(u.mem_read(MAP+i*20,20))) for i in range(u32(u,APP+0x5efc))]

input(0xf9,1);input(0xf9,1);refresh();tap_synth();refresh()
check([bound(i) for i in range(1,8)]==SYN[1:],'synth page 1 bound')
check(not any(overlay(knob(i))[0] for i in range(8)) and not overlay(BUTTON,True)[0],'Learn off: no learn overlay on any synth control')
u.mem_write(APP+0x73b3,b'\1');refresh()
check(all(overlay(knob(i))[0] for i in range(1,8)) and not overlay(knob(0))[0],'Learn on: the seven shown synth knobs get the learn outline, the hidden Level knob none')
check(overlay(BUTTON,True)[0],'Learn on: SYN ON gets the learn outline')
e=learn_tap(knob(1))
check(e==[(0x13e,0x8148664,7,SYN[1])],'tapping Timbre in Learn posts 0x13e for track 8 and the synth id '+str(e))
check(struct.unpack('<HH',u.mem_read(APP+0x73ba,4))==(SYN[1],7) and overlay(knob(1))[1] and not any(overlay(knob(i))[1] for i in range(2,8)),'Timbre is armed and only its knob shows armed')
learn_cc(20)
check(items()==[(2,0xb0,20,SYN[1],7)],'the next CC maps channel 3 CC 20 to Timbre of track 8 '+str(items()))
check(len(sent)==1 and sent[0][0]==0x7c and struct.unpack_from('<IIHH',sent[0],12)==(2,0xb0,20,SYN[1]) and struct.unpack_from('<H',sent[0],2)[0]==7,'the M7 is told the new map item (0x7c)')
check(overlay(knob(1))[2]==b'3:20' and all(overlay(knob(i))[2]==b'' for i in range(2,8)),'Timbre shows its CC "3:20", the others none')
e=learn_tap(BUTTON,True);learn_cc(21)
check(e==[(0x13e,0x8148664,7,0x1bf)] and (2,0xb0,21,0x1bf,7) in items() and overlay(BUTTON,True)[2]==b'3:21','SYN ON maps like a stock toggle and shows its CC')
# Page 2 knobs (A cycles the focus to page 2 right, then page 2).
from_ui=0x388001e0
w32(u,from_ui+0x1c,1);refresh()
check([bound(i) for i in range(4)]==PAGE2 and not overlay(BUTTON,True)[0],'page 2: the LFO knobs bound on the left and the hidden SYN ON loses its outline')
check(all(overlay(knob(i))[0] for i in range(7)) and not overlay(knob(7))[0],'page 2: the seven shown knobs are learnable, the empty slot not')
e=learn_tap(knob(0));learn_cc(22)
check((2,0xb0,22,0x1ca,7) in items() and overlay(knob(0))[2]==b'3:22' and all(overlay(knob(i))[2]==b'' for i in range(1,7)),'page 2 LFO Rate maps and shows only its own CC')
e=learn_tap(knob(1));learn_cc(23)
check(e==[(0x13e,0x8148664,7,0x1d7)] and (2,0xb0,23,0x1d7,7) in items() and overlay(knob(1))[2]==b'3:23','S10: LFO2 Rate maps and shows its CC')
e=learn_tap(knob(3));learn_cc(24)
check(e==[(0x13e,0x8148664,7,0x1d9)] and (2,0xb0,24,0x1d9,7) in items() and overlay(knob(3))[2]==b'3:24','S10: LFO2 Amount maps and shows its CC')
# Leaving Learn hides the outlines again.
u.mem_write(APP+0x73b3,b'\0');refresh()
check(not any(overlay(knob(i))[0] for i in range(8)),'Learn off again: outlines gone')
print(H.checks,'S9a synth MIDI Learn checks passed; controller response needs hardware.')
