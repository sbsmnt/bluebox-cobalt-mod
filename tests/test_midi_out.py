"""USB MIDI out probe (m1) under Unicorn (../../EXPLORATION.md).
Runs the stock MIDI-out flush (0x08103dc8: USB-out ring -> TX slot), the
patched converter, and the stock USB-host MIDI SOF handler (0x0810596c) up to
Transmit, with the probe driven through pc_main. No USB hardware: the class
pData/endpoint are set by hand and the RTOS message post is stubbed.
"""
from firmware_harness import *
import firmware_harness as H

PC=M['symbols']['pcm4']
PORT=0x30002c50;RING=PORT+0x5070;RING_R=PORT+0x5570;RING_W=PORT+0x5574
SLOT=PORT+0x6180;SLOT_R=PORT+0x6d88;SLOT_W=PORT+0x6d8c
CLASS_DATA=0x300000b4;HANDLE=0x3000a854;PHOST=0x30009fe8
SESSION=0x300173d0;NOW=0x3001eb3c;LOADNAME=0x3001734c;LOADBUF=0x24012000

def push(u,msgs,cable=0):
 w=u32(u,RING_W)
 for i,m in enumerate(msgs):u.mem_write(RING+((w+i)&0xff)*5,bytes(m[:3]+[0]*(3-len(m)))+bytes([cable,len(m)]))
 w32(u,RING_W,w+len(msgs))
def flush(u):
 call(u,0x8103dc8,PORT)
 w=u32(u,SLOT_W)
 if w==0:return b''
 s=(w-1)&3;n=struct.unpack('<H',u.mem_read(SLOT+s*0x302+0x300,2))[0]
 return bytes(u.mem_read(SLOT+s*0x302,n))
def packets(b):return [tuple(b[i:i+4]) for i in range(0,len(b),4)]

# 1. Converter: stock drops CC/PC/pitch bend, patched passes every channel message.
msgs=[[0x90,60,100],[0xbe,1,64],[0xce,5],[0xe0,0,64],[0x80,60,0],[0xf8],[0xf0],[0xfa]]
want_patched=[(9,0x90,60,100),(0xb,0xbe,1,64),(0xc,0xce,5,0),(0xe,0xe0,0,64),(8,0x80,60,0),(0xf,0xf8,0,0),(0xf,0xfa,0,0)]
want_stock=[(9,0x90,60,100),(8,0x80,60,0),(0xf,0xf8,0,0),(0xf,0xfa,0,0)]
for image,want,label in ((stock,want_stock,'stock'),(patched,want_patched,'patched')):
 u=machine(image);push(u,msgs);got=packets(flush(u))
 check(got==want,label+' converter: '+' '.join('%x:%02x'%(p[0],p[1]) for p in got))
 check(u32(u,RING_R)==u32(u,RING_W),label+' flush drains the USB-out ring')
u=machine(patched);push(u,[[0xbe,1,64],[0xf8]],cable=2);got=packets(flush(u))
check(got==[(0x2b,0xbe,1,64),(0x2f,0xf8,0,0)],'patched converter: MidiMsg byte 3 = USB cable (cable 2: 2b/2f)')
check(len(M['patches'])==len({p['offset'] for p in M['patches']}),'patch offsets unique')
p=[x for x in M['patches'] if x['address']==0x08103c3e]
check(len(p)==1,'one patch at the converter type check 0x08103c3e')

u=machine(patched);push(u,[[0xf0,0,0x20],[0x29,2,0xe],[0x10,1,0xf7]],cable=0x82)
w=u32(u,RING_W);u.mem_write(RING+((w-1)&0xff)*5+3,bytes([0xb2]))  # last packet: CIN 7
check(packets(flush(u))==[(0x24,0xf0,0,0x20),(0x24,0x29,2,0xe),(0x27,0x10,1,0xf7)],'patched converter: raw SysEx packets (byte 3 = 0x80 | CIN-4 << 4 | cable)')

# 2. m2 through pc_main: a real MIDI map in the session, values in the stock
#    tag tables, ranges from the stock parameter definitions; USB transfers
#    are completed by hand after the stock SOF handler hands them to Transmit.
MAP=0x3001d2c8;ITEMS=0xc0300000;DEFS=0x3001eb30;VIDPID=0x3000a316
def table_init(u,addr,kind):
 u.mem_write(0x24010000,bytes([kind]));call(u,0x81202b8,addr,0x24010000);w32(u,addr,0x814ae3c)
def taddr(cell):return SESSION+(0xc08+cell*0x30 if cell<12 else 0xe78+(cell-0x0c)*0x10 if cell<=0x12 else 0xe48+(cell-0x14)*0x10)
def m2_machine():
 u=machine();heap=[0xc0100000]
 def alloc(u):
  a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
 stub(u,0x8103128,alloc)
 u.mem_write(0x30000000,bytes(u.mem_read(0x0814d230,0xec)))  # M4 .data (startup copy)
 call(u,0x8134cb8,0)
 for c in range(12):table_init(u,taddr(c),1)
 table_init(u,taddr(0x0c),2)
 for c in range(0x0d,0x13):table_init(u,taddr(c),3)
 table_init(u,taddr(0x14),4);table_init(u,taddr(0x15),5);table_init(u,taddr(0x16),8)
 stub(u,0x8100730,lambda u:None)
 stub(u,0x811b78a,lambda u:None)  # RTOS message post from Transmit
 w32(u,LOADNAME,LOADBUF);u.mem_write(LOADBUF,b'\0')
 w32(u,PHOST+0x540,0x30000098)
 return u
u=m2_machine()
check(u32(u,0x30000098)==0x08145d38 and u32(u,0x300000b4)==0,'USBH MIDI class in RAM at 0x30000098 ("MIDI"), no device: pData 0')
TOAST=u32(u,0x38800fd4)+0x20+640*4+16*640*4+64*8+64*2+8*4
SETTINGS=SESSION+0x737c;call(u,0x8120a52,SETTINGS)
check(call(u,0x8120234,SETTINGS,0x293)==1,'Map CC Sync defaults to USB')
def tset(cell,id,v):call(u,0x8120264,taddr(cell),id,v&0xffffffff)
def defrange(id):
 d=bytes(u.mem_read(u32(u,DEFS)+id*28,28));t=d[2]
 lo=struct.unpack('<i',d[16:20])[0];hi=struct.unpack('<i',d[12:16])[0]-1 if t in(5,6) else struct.unpack('<i',d[20:24])[0]
 return lo,hi
def setmap(items):
 for i,(chan,msg,ctrl,param,cell) in enumerate(items):u.mem_write(ITEMS+20*i,struct.pack('<6H2i',chan,msg,ctrl,param,cell,0,0,0))
 w32(u,MAP,ITEMS);w32(u,MAP+4,len(items))
clock=[10000];sent=[]
def main(dt=20):
 clock[0]+=dt;w32(u,NOW,clock[0]);call(u,PC['pc_main'],0x30000180)
def usb():
 """TIM4 flush + SOF handler (stock) + transfer complete; returns packets sent."""
 call(u,0x8103dc8,PORT)
 if u32(u,SLOT_W)==u32(u,SLOT_R):return []
 call(u,0x810596c,PHOST)
 buf=u32(u,HANDLE+0xc);n=struct.unpack('<H',u.mem_read(HANDLE+0x14,2))[0]
 out=packets(bytes(u.mem_read(buf,n)));u.mem_write(HANDLE+0x18,b'\x00');sent.extend(out);return out
def run(passes=40,dt=20):
 sent.clear()
 for _ in range(passes):main(dt);usb()
 return list(sent)
def attach(vid=0x1ab1,pid=0x0001,ep=1):
 u.mem_write(VIDPID,struct.pack('<HH',vid,pid));u.mem_write(HANDLE,bytes(0x1c));u.mem_write(HANDLE+3,bytes([ep]));w32(u,CLASS_DATA,HANDLE)
def detach():w32(u,CLASS_DATA,0)

# values and ranges
lvl_lo,lvl_hi=defrange(0x4);mode_lo,mode_hi=defrange(0x1a0);mute_lo,mute_hi=defrange(0x160)
check((mode_lo,mode_hi)==(0,5) and (mute_lo,mute_hi)==(0,1) and lvl_hi>lvl_lo,'definition ranges: FX1 mode enum 0..5, mute 0..1, level %d..%d'%(lvl_lo,lvl_hi))
tset(0,0x4,lvl_lo+(lvl_hi-lvl_lo)//2);tset(0x0c,0x160,1);tset(0x14,0x1a0,2);tset(3,0x4,lvl_hi)
want_lvl=round((lvl_hi-lvl_lo)//2*127/(lvl_hi-lvl_lo))
MAPPED=[(14,0xb0,1,0x4,0),(14,0xb0,2,0x160,0x0c),(14,0xb0,3,0x1a0,0x14),(4,0xb0,8,0x4,3),
        (14,0x90,36,0x4,0),(14,0xb0,9,0x4,0x13)]  # note item and unknown cell are skipped
setmap(MAPPED)
GEN=[(0xb,0xbe,1,want_lvl),(0xb,0xbe,2,127),(0xb,0xbe,3,round(2*127/5)),(0xb,0xb4,8,127)]

run(40)
check(sent==[] and u32(u,RING_W)==u32(u,RING_R),'no device: nothing queued')
u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0')
check(run(40)==[],'load complete with no device: nothing sent')
attach()
got=run(40)
check(got==GEN,'attach (generic device) + 500 ms: the 4 mapped CCs with current values on cable 0: '+' '.join('%02x/%d=%d'%(p[1],p[2],p[3]) for p in got))
check(u32(u,TOAST)==0,'no band when the dump completes')
tset(0,0x4,lvl_lo);u.mem_write(LOADBUF,b'Song\0');main()
check(run(10,20)==[],'nothing sent while a load runs')
u.mem_write(LOADBUF,b'\0')
got=run(40)
check(got[0]==(0xb,0xbe,1,0) and got[1:]==GEN[1:],'load complete + 500 ms: dump again with the new track 1 level (0)')
# round trip: the stock M7 formula on the sent CC gives the value back
lv=lvl_lo+(lvl_hi-lvl_lo)*37//100;tset(0,0x4,lv);detach();main();attach();got=run(40)
cc=got[0][3];back=round(lvl_lo+(lvl_hi-lvl_lo)*cc/127)
check(abs(back-lv)<=(lvl_hi-lvl_lo)/254+1,'round trip: CC %d maps back to %d (value %d)'%(cc,back,lv))

# batching: 100 mapped CCs -> batches of 48, one TX slot each, all delivered in order
big=[(14,0xb0,i%128,0x4,i%12) for i in range(100)];setmap(big)
for c in range(12):tset(c,0x4,lvl_lo+(lvl_hi-lvl_lo)*c//11)
detach();main();attach()
slots=[];sent.clear()
for _ in range(60):
 main();out=usb()
 if out:slots.append(len(out))
check(slots==[48,48,4] and [p[2] for p in sent]==[i%128 for i in range(100)],'100 CCs: batches %s, one TX slot each, in map order'%slots)
check(all(p[3]==round((c*(lvl_hi-lvl_lo)//11)*127/(lvl_hi-lvl_lo)) for p,c in zip(sent,[i%12 for i in range(100)])),'each CC carries its own track level')

# stuck transfer: no completion for 1 s -> toast, dump abandoned
setmap(big);detach();main();attach()
for _ in range(45):main(50);call(u,0x8103dc8,PORT);call(u,0x810596c,PHOST)  # never completes
check(u32(u,TOAST)==(8<<8|3),'100-CC dump whose first transfer never completes: toast MAP CC SYNC: USB OUT STUCK after 1 s')
w32(u,TOAST,0)
u.mem_write(HANDLE+0x18,b'\x00');setmap(MAPPED)

# m9: no Launchpad profile: a Launchpad Pro MK3 (1235:0128) gets the generic dump, no SysEx, no cable 2
tset(0,0x4,lvl_lo+(lvl_hi-lvl_lo)//2);tset(3,0x4,lvl_hi)
detach();main();attach(0x1235,0x0128);got=run(40)
check(got==GEN,'Launchpad attach: the same 4 CCs on cable 0 as any device, no setup SysEx, nothing on cable 2')
# device without OUT endpoint
detach();main();attach(ep=0);run(40)
check(sent==[] and u32(u,TOAST)==0,'device without OUT endpoint: nothing sent, no band')
# empty map
setmap([]);detach();main();attach();got=run(40)
check(got==[] and u32(u,TOAST)==0,'empty map: nothing sent, no band')

# 3. m3: DIN Out thru (mo_next replaces the stock input merger 0x08103c62 at
#    both TIM4 poll calls) and DIN Sync (the feedback dump on the two serial
#    outputs, sent by the stock flush through the UART send 0x08104158, stubbed).
def bl_target(image,a):
 h1,h2=struct.unpack_from('<HH',image,a-0x08040000);s=(h1>>10)&1;i1=1-(((h2>>13)&1)^s);i2=1-(((h2>>11)&1)^s)
 value=(s<<24)|(i1<<23)|(i2<<22)|((h1&1023)<<12)|((h2&2047)<<1)
 return a+4+(value-(1<<25) if s else value)
for at in (0x081033c0,0x0810343a):
 check(bl_target(patched,at)==PC['mo_next'] and bl_target(stock,at)==0x8103c62,'poll call %x goes to mo_next (stock: the merger 0x08103c62)'%at)
MSGB=0x24020000;SRCB=0x24020010
def fill(m):
 """One message in every merger source: serial parsers A/B (raw bytes), USB host ring, M7 ring."""
 for par,b in ((PORT,[0x9f,0x3c,0x64]),(PORT+0x210,[0xb1,7,100])):
  w=u32(m,par+0x204)
  for i,x in enumerate(b):m.mem_write(par+((w+i)&0x1ff),bytes([x]))
  w32(m,par+0x204,w+len(b))
 r=u32(m,PORT+0x4b64);m.mem_write(PORT+0x4660+(r&0xff)*5,bytes([0xb2,1,2,0,3]));w32(m,PORT+0x4b64,r+1)
 r=u32(m,PORT+0x506c);m.mem_write(PORT+0x4b68+(r&0xff)*5,bytes([0xf8,0,0,0,1]));w32(m,PORT+0x506c,r+1)
def drain(m,fn):
 out=[]
 for _ in range(8):
  r=call(m,fn,PORT,MSGB,SRCB)
  if not r:break
  out.append((bytes(m.mem_read(MSGB,5)),m.mem_read(SRCB,1)[0]))
 return out
ref=machine(stock);fill(ref);want=drain(ref,0x8103c62)
mine=machine(patched);fill(mine);got=drain(mine,PC['mo_next'])
check(len(want)==4 and got==want,'mo_next returns the stock merger\'s messages, order and src bytes: '+' '.join(m.hex()+'/%d'%s for m,s in got))
SER=[PORT+0x630,PORT+0x630+0x2018]
def q_open(q):
 base=q+(u32(u,q+0x2010)&3)*0x804;return [bytes(u.mem_read(base+i*5,5)) for i in range(u32(u,base+0x500))]
uart=[[],[]];uart_ok=[1]
def uart_send(m):
 i=m.reg_read(UC_ARM_REG_R0)
 if uart_ok[0]:uart[i].extend(m.mem_read(m.reg_read(UC_ARM_REG_R1),m.reg_read(UC_ARM_REG_R2)))
 m.reg_write(UC_ARM_REG_R0,uart_ok[0])
stub(u,0x8104158,uart_send)
def setting(tag,v):call(u,0x8120264,SETTINGS,tag,v);main()
def thru(mode):
 setting(0x292,mode);uart[0].clear();uart[1].clear();fill(u);n=len(drain(u,PC['mo_next']));usb()
 return n,bytes(uart[0]),bytes(uart[1])
detach();main()
check(thru(0)==(4,b'',b''),'DIN Out Default: inputs handled as stock, nothing added to the DIN outputs')
check(thru(1)==(4,bytes([0xb2,1,2]),bytes([0xb2,1,2])),'DIN Out USB Thru: USB host and computer messages on both serial outputs, DIN input not, clock not')
n,a,b=thru(2)
check(n==4 and a==b==bytes([0x9f,0x3c,0x64,0xb1,7,100,0xb2,1,2]),'DIN Out All Thru: every input except clock on both serial outputs, in arrival order, channels kept (ch 16 note stays 9f)')
def rt(bs):
 r=u32(u,PORT+0x506c)
 for x in bs:u.mem_write(PORT+0x4b68+(r&0xff)*5,bytes(x));r+=1
 w32(u,PORT+0x506c,r);uart[0].clear();n=len(drain(u,PC['mo_next']));usb();return n,bytes(uart[0])
check(rt([[0xfa,0,0,0,1],[0xf8,0,0,0,1],[0xfb,0,0,0,1],[0xfc,0,0,0,1],[0xf2,4,0,0,3],[0xfe,0,0,0,1]])==(6,bytes([0xfe])),'thru drops clock, start, continue, stop and song position (still delivered to the bluebox); active sensing passes')
# flow control: 3 closed slots still waiting on output A -> thru skips it, B still gets the message
q=SER[0];w32(u,q+0x2010,u32(u,q+0x2014)+3);before=q_open(SER[0])
fill(u);drain(u,PC['mo_next'])
check(q_open(SER[0])==before and len(q_open(SER[1]))==3,'thru never writes a serial queue with 3 slots waiting (stock indices have no full check)')
w32(u,q+0x2010,u32(u,q+0x2014));u.mem_write(q+(u32(u,q+0x2010)&3)*0x804+0x500,bytes(4))
w=u32(u,SER[1]+0x2010);u.mem_write(SER[1]+(w&3)*0x804+0x500,bytes(4))
setting(0x292,0)
# m5: one input owns clock/transport (MIDI in > computer > USB host); 300 ms of silence hands it over
NOW=0x3001eb3c
def din_in(b):
 w=u32(u,PORT+0x204)
 for i,x in enumerate(b):u.mem_write(PORT+((w+i)&0x1ff),bytes([x]))
 w32(u,PORT+0x204,w+len(b))
def ring_in(ring,widx,m):
 r=u32(u,widx);u.mem_write(ring+(r&0xff)*5,bytes(m));w32(u,widx,r+1)
def host_in(m):ring_in(PORT+0x4660,PORT+0x4b64,m)
def pc_in(m):ring_in(PORT+0x4b68,PORT+0x506c,m)
def got():return [(m[0],s) for m,s in drain(u,PC['mo_next'])]
F8=[0xf8,0,0,0,1]
w32(u,NOW,1000000);host_in(F8)
check(got()==[(0xf8,1)],'clock from the USB host alone is followed (stock)')
din_in([0xf8]);host_in(F8);host_in([0xb0,1,2,0,3]);pc_in(F8);pc_in([0xfa,0,0,0,1])
check(got()==[(0xf8,0),(0xb0,1)],'MIDI in clock takes over at once; USB host and computer clock/start dropped, their CC still delivered')
w32(u,NOW,1000200);host_in(F8)
check(got()==[],'USB host clock still dropped 200 ms after the last MIDI in tick')
w32(u,NOW,1000301);host_in(F8)
check(got()==[(0xf8,1)],'after 300 ms without MIDI in clock the USB host clock is followed')
pc_in(F8)
check(got()==[(0xf8,1)],'computer clock takes over from the USB host at once')
host_in(F8)
check(got()==[],'then the USB host clock is dropped (the computer outranks it)')
din_in([0xfc,0xf8])
check(got()==[(0xfc,0),(0xf8,0)],'MIDI in stop + clock take over from the computer at once')
# m6: Clock Src USB: the computer, then the USB host port, then MIDI in
w32(u,TOAST,0);setting(0x294,1);w32(u,NOW,1002000)
check(u32(u,TOAST)==(9<<8|1),'Clock Src changed to USB: band shows the USB order (T_CLOCK, 1)')
w32(u,TOAST,0);main()
check(u32(u,TOAST)==0,'no band again while Clock Src stays the same')
din_in([0xf8]);host_in(F8)
check(got()==[(0xf8,0),(0xf8,1)],'Clock Src USB: USB host clock takes over from MIDI in at once')
din_in([0xf8,0xfa])
check(got()==[],'Clock Src USB: MIDI in clock and start dropped while the USB host clocks')
pc_in(F8)
check(got()==[(0xf8,1)],'Clock Src USB: the computer outranks the USB host')
w32(u,NOW,1002400);din_in([0xf8])
check(got()==[(0xf8,0)],'Clock Src USB: MIDI in followed once USB has been silent for 300 ms')
setting(0x294,0)
check(u32(u,TOAST)==(9<<8|0),'Clock Src back to MIDI In: band shows the MIDI In order (T_CLOCK, 0)')

# DIN Sync: load dump on the serial outputs
def cc_bytes(pk):return b''.join(bytes(p[1:]) for p in pk)
def din_run(passes=40,dt=20):
 uart[0].clear();uart[1].clear();run(passes,dt);return bytes(uart[0]),bytes(uart[1])
setmap(MAPPED);tset(0,0x4,lvl_lo+(lvl_hi-lvl_lo)//2);tset(3,0x4,lvl_hi)
u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0')
check(din_run()==(b'',b''),'Map CC Sync USB: load complete sends nothing on MIDI out')
setting(0x293,3);w32(u,TOAST,0)
u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0');a,b=din_run()
check(a==b==cc_bytes(GEN) and sent==[] and u32(u,TOAST)==0,'Map CC Sync MIDI Out, no USB device: load complete + 500 ms sends the 4 CCs on both serial outputs, no band')
attach();a,b=din_run()
check(a==b==b'' and sent==[],'Map CC Sync MIDI Out: USB attach sends nothing (attach dump is USB only)')
u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0');a,b=din_run()
check(a==b==cc_bytes(GEN) and sent==[],'Map CC Sync MIDI Out + USB device: load complete dumps on MIDI out only')
setting(0x293,2);detach();main();attach();a,b=din_run()
check(a==b==b'' and sent==GEN,'Map CC Sync USB+MIDI Out: USB attach dumps on USB only')
u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0');a,b=din_run()
check(a==b==cc_bytes(GEN) and sent==GEN,'Map CC Sync USB+MIDI Out: load complete dumps on USB and MIDI out')
setting(0x293,0);detach();main();attach(0x1235,0x0128);a,b=din_run()
u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0');c,d=din_run()
check(a==b==c==d==b'' and sent==[],'Map CC Sync Off: attach and load send nothing')
detach();main();setting(0x293,2)
# 100 CCs: DIN batches of 16 (48 bytes), each once both serial queues are idle
setmap(big);detach();main()
uart[0].clear();uart[1].clear();sizes=[]
u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0')
for _ in range(60):
 k=len(uart[0]);main();usb()
 if len(uart[0])>k:sizes.append(len(uart[0])-k)
check(sizes==[48]*6+[12] and bytes(uart[0])==bytes(uart[1]) and list(uart[0][1::3])==[i%128 for i in range(100)],'100 CCs on DIN: sends %s bytes, in map order'%sizes)
# UART never accepts: DIN dump abandoned after 1 s
uart_ok[0]=0;u.mem_write(LOADBUF,b'Song\0');main();u.mem_write(LOADBUF,b'\0')
for _ in range(45):main(50);usb()
check(u32(u,TOAST)==(8<<8|5),'MIDI output that never sends: toast MAP CC SYNC: MIDI OUT STUCK after 1 s')
uart_ok[0]=1
for _ in range(10):usb()
setmap(MAPPED);setting(0x293,1)
print(H.checks,"checks passed")
