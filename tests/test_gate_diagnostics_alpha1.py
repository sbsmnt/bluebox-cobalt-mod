"""Actual M7 telemetry, cross-core snapshot decoding and bounded footer draw.
The readback is created by actual native-queue Gate processing, then only that
published cache line is transported into a separate M4 emulator.
"""
from gate_ui_fixture import *
D=0x38800680;U=0x38800a00;TEXT=0x24062000
ch=2;setup(0,ch,-90000)
amp=10**(-56/20);right=10**(-61/20)
a,_,_=gsetup(ch=ch)
gblock(a,[amp]*32,[right]*32,[(0x1ae,-90000)],ch=ch)
e=D+64*ch;words=struct.unpack('<IIi4fI',a.mem_read(e,32))
check(words[0]==0x47444131 and words[1]==2 and words[2]==-90000 and words[7]==1,'M7 publishes accepted native-queue threshold, sequence and received-event count')
check(abs(words[3]-amp)<1e-9 and abs(words[4]-right)<1e-9 and abs(words[5]-amp)<1e-9 and words[6]==1.,'M7 publishes actual pre-gate stereo peaks, detector envelope and applied gain')
check(u32(a,0xe000ef68)==e,'M7 cleans the aligned published cache line for M4 visibility')
for i in range(74):gblock(a,[amp]*32,[right]*32,ch=ch)
check(u32(a,e+4)==2,'no periodic publish before 75 callbacks')
gblock(a,[amp]*32,[right]*32,ch=ch)
check(u32(a,e+4)==4,'periodic snapshot refreshes without another UI edit')
u.mem_write(e,bytes(a.mem_read(e,32)))
# Simulate the visible widget remaining stale while the saved setting advances.
k=P+0xbe0+ch*0xd98+0x8f0
call(u,0x8127b84,k,0x1ae,(-80000)&0xffffffff,1)
def lines():
 call(u,S['gate_diag_lines'],TEXT)
 return [bytes(u.mem_read(TEXT+i*54,54)).split(b'\0')[0].decode() for i in range(4)]
rows=lines();print('DIAGNOSTIC ROWS',rows)
check('U-80.0 S-90.0' in rows[1],'diagnostic does not confuse displayed widget with saved threshold')
check('T-90.0 RX1 Q2 G100%' in rows[2],'audio row decodes independently produced M7 threshold/event/sequence/gain')
check('L-56.0 R-61.0 ENV-56.0' in rows[3],'detector input formatting matches known stereo amplitudes')
# Independent UI, model, DSP differences remain visible rather than normalized.
call(u,0x8120264,APP+0xc08+ch*0x30,0x1ae,(-70000)&0xffffffff)
rows=lines();check('U-80.0 S-70.0' in rows[1] and 'T-90.0' in rows[2],'widget, saved setting and audio-processor threshold can show three different values')
seq=u32(u,e+4);w32(u,e+4,seq|1)
check('WAIT' in lines()[2],'in-progress odd snapshot is rejected')
w32(u,e+4,seq);w32(u,e,0)
check('WAIT' in lines()[2],'absent/wrong audio report is not shown as a valid threshold')
u.mem_write(e,bytes(a.mem_read(e,32)))
# Report confirms the actual gain reaches zero at a higher threshold.
gblock(a,[amp]*32,[right]*32,[(0x1ae,-48000)],ch=ch)
for i in range(4100):gblock(a,[amp]*32,[right]*32,ch=ch)
r=struct.unpack('<IIi4fI',a.mem_read(e,32))
check(r[2]==-48000 and r[6]==0. and r[7]==2,'M7 reports actual closure at -48dB after receiving -56dB noise')
u.mem_write(e,bytes(a.mem_read(e,32)));rows=lines()
check('T-48.0 RX2' in rows[2] and 'G0%' in rows[2],'footer shows the closed DSP gain rather than assuming closure from threshold')
# On Off, do not invent a detector measurement that the bypass never scanned.
gblock(a,[amp]*32,[right]*32,[(0x1ae,-96000)],ch=ch)
for i in range(150):gblock(a,[amp]*32,[right]*32,ch=ch)
u.mem_write(e,bytes(a.mem_read(e,32)))
check('TOFF' in lines()[2] and lines()[3]=='IN -- (GATE OFF)','Off readback explicitly distinguishes unavailable input telemetry')
# UI event counters and requested/actual parameter IDs.
setup(0,0,-60000);clock[0]+=1000;w32(u,0x3001eb3c,clock[0])
u.mem_write(TMP,struct.pack('<6I',0x32,0x8148664,0,0,160,0));call(u,S['gate_mix_event'],P,TMP)
check(u32(u,U+4)==1 and u32(u,U+8)==1 and u32(u,U+12)==1,'MIXER global input, Gate branch and local Gate commit counted separately')
u.mem_write(TMP,struct.pack('<6I',0x6e,0x8148664,0,0x1ae,(-50000)&0xffffffff,0));call(u,S['gate_track_local'],T,TMP)
check(u32(u,U+16)==1,'TRACK local Gate edits counted independently')
for db in (-100,-80,-61,-56,-48,-20,0,6):
 u.reg_write(UC_ARM_REG_S0,struct.unpack('<I',struct.pack('<f',10**(db/20)))[0]);raw=call(u,S['gate_diag_db']);v=struct.unpack('<i',struct.pack('<I',raw))[0]
 check(abs(v-db*1000)<10,'diagnostic-only dB formatting error under 0.01dB at '+str(db))
# Footer writes are strictly bounded in every supported format.
FB=0xc0100000
for fmt,bpp in ((1,4),(2,3),(3,2),(4,2)):
 u.mem_write(FB,bytes(320*240*bpp));call(u,S['gate_diag_draw'],FB,320,fmt,bpp)
 data=bytes(u.mem_read(FB,320*240*bpp))
 check(not any(data[:208*320*bpp]) and any(data[208*320*bpp:]),'diagnostic pixels stay in bottom 32 rows, format '+str(fmt))
# Save an internal render for visual inspection (not required to rerun tests).
try:
 from PIL import Image
 u.mem_write(FB,bytes(320*240*4));call(u,S['gate_diag_draw'],FB,320,1,4)
 Image.frombytes('RGBA',(320,240),bytes(u.mem_read(FB,320*240*4)),'raw','BGRA').save(str(ROOT/'tests/diagnostic-preview.png'))
except ImportError:pass
print(str(H.checks)+' alpha.1 diagnostic readback, counter, calibration-format and footer bounds checks passed; physical trace is still needed.')
