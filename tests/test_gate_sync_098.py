from gate_ui_fixture import *
for quad in (0,1):
 for ch in range(12):
  for page in ('MIXER','TRACK'):
   setup(quad,ch,-60000)
   k=P+0xbe0+ch*0xd98+0x8f0 if page=='MIXER' else T+0xde48
   # Real numeric knob encoder, through native child->column->panel handlers.
   clock[0]+=1000;w32(u,0x3001eb3c,clock[0]);call(u,0x8127d50,k,160)
   expected=val(k)
   check(expected>-60000 and commands==[(ch,0x1ae,expected&0xffffffff)],f'{page} encoder makes one correct audio command {(quad,ch)}')
   check(call(u,0x8120234,APP+0xc08+ch*0x30,0x1ae)==(expected&0xffffffff),f'{page} encoder updates shared native setting {(quad,ch)}')
   notices=flush()
   check(len(notices)==1 and struct.unpack_from('<H',notices[0])[0]==0x66 and struct.unpack_from('<H',notices[0],8)[0]==ch,f'{page} emits one native track refresh {(quad,ch)}')
   check(val(P+0xbe0+ch*0xd98+0x8f0)==expected and val(T+0xde48)==expected,f'both pages show same threshold after {page} edit {(quad,ch)}')
   check(not any(struct.unpack_from('<H',e)[0]==0x6e and struct.unpack_from('<I',e,12)[0]==0x1ae for e in parents),f'{page} Gate edit consumed before parent; no duplicate {(quad,ch)}')
   # Native relative touch-drag event 0x167, not a synthesized threshold edit.
   commands.clear();call(u,0x8127d38,k);u.mem_write(TMP,struct.pack('<6I',0x167,0x8148664,0,50,0,0))
   call(u,0x8127e98,k,TMP);touch=val(k)
   check(touch!=expected and commands==[(ch,0x1ae,touch&0xffffffff)],f'{page} touch drag reaches same model/audio path {(quad,ch)}')
   flush();check(val(P+0xbe0+ch*0xd98+0x8f0)==touch and val(T+0xde48)==touch,f'both pages sync after {page} touch drag {(quad,ch)}')
# Actual hardware encoder notification into the full native MIXER handler.
setup(0,0,-60000)
clock[0]+=1000;w32(u,0x3001eb3c,clock[0])
u.mem_write(TMP,struct.pack('<6I',0x32,0x8148664,0,0,160,0))
call(u,S['gate_mix_event'],P,TMP)
check(len(commands)==1 and commands[0][0:2]==(0,0x1ae),'MIXER physical encoder notification reaches shared Gate commit')
flush();check(val(P+0xbe0+0x8f0)==val(T+0xde48)>-60000,'physical MIXER encoder edit refreshes TRACK')
# Direct native mixer fader event forms and Main guard use the LOCAL handler.
for kind in (0x3c,0x3d,0x6e):
 setup(0,4,-60000);commands.clear()
 e=(kind,0x8148664,4,0x1ae,(-70000)&0xffffffff,0) if kind==0x6e else (kind,0x8148664,4,(-70000)&0xffffffff,0x1ae,0)
 u.mem_write(TMP,struct.pack('<6I',*e));call(u,u32(u,u32(u,P)+0x30),P,TMP);flush()
 check(commands==[(4,0x1ae,(-70000)&0xffffffff)] and val(T+0xde48)==-70000,f'MIXER local event {kind:x} commits millidecibels and syncs TRACK')
 commands.clear();e=list(e);e[2]=12;u.mem_write(TMP,struct.pack('<6I',*e));call(u,u32(u,u32(u,P)+0x30),P,TMP)
 check(not commands,f'Main rejected at actual local handler {kind:x}')
# Unrelated native edits still bubble; neither Gate handler intercepts them.
for panel in (P,T):
 parents.clear();commands.clear();u.mem_write(TMP,struct.pack('<6I',0x6e,0x8148664,2,4,9000,0))
 call(u,u32(u,u32(u,panel)+0x30),panel,TMP)
 check(not commands and call(u,u32(u,u32(u,panel)+0x30),panel,TMP)==1,'non-Gate parameter retains native parent-routing result '+hex(panel))
# Capture commands produced by the actual MIXER local handler and feed the M7
# native parameter queue. No direct threshold/state writes in the audio fixture.
setup(0,4,-60000)
for threshold,closed in ((-70000,True),(-96000,False)):
 commands.clear();u.mem_write(TMP,struct.pack('<6I',0x6e,0x8148664,4,0x1ae,threshold&0xffffffff,0))
 call(u,u32(u,u32(u,P)+0x30),P,TMP);flush()
 ch,pid,value=commands[0];value=struct.unpack('<i',struct.pack('<I',value))[0]
 a,_,_=gsetup(ch=ch);pcm=None
 for i in range(200):pcm,_=gblock(a,[.0001]*32,[.0001]*32,[(pid,value)] if i==0 else [],ch=ch)
 samples=struct.unpack('<64f',pcm)
 check((max(map(abs,samples))<1e-10) if closed else all(abs(x-.0001)<1e-10 for x in samples),'MIXER-generated command '+('closes actual M7 audio' if closed else 'Off restores actual M7 unity audio'))
# Gate control scale and the quiet-signal report: verify actual native drawing
# fraction, initial Off travel, stored dB and native queued audio together.
for page in ('MIXER','TRACK'):
 setup(0,0,-96000)
 k=P+0xbe0+0x8f0 if page=='MIXER' else T+0xde48
 clock[0]+=1000;w32(u,0x3001eb3c,clock[0]);call(u,0x8127d50,k,80)
 check(val(k)==-95900 and commands==[(0,0x1ae,(-95900)&0xffffffff)],page+' enables at first native 0.1dB step after Off')
 flush()
 for value in (-96000,-95000,-85000,-80000,-70000,-54000,-30000,-12000):
  setup(0,0,value)
  call(u,S['dly_knob_fraction'],k)
  fraction=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]
  check(abs(fraction-(value+96000)/84000)<1e-5,page+' native knob scale correct at '+str(value/1000)+' dB')
 for threshold,source_db in ((-85000,-95),(-70000,-80)):
  setup(0,0,threshold)
  call(u,S['dly_knob_fraction'],k)
  fraction=struct.unpack('<f',struct.pack('<I',u.reg_read(UC_ARM_REG_S0)))[0]
  # Commit the value displayed by this real knob, then transport its command.
  u.mem_write(TMP,struct.pack('<6I',0x6e,0x8148664,0,0x1ae,val(k)&0xffffffff,0))
  call(u,u32(u,u32(u,P if page=='MIXER' else T)+0x30),P if page=='MIXER' else T,TMP)
  ch,pid,v=commands[-1];v=struct.unpack('<i',struct.pack('<I',v))[0]
  a,_,_=gsetup(ch=ch);amp=10**(source_db/20)
  for i in range(200):pcm,_=gblock(a,[amp]*32,[amp]*32,[(pid,v)] if i==0 else [],ch=ch)
  check(fraction<.5 and max(map(abs,struct.unpack('<64f',pcm)))<1e-10,page+' quiet '+str(source_db)+'dB signal closes below halfway at '+str(threshold/1000)+'dB')
# Profiler/master stay identical. The delay module now contains the separately
# tested Shoegaze dispatch/allocation; the production Gate source stays exact.
expected_dsp={
 'cm7.bin':'04d2cd048283d02db2bb90d6f29c6b729a38fa8e3032dc6a5e5dd6dcef686579'}

for name,digest in expected_dsp.items():
 check(hashlib.sha256((ROOT/'build'/name).read_bytes()).hexdigest()==digest,'unchanged v0.9.7 DSP payload '+name)
# exp.6 rewrites the gate's peak scan and gain loop for speed with identical
# arithmetic; test_exp6_gate_granular.py checks PCM/state bit-identity.
print(str(H.checks)+' v1.0.0-alpha.4 native Gate shared-commit/refresh/audio checks passed; hardware still required.')
