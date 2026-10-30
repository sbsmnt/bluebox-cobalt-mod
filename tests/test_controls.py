"""v0.7 native buttons/list/widget input and timed-value regressions.
Display output and model/event queues are boundaries; real parameter defs,
selector list, knob binding/acceleration and label setter are executed.
"""
from firmware_harness import *
import firmware_harness as H
S=M['symbols']['4'];MODEL=0x300173d0;SET=MODEL+0xe48
P=0x24010000;W=P+0xc3b0;POP=0xc0100000;LIST=POP+0x1090;EV=0x24060000;TICK=0x3001eb3c

def init(image=patched):
 u=machine(image);heap=[0xc0010000]
 def alloc(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,alloc);call(u,0x8134cb8,0)
 u.mem_write(0x24008000,b'\x04');call(u,0x81202b8,SET,0x24008000)
 call(u,0x8138154,POP+0xf50);call(u,0x8138154,POP+0xff0)
 return u

def event(u,typ,pid=0,value=0,slot=20):
 u.mem_write(EV,struct.pack('<H6xH2xIII',typ,slot,pid,value&0xffffffff,0));return EV

def text(u,w):return bytes(u.mem_read(w+0xd1,32)).split(b'\0')[0].decode()

# Derive action-to-slot/control-group mapping through the original action dispatcher.
for action,slot,group in [(14,20,0),(15,20,1),(16,21,0),(17,21,1)]:
 u=init();seen=[]
 stub(u,0x8102a4c,lambda u:None)
 stub(u,0x813c384,lambda u:seen.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
 call(u,0x8122508,MODEL,action,0,0)
 check(len(seen)==1 and struct.unpack_from('<H',seen[0])[0]==0x93 and struct.unpack_from('<H',seen[0],8)[0]==slot and struct.unpack_from('<I',seen[0],12)[0]==group and bytes(u.mem_read(MODEL+0x739a,1))==bytes([action]),'native action '+str(action)+' establishes slot '+str(slot)+' group '+str(group))
 # Continue into real B handler with the state established by that action.
 seen.clear();stub(u,0x8108950,lambda u:None)
 call(u,0x8123862,MODEL,event(u,7))
 check(len(seen)==1 and struct.unpack_from('<H',seen[0])[0]==0x9d and struct.unpack_from('<H',seen[0],8)[0]==slot,'action-to-B integration preserves FX slot '+str(slot)+' from action '+str(action))

# Native A/B branch actually executed, including the exact patched B call.
for mode in [14,15,16,17,19]:
 u=init();u.mem_write(MODEL+0x739a,bytes([mode]));seen=[]
 stub(u,0x8102a4c,lambda u:None);stub(u,0x8108950,lambda u:None)
 def capture(u):seen.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24)))
 stub(u,0x813c384,capture)
 call(u,0x8123862,MODEL,event(u,7))
 check(seen and struct.unpack_from('<H',seen[0])[0]==0x9d and struct.unpack_from('<H',seen[0],8)[0]==(20 if mode in [14,15] else 21),'real B dispatch opens FX1 slot20 only for FX1 controls; mode '+str(mode))
for mode in [14,15,16,17]:
 u=init();u.mem_write(MODEL+0x739a,bytes([mode]));seen=[]
 stub(u,0x813c384,lambda u:seen.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
 call(u,0x8123862,MODEL,event(u,12))
 check(len(seen)==1 and struct.unpack_from('<H',seen[0])[0]==0x164,'real A dispatch emits control-group cycle for mode '+str(mode))
 pages=[];stub(u,0x812ab7c,lambda u:pages.append(u.reg_read(UC_ARM_REG_R1)))
 u.mem_write(P+0xc3a4,struct.pack('<H',20 if mode in [14,15] else 21))
 # Native FX handler calls original group selector; only final widget boundary stubbed.
 for group in [0,1,0]:
  w32(u,P+0xc3a0,group);call(u,S['dly_event'],P,event(u,0x164))
 check(pages==[1,0,1],'A cycles left/right groups repeatedly in '+('FX1' if mode in [14,15] else 'FX2'))

# Native selector object/list routines, row text, saved selection and emitter.
u=init();call(u,0x813c004,LIST);u.mem_write(LIST+4,struct.pack('<4i',0,0,320,180))
for selection in [5,0,1,2,3,4,5]:
 call(u,0x8120264,SET,0x1a0,selection)
 u.mem_write(POP+0xd99a,struct.pack('<H',20));call(u,S['dly_selector_fill'],POP)
 count=u32(u,LIST+0xc8e4)
 names=[bytes(u.mem_read(LIST+0x150+i*0xc8+0x31,32)).split(b'\0')[0] for i in range(count)]
 check(count==6 and names==[b'Delay',b'Warp delay',b'Granular',b'Tape',b'Ducked',b'Reverse'],'native FX1 selector contains all five FX1 types, selection '+str(selection))
 check(struct.unpack('<H',u.mem_read(POP+0xd998,2))[0]==0x1a0 and u32(u,LIST+0xc8e8)==(0 if selection==5 else selection+1),'native FX1 selector carries Mode id and saved highlight '+str(selection))
seen=[];stub(u,0x813a414,lambda u:seen.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
call(u,u32(u,0x814c62c),POP,event(u,0x150,2)) # list emits selection in event +12
check(len(seen)==1 and struct.unpack_from('<H',seen[0])[0]==0x6e and struct.unpack_from('<H',seen[0],8)[0]==20 and struct.unpack_from('<II',seen[0],12)==(0x1a0,1),'native selector emits exactly one Granular change for FX1 through the stock parent path')
for selected in [2,3,4]:
 seen.clear();call(u,u32(u,0x814c62c),POP,event(u,0x150,selected+1))
 check(len(seen)==1 and struct.unpack_from('<II',seen[0],12)==(0x1a0,selected),'native selector emits distinct saved value for new type '+str(selected))
# FX2 must call the untouched builder with the same arguments and return path.
orig=[];stub(u,0x8139aec,lambda u:orig.append(u.reg_read(UC_ARM_REG_R0)))
u.mem_write(POP+0xd99a,struct.pack('<H',21));call(u,S['dly_selector_fill'],POP)
check(orig==[POP],'FX2 selector delegates to original reverb builder exactly once')

# Execute the complete original FX2 fill, comparing actual rows and highlight
# against stock. Only display blitting remains outside this list fixture.
rows=[]
for image in [stock,patched]:
 q=init(image);q.mem_write(0x24008000,b'\x05');call(q,0x81202b8,MODEL+0xe58,0x24008000)
 call(q,0x813c004,LIST);q.mem_write(LIST+4,struct.pack('<4i',0,0,320,180));q.mem_write(POP+0xd99a,struct.pack('<H',21))
 call(q,0x8139aec if image==stock else S['dly_selector_fill'],POP)
 count=u32(q,LIST+0xc8e4)
 rows.append((bytes(q.mem_read(POP+0xd998,4)),u32(q,LIST+0xc8e8),[bytes(q.mem_read(LIST+0x181+i*0xc8,32)).split(b'\0')[0] for i in range(count)]))
check(rows[0][:2]==rows[1][:2] and rows[0][2]==rows[1][2][:15] and rows[1][2][15:]==[b'Gazer'],'native FX2 list retains the fifteen stock styles and adds Gazer (exp.8 name) at the end')

# Real knob registration and native text setter. Keep UI parent as a boundary.
def knob(u,pid,value):
 u.mem_write(W,bytes(0x3d0));u.mem_write(W+0x32,struct.pack('<H',20));u.mem_write(W+0x3c9,b'\x01')
 call(u,0x8127b84,W,pid,value&0xffffffff,1)
 u.mem_write(P+0xc3a4,struct.pack('<H',20))
 # Register panel ownership without invoking its native children/layout.
 stub(u,0x812abe0,lambda u:None)
 slot=0x24008010;u.mem_write(slot,struct.pack('<H',20));call(u,S['dly_populate'],P,slot)
 return W

u=init();# Geometry callbacks are a boundary for dly_populate.
V=0x24009000;RECT=0x80dfe30;w32(u,V+0x20,RECT|1)
stub(u,RECT,lambda u:u.mem_write(u.reg_read(UC_ARM_REG_R0)+4,bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),16))))
# Seed layout widgets before knob() clears the chosen widget; avoid layout moves
# by keeping the panel expanded in this numeric/label fixture.
u.mem_write(P+0xc398,b'\x01')
stub(u,0x812afd8,lambda u:None)
# Capture events while allowing native bind/setter to store values normally.
changes=[];stub(u,0x813a414,lambda u:changes.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
for pid,value,expected in [(0x1a1,125,'12.5%'),(0x1a2,80,'80ms'),(0x1a3,4,'4'),(0x4a,-7,'-7 st'),(0x1a4,1000,'100.0%')]:
 knob(u,pid,value);w32(u,TICK,10000)
 # A zero delta still runs the physical-turn route without changing the value.
 call(u,S['dly_turn'],W,0)
 check(text(u,W)==expected,'turn displays exact current value and units for '+hex(pid))
 w32(u,TICK,12999);call(u,S['dly_tick'],P,0x24060040)
 check(text(u,W)==expected,'value remains visible at 2999ms for '+hex(pid))
 w32(u,TICK,13000);call(u,S['dly_tick'],P,0x24060040)
 check(text(u,W) in ['POSIT','SIZE ','DENSI','PITCH','JITTE'],'native label returns at 3000ms for '+hex(pid)+': '+text(u,W))
# Added delay controls share unused timer slots within their own mode and
# retain native label restoration, including the four-digit Release range.
for pid,value,expected in [(0x1a7,350,'35.0%'),(0x1a8,700,'70.0%'),(0x1a9,2000,'2000ms'),(0x1aa,400,'400ms'),(0x1ab,650,'65.0%')]:
 knob(u,pid,value);call(u,0x81278b0,W,1);label=text(u,W);w32(u,TICK,50000)
 call(u,S['dly_turn'],W,0)
 check(text(u,W)==expected,'new control displays correct value/units for '+hex(pid))
 w32(u,TICK,52999);call(u,S['dly_tick'],P,0x24060040)
 check(text(u,W)==expected,'new control holds its value for 2999ms '+hex(pid))
 w32(u,TICK,53000);call(u,S['dly_tick'],P,0x24060040)
 check(text(u,W)==label,'new control restores its actual native label at 3000ms '+hex(pid))
# Separate knobs retain independent deadlines, and a native rebind during the
# hold does not shorten the timer or show an out-of-date value.
knob(u,0x1a1,125);W2=W+0x3d0;u.mem_write(W2,bytes(0x3d0));u.mem_write(W2+0x32,struct.pack('<H',20));u.mem_write(W2+0x3c9,b'\x01')
call(u,0x8127b84,W2,0x1a2,80,1)
w32(u,TICK,30000);call(u,S['dly_turn'],W,0)
w32(u,TICK,31000);call(u,S['dly_turn'],W2,0)
w32(u,TICK,33000);call(u,S['dly_tick'],P,0x24060040)
check(text(u,W)=='POSIT' and text(u,W2)=='80ms','each knob uses its own three-second deadline')
call(u,0x8127b84,W2,0x1a2,120,1);w32(u,TICK,33999);call(u,S['dly_tick'],P,0x24060040)
check(text(u,W2)=='120ms','native parameter rebind keeps showing the current value until the existing deadline')
w32(u,TICK,34000);call(u,S['dly_tick'],P,0x24060040)
check(text(u,W2)=='SIZE ','the independently updated knob still expires at its own deadline')

# Every move restarts its own timer, including moves that do not cross a semitone.
knob(u,0x4a,0);w32(u,TICK,20000);call(u,S['dly_turn'],W,1)
check(u32(u,W+0x98)==0 and text(u,W)=='0 st','a small pitch move accumulates fractional travel and shows the current value')
w32(u,TICK,22900);call(u,S['dly_turn'],W,1)
w32(u,TICK,23001);call(u,S['dly_tick'],P,0x24060040)
check(text(u,W)=='0 st','continued turning restarts the three-second pitch timer')
w32(u,TICK,25900);call(u,S['dly_tick'],P,0x24060040)
check(text(u,W)=='PITCH','pitch label returns three seconds after the final movement')
# Unsigned time deltas handle the native millisecond counter wrapping.
w32(u,TICK,0xfffffff0);call(u,S['dly_turn'],W,0)
w32(u,TICK,(0xfffffff0+2999)&0xffffffff);call(u,S['dly_tick'],P,0x24060040)
check(text(u,W)=='0 st','value timer survives native clock wrap before deadline')
w32(u,TICK,(0xfffffff0+3000)&0xffffffff);call(u,S['dly_tick'],P,0x24060040)
check(text(u,W)=='PITCH','value timer expires correctly across native clock wrap')
# Compare real native accelerated turn versus normalized pitch travel.
for direction in [1,-1]:
 u=init();u.mem_write(P+0xc398,b'\x01');stub(u,0x812abe0,lambda u:None)
 changes=[];stub(u,0x813a414,lambda u:changes.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
 knob(u,0x4a,0)
 # exp.8: Pitch takes 160 slow units per semitone (was ~42).
 for i in range(159):w32(u,TICK,i*1000);call(u,S['dly_turn'],W,direction&0xffffffff)
 check(u32(u,W+0x98)==0,'slow pitch travel retains sub-semitone input '+str(direction))
 w32(u,TICK,160000);call(u,S['dly_turn'],W,direction&0xffffffff)
 check(struct.unpack('<i',u.mem_read(W+0x98,4))[0]==direction and len(changes)==1,'160 slow encoder units produce one semitone and one native edit '+str(direction))
 for i in range(2000):w32(u,TICK,161000+i*1000);call(u,S['dly_turn'],W,direction&0xffffffff)
 check(struct.unpack('<i',u.mem_read(W+0x98,4))[0]==12*direction,'normalized pitch still reaches its octave limit '+str(direction))
 for i in range(160):w32(u,TICK,2200000+i*1000);call(u,S['dly_turn'],W,(-direction)&0xffffffff)
 check(struct.unpack('<i',u.mem_read(W+0x98,4))[0]==11*direction,'pitch reverses cleanly after reaching its bound '+str(direction))
# Non-pitch and FX2 pitch are delegated, with native return values intact.
u=init();delegate=[];stub(u,0x8127d50,lambda u:(delegate.append((u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1))),u.reg_write(UC_ARM_REG_R0,9)))
u.mem_write(W+0x94,struct.pack('<H',0x39));u.mem_write(W+0x32,struct.pack('<H',20));ret=call(u,S['dly_turn'],W,3)
check(ret==9 and delegate==[(W,3)],'ordinary FX1 controls retain native sensitivity and return')
u.mem_write(W+0x94,struct.pack('<H',0x4a));u.mem_write(W+0x32,struct.pack('<H',21));ret=call(u,S['dly_turn'],W,4)
check(ret==9 and delegate[-1]==(W,4),'FX2 controls retain native sensitivity')


# Reused popup headers: execute native setters including widget invalidation.
def header(u,offset):return bytes(u.mem_read(POP+offset+0x34+0x31,32)).split(b'\0')[0]
u=init();call(u,0x813c004,LIST)
u.mem_write(0x24008000,b'\x05');call(u,0x81202b8,MODEL+0xe58,0x24008000)
for slot in [20,21,20,21]:
 u.mem_write(POP+0xd99a,struct.pack('<H',slot));call(u,S['dly_selector_fill'],POP)
 check((header(u,0xf50),header(u,0xff0))==((b'FX1',b'Delay') if slot==20 else (b'FX2',b'Reverb')),'shared selector restores both headers for slot '+str(slot))
# Run the exact patched title call site with the native setter and real model.
LABEL=0x24050000;STR=0x24050100;u.mem_write(STR,b'Delay\0')
for mode,title in [(0,b'Warp'),(1,b'Granulr'),(2,b'Tape'),(3,b'Ducked'),(4,b'RevDly'),(5,b'Delay'),(0,b'Warp')]:
 call(u,0x8120264,SET,0x1a0,mode)
 call(u,0x812ac38,LABEL,STR,end=0x812ac3c)
 check(bytes(u.mem_read(LABEL+0x31,32)).split(b'\0')[0]==title,'native FX1 title call displays '+title.decode())
# Execute the full native Tools constructor and header layout, rather than
# only the setter: the existing middle field must remain visible and disjoint.
TOOLS=0xc0140000;call(u,0x813140c,TOOLS)
def tools_text(offset):return bytes(u.mem_read(TOOLS+offset+0x65,32)).split(b'\0')[0]
check(tools_text(0x34)==b'1.5.2','full Tools constructor retains the official version in the right field')
check(tools_text(0xd4)==b'Mout m9','full Tools constructor fills the existing middle header field')
rects=[struct.unpack('<4i',u.mem_read(TOOLS+o+4,16)) for o in [0x174,0xd4,0x34]]
check(rects==[(0,182,105,38),(106,182,106,38),(213,182,106,38)],'native header layout puts storage, mod and official version in three separate fields')
check(all(bytes(u.mem_read(TOOLS+o+0x30,1))==b'\0' for o in [0x174,0xd4,0x34]),'all three Tools header fields are visible')
# Exercise a native storage-text update; the new middle field must survive it.
STR=0x24050100;u.mem_write(STR,b'32GB\0');call(u,0x813820c,TOOLS+0x174,STR)
check(tools_text(0x174)==b'32GB' and tools_text(0xd4)==b'Mout m9' and tools_text(0x34)==b'1.5.2','native storage update leaves mod and official-version fields separate')
check(bytes(u.mem_read(0x8146bd0,6))==b'1.5.2\0','shared official version string remains intact')

# Density: actual native binding, encoder, fraction and indicator rendering.
u=init();stub(u,0x813a414,lambda u:None)
stub(u,0x813a1f8,lambda u:None);lines=[]
stub(u,S['theme_knob_line'],lambda u:lines.append((bytes(u.mem_read(u.reg_read(UC_ARM_REG_R0),8)),bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),8)))))
for n in [1,2,3,4]:
 u.mem_write(W,bytes(0x3d0));u.mem_write(W+0x32,struct.pack('<H',20));u.mem_write(W+0x3c9,b'\x01')
 call(u,0x8127b84,W,0x1a3,n,1)
 call(u,S['dly_knob_fraction'],W)
 fraction=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]
 check(abs(fraction-(n-1)/3)<1e-6 and u32(u,W+0x88)==4 and u32(u,W+0x84)==n-1 and u32(u,W+0x98)==n,'Density '+str(n)+' has full-scale drawing and unchanged native cursor/value')
 # Actual native renderer reaches the wrapper, then native sin/cos and line draw.
 lines.clear()
 u.mem_write(W+4,struct.pack('<4i',48,45,48,44));u.mem_write(EV,b'\x00\x01'+bytes(6))
 call(u,0x812808c,W,EV)
 check(len(lines)==1,'Density '+str(n)+' runs native pointer renderer')
 if n==1:first=lines[0]
 if n==4:check(lines[0]!=first,'Density minimum and maximum occupy different scale endpoints')
for direction,target in [(100,4),(-100,1)]:
 for i in range(10):w32(u,TICK,100000+i*1000);call(u,S['dly_turn'],W,direction&0xffffffff)
 check(u32(u,W+0x98)==target and u32(u,W+0x84)==target-1,'Density encoder reaches and clamps at '+str(target))
# FX2 and other integer controls retain their original drawing fraction.
u.mem_write(W+0x32,struct.pack('<H',21))
call(u,0x8127308,W);native=u.reg_read(UC_ARM_REG_S0)
call(u,S['dly_knob_fraction'],W)
check(u.reg_read(UC_ARM_REG_S0)==native,'FX2 drawing fraction remains native')
# Density now requires 256 slow units per integer and caps acceleration at2.
u=init();u.mem_write(P+0xc398,b'\x01');stub(u,0x812abe0,lambda u:None)
changes=[];stub(u,0x813a414,lambda u:changes.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
knob(u,0x1a3,1)
for i in range(255):w32(u,TICK,100000+i*1000);call(u,S['dly_turn'],W,1)
check(u32(u,W+0x98)==1 and not changes,'255 slow Density increments retain fractional movement without jumping')
w32(u,TICK,356000);call(u,S['dly_turn'],W,1)
check(u32(u,W+0x98)==2 and len(changes)==1,'256 slow Density increments emit one native edit')
for i in range(8):w32(u,TICK,360000+i*1000);call(u,S['dly_turn'],W,1)
for i in range(255):w32(u,TICK,380000+i*1000);call(u,S['dly_turn'],W,0xffffffff)
check(u32(u,W+0x98)==2,'Density reversal discards previous-direction travel')
w32(u,TICK,636000);call(u,S['dly_turn'],W,0xffffffff)
check(u32(u,W+0x98)==1 and len(changes)==2,'256 reverse units return Density to one without windup')
for i in range(128):w32(u,TICK,650000+i*1000);call(u,S['dly_turn'],W,1)
W2=W+0x3d0;u.mem_write(W2,bytes(0x3d0));u.mem_write(W2+0x32,struct.pack('<H',20));u.mem_write(W2+0x3c9,b'\x01');call(u,0x8127b84,W2,0x4a,0,1)
for i in range(160):w32(u,TICK,790000+i*1000);call(u,S['dly_turn'],W2,1)  # exp.8: 160 units/semitone
for i in range(128):w32(u,TICK,960000+i*1000);call(u,S['dly_turn'],W,1)
check(u32(u,W+0x98)==2 and u32(u,W2+0x98)==1,'Density/Pitch fractions remain independent')
for delta,target in [(100,4),(-100,1)]:
 for i in range(10):w32(u,TICK,1000000+i*1000);call(u,S['dly_turn'],W,delta&0xffffffff)
 check(u32(u,W+0x98)==target,'Density still reaches its bound '+str(target))
# First selector row maps to newly appended saved id5, old modes retain ids0..4.
u=init();call(u,0x813c004,LIST);u.mem_write(POP+0xd99a,struct.pack('<H',20))
call(u,S['dly_selector_fill'],POP)
seen=[];stub(u,0x813a414,lambda u:seen.append(bytes(u.mem_read(u.reg_read(UC_ARM_REG_R1),24))))
u.ctl_flush_tb()
for row,mode in enumerate([5,0,1,2,3,4]):
 seen.clear();call(u,u32(u,0x814c62c),POP,event(u,0x150,row))
 check(len(seen)==1 and struct.unpack_from('<II',seen[0],12)==(0x1a0,mode),'selector row '+str(row)+' maps to backward-compatible saved mode '+str(mode))
seen.clear();u.mem_write(POP+0xd99a,struct.pack('<H',21));call(u,u32(u,0x814c62c),POP,event(u,0x150,2))
check(len(seen)==1 and struct.unpack_from('<I',seen[0],16)[0]==2,'FX2 selector event value passes through unchanged')
print(str(H.checks)+' total v1.0.0-alpha.4 control checks passed; physical buttons/touch and display remain device tests.')
