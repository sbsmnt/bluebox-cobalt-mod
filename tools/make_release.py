"""Build the standalone patcher from compiled code; no stock firmware is shipped."""
from html import escape
import base64,hashlib,json,pathlib,struct,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
STOCK_HASH='38a7944c9eed15dac18e2dac89e7574c9112f60b20324d77a4c26ba522607126'
def branch(at,target):
    off=(target&~1)-(at+4)
    if off%2 or not -(1<<24)<=off<(1<<24):raise ValueError('branch range')
    s=(off>>24)&1;i1=(off>>23)&1;i2=(off>>22)&1
    return struct.pack('<HH',0xf000|(s<<10)|((off>>12)&1023),0xd000|(((1-i1)^s)<<13)|(((1-i2)^s)<<11)|((off>>1)&2047))
def symbols(core):
    return {f[2]:int(f[0],16) for line in (ROOT/'build'/f'cm{core}.symbols').read_text().splitlines() if len(f:=line.split())==3}
if len(sys.argv)!=2:raise SystemExit('Usage: ./build.sh /path/to/stock/BLUEBOX.BIN')
stock=pathlib.Path(sys.argv[1]).read_bytes()
if hashlib.sha256(stock).hexdigest()!=STOCK_HASH:raise SystemExit('Wrong stock firmware: desktop 1.5.2 exact image required')
patched=bytearray(stock);patches=[]
def add(name,address,old,new):
    offset=address-0x08040000
    if len(old)!=len(new) or stock[offset:offset+len(old)]!=old:raise ValueError('Unexpected original bytes: '+name)
    for p in patches:
        if offset<p['offset']+len(base64.b64decode(p['new'])) and p['offset']<offset+len(new):raise ValueError('overlap')
    patched[offset:offset+len(old)]=new
    patches.append(dict(name=name,address=address,offset=offset,old=base64.b64encode(old).decode(),new=base64.b64encode(new).decode()))
for core,address in [(7,0x080b0000),(4,0x080b8000)]:
    code=(ROOT/'build'/f'cm{core}.bin').read_bytes()
    if len(code)>0x8000:raise ValueError('code cave too large')
    add(f'M{core} additional code',address,bytes(len(code)),code)
add('audio profiler exit call',0x0804c50e,branch(0x0804c50e,0x0804bbf8),branch(0x0804c50e,symbols(7)['cpu_prof_end']))
add('clock label width 140 to 128 pixels',0x08139790,bytes.fromhex('8c23'),bytes.fromhex('8023'))
gr_code=(ROOT/'build'/'gr7.bin').read_bytes()
if len(gr_code)>0x8000:raise ValueError('compressor code cave too large')
gr_symbols={f[2]:int(f[0],16) for line in (ROOT/'build'/'gr7.symbols').read_text().splitlines() if len(f:=line.split())==3}
add('M7 compressor telemetry code',0x080a0000,bytes(len(gr_code)),gr_code)
add('compressor process pointer',0x0806a24c,struct.pack('<I',0x08042649),struct.pack('<I',gr_symbols['gr_process']|1))
add('settings redraw check call',0x0813408e,branch(0x0813408e,0x08139586),branch(0x0813408e,symbols(4)['gate_ui_tick']))
# Theme .data is copied from flash 0x0814d230 to SRAM 0x30000000 at startup.
# The stock palette getter 0x08101064 indexes it by r1. Slot 4 is observed in
# the selected-row draw path; slot 32 is the second pink theme accent.
for slot in (4,32):
    add(f'purple UI palette slot {slot}',0x0814d230+4*slot,
        struct.pack('<I',0xffff6af3),struct.pack('<I',0xff9455fa))
PC={f[2]:int(f[0],16) for line in (ROOT/'build'/'pcm4.symbols').read_text().splitlines() if len(f:=line.split())==3}
add('parameter definitions final call',0x08135fec,branch(0x08135fec,0x08134c8c),branch(0x08135fec,PC['pc_defs_hook']))
add('master defaults final add',0x08120848,branch(0x08120848,0x0812028e),branch(0x08120848,symbols(4)['sat_default']))
add('master settings saturation row',0x0814aea2,bytes(8),struct.pack('<4H',0x43,0x1b6,0x1b7,0x1b8))
# Extended delay: independently mapped desktop 1.5.2 locations.
dcode=(ROOT/'build'/'delay7.bin').read_bytes()
if len(dcode)>0x8000:raise ValueError('delay code cave too large')
ds={f[2]:int(f[0],16) for line in (ROOT/'build'/'delay7.symbols').read_text().splitlines() if len(f:=line.split())==3}
add('M7 granular and extended delay code',0x080a8000,bytes(len(dcode)),dcode)
add('delay graph constructor call',0x08050f1a,branch(0x08050f1a,0x08052478),branch(0x08050f1a,ds['dly_construct']))
add('delay process pointer',0x0806b014,struct.pack('<I',0x08053179),struct.pack('<I',ds['dly_process']|1))
add('reverb send process pointer',0x0806a33c,struct.pack('<I',0x080489a1),struct.pack('<I',ds['delay_reverb_process']|1))
add('FX1 native input gate capture',0x08052c8c,branch(0x08052c8c,0x0804df54),branch(0x08052c8c,ds['dly_input_gate']))
for at in [0x08052bbc,0x08052bdc,0x08052cd0,0x08052d00,0x08052d4e,0x08052d68,0x08052e5e,0x08052ea4,0x08052f34]:
    add('delay line read '+hex(at),at,branch(at,0x080593ec),branch(at,ds['dly_read']))
# Native kind-4 case bridge: use the native add helper for all 25 defaults.
at=0x08120872
code=bytes.fromhex('2046')+branch(at+2,symbols(4)['dly_defaults'])
tail=bytearray(branch(at+6,0x08120538));tail[3]&=0xbf
code+=tail
code+=bytes.fromhex('00bf')*((98-len(code))//2)
add('delay native parameter list',at,bytes.fromhex('4ff4c87233212046fff708fd4ff4c87239212046fff702fd78220e212046fff7fdfc4ff47a72cb212046fff7f7fc012235212046fff7f2fc0122ca212046fff7edfc012236212046fff7e8fc002237212046fff7e3fc062234212046fff7defc31e6'),code)
add('FX delay mode parameter list',0x0812ac4a,branch(0x0812ac4a,0x08121cf4),branch(0x0812ac4a,symbols(4)['dly_parameters']))
for at in [0x0812af10,0x0812af4e]:add('delay encoder paging '+hex(at),at,bytes.fromhex('02d0'),bytes.fromhex('06d0'))
add('FX compact panel 13 widgets',0x0812ae5e,bytes.fromhex('b9f1090f'),bytes.fromhex('b9f10c0f'))
for at in [0x0812b006,0x0812b4d4,0x0812b51a]:
    add('FX populate '+hex(at),at,branch(at,0x0812abe0),branch(at,symbols(4)['dly_populate']))
add('FX native B selector action',0x08123a42,branch(0x08123a42,0x08122508),branch(0x08123a42,symbols(4)['dly_choose_action']))
add('FX shared selector list fill',0x08139b74,branch(0x08139b74,0x08139aec),branch(0x08139b74,symbols(4)['dly_selector_fill']))
add('FX1 selector row to saved mode',0x0814c62c,struct.pack('<I',0x08139aa9),struct.pack('<I',symbols(4)['dly_selector_event']|1))
add('FX encoder input',0x0812b4aa,branch(0x0812b4aa,0x08127d50),branch(0x0812b4aa,symbols(4)['dly_turn']))
add('FX timed value update',0x0814b594,struct.pack('<I',0x0812afd9),struct.pack('<I',symbols(4)['dly_tick']|1))
add('FX mode change event handler',0x0814b5c4,struct.pack('<I',0x0812b461),struct.pack('<I',symbols(4)['dly_event']|1))
add('FX layout wrapper',0x0812b4c4,branch(0x0812b4c4,0x0812b090),branch(0x0812b4c4,symbols(4)['dly_layout']))
# Native A switch: actions14/15 are FX1; use the existing cycle event164 arm.
add('FX1 A group-cycle dispatch',0x081238d2,bytes.fromhex('1c1c'),bytes.fromhex('2d2d'))
add('FX1 active effect title',0x0812ac38,branch(0x0812ac38,0x0813fe4a),branch(0x0812ac38,symbols(4)['dly_title']))
add('FX2 Shoegaze title',0x0812ac1c,branch(0x0812ac1c,0x0813fe4a),branch(0x0812ac1c,symbols(4)['sg_title']))
add('FX2 Shoegaze defaults',0x0812092c,branch(0x0812092c,0x0812028e),branch(0x0812092c,symbols(4)['sg_defaults_ui']))
add('Density drawing fraction',0x08128224,branch(0x08128224,0x08127308),branch(0x08128224,symbols(4)['dly_knob_fraction']))
add('Tools mod version label',0x081314a0,branch(0x081314a0,0x0813820c),branch(0x081314a0,symbols(4)['dly_tools_version']))
add('shared encoder acceleration ceiling 2x',0x0813b86e,bytes.fromhex('0628a8bf0620'),bytes.fromhex('0228a8bf0220'))
for slot,rgb in [(1,0x56565a),(2,0x141414),(3,0xfcfcfc),(5,0x09d7f5),(31,0x09d7f5),(10,0x09d7f5),(27,0x09d7f5)]:
 word=struct.pack('<I',0xff000000|rgb)
 add('theme palette slot '+str(slot),0x0814d230+4*slot,word,word)
for at in [0x081010de,0x08101104]:
 add('knob extended conversion CLUT '+hex(at),at,branch(at,0x0811085c),branch(at,symbols(4)['theme_knob_clut']))
for at in [0x08128128,0x081281b6]:
 add('knob outline color '+hex(at),at,branch(at,0x0813723c),branch(at,symbols(4)['theme_knob_pixel']))
add('knob pointer color',0x081282a6,branch(0x081282a6,0x081364da),branch(0x081282a6,symbols(4)['theme_knob_line']))
# Per-track adaptive noise gate: original desktop EQ class, bypass Main.
ngcode=(ROOT/'build'/'gate7.bin').read_bytes()
if len(ngcode)>0x8000:raise ValueError('gate code cave too large')
ngs={f[2]:int(f[0],16) for line in (ROOT/'build'/'gate7.symbols').read_text().splitlines() if len(f:=line.split())==3}
add('M7 adaptive gate code',0x080c0000,bytes(len(ngcode)),ngcode)
add('channel EQ gate process pointer',0x0806a2b4,struct.pack('<I',0x08043b91),struct.pack('<I',ngs['gate_process']|1))
# exp.7: MIXER Gate view meters show the detector input on the Gate fader's
# scale (restores the v0.9.7 publication overrides, with the scale mapping).
for at,old,new in [(0x0804f8be,0x0804bab0,'gate_meter_mono_shim'),(0x0804f9c6,0x0804bacc,'gate_meter_stereo_shim'),(0x0804f9e0,0x0804bacc,'gate_meter_stereo_shim')]:
 add('Gate input meter '+hex(at),at,branch(at,old),branch(at,ngs[new]))
for at in [0x0804304a,0x08050c96,0x08051062]:
 add('gate EQ owner constructor '+hex(at),at,branch(at,0x08042e60),branch(at,ngs['gate_construct']))
add('TRACK Gate fine encoder',0x08133248,branch(0x08133248,0x08127d50),branch(0x08133248,symbols(4)['gate_turn']))
add('Gate knob Off formatting',0x08127a08,branch(0x08127a08,0x081403c4),branch(0x08127a08,symbols(4)['gate_db_shim']))
add('channel defaults append gate',0x08120534,branch(0x08120534,0x0812028e),branch(0x08120534,symbols(4)['gate_default']))
add('MIXER construct sixth Gate button',0x081349dc,branch(0x081349dc,0x0812dadc),branch(0x081349dc,symbols(4)['gate_mix_construct']))
add('MIXER ignore Main gate edits',0x0814b7d8,struct.pack('<I',0x0812e755),struct.pack('<I',symbols(4)['gate_mix_event']|1))
add('MIXER shared Gate edit commit',0x0814b7d4,struct.pack('<I',0x0812e469),struct.pack('<I',symbols(4)['gate_mix_local']|1))
add('TRACK shared Gate edit commit',0x0814bb6c,struct.pack('<I',0x08132e39),struct.pack('<I',symbols(4)['gate_track_local']|1))
add('MIXER sends Gate selector',0x0814b7f4,bytes(2),struct.pack('<H',0x1ae))
for at in [0x0812e4c0,0x0812e4d0]:
 add('MIXER seventh button range '+hex(at),at,bytes.fromhex('0528' if at==0x0812e4c0 else '0529'),bytes.fromhex('0628' if at==0x0812e4c0 else '0629'))
for at in [0x0812e4d8,0x0812e6a4]:
 add('MIXER sends selection '+hex(at),at,branch(at,0x0812e2e0),branch(at,symbols(4)['gate_mix_select']))
for at in [0x081331c0,0x081331dc,0x081331fe]:
 add('Track Gate knob binding '+hex(at),at,branch(at,0x081328f8),branch(at,symbols(4)['gate_track_bind']))
# Gate uses native default/Vol metering. The earlier detector-input report
# overrides are not installed; the compiled Gate processing stays unchanged.
for at in [0x0812e830,0x08134586]:
 add('MIXER Gate label '+hex(at),at,branch(at,0x0812e54c),branch(at,symbols(4)['gate_mix_setup']))
for at in [0x0812e3e0,0x0812e77e,0x0812e7ea,0x0812e84e]:
 add('MIXER Gate fader '+hex(at),at,branch(at,0x0812dde8),branch(at,symbols(4)['gate_column']))
# Overdub/recording throughput fix (from the hardware-tested C16 experiment,
# without its diagnostics). Three native PCM24 recording call sites and two
# playback call sites go to the word/burst wrappers; the stereo-left call
# enters the fused bridge, which resumes after the (now unreached) right call.
S4=symbols(4)
for at,symbol in ((0x0810aa28,'record_pcm24_fast'),(0x0810aa50,'record_pcm24_stereo_bridge'),(0x0810aa6c,'record_pcm24_fast')):
 add('record PCM24 conversion '+hex(at),at,branch(at,0x08108df4),branch(at,S4[symbol]))
for at,target,symbol in ((0x0810aece,0x08108c8c,'record_pcm24_read_mono'),(0x0810af54,0x08108cd4,'record_pcm24_read_stereo')):
 add('playback PCM24 conversion '+hex(at),at,branch(at,target),branch(at,S4[symbol]))
# The stock PCM24 recording converter and mono/stereo playback decoder bodies
# (only reached from the call sites above) are refilled in place with the
# burst loops, NOP-padded: ART-cached page 0x081, no new flash or cache change.
if hashlib.sha256(stock[0xcd5ba:0xcd5d8]).hexdigest()!='e7d4196bf676d3f78ffa07d9f5dfba43cb293baead450a174b18a2b918d66ebb':raise ValueError('Unexpected native ART configuration')
for name,at,size,section,symbol,digest in (
  ('in-place bank2 PCM24 burst recording loop',0x08108df4,0xa4,'pcmhot','record_pcm24_burst','2536f7c45a4a9ad8bd7d06474ce395ae1276e7a60b03a901892e823bdcb1a335'),
  ('in-place bank2 PCM24 burst mono playback loop',0x08108c8c,0x48,'pcmdm','record_pcm24_unburst_mono','ea2a4a0af62722de7ac31141d396c120dc22dd3662c01e06af59f09e49377848'),
  ('in-place bank2 PCM24 burst stereo playback loop',0x08108cd4,0x70,'pcmds','record_pcm24_unburst_stereo','bcddf9b24737686374272ce20ffd93c98f500c14499424e4ccf55f3228e26019')):
 o=at-0x08040000;old=stock[o:o+size];hot=(ROOT/'build'/f'{section}.bin').read_bytes()
 if hashlib.sha256(old).hexdigest()!=digest:raise ValueError('Unexpected native PCM24 body '+hex(at))
 if S4[symbol]!=at or not hot or len(hot)>size or len(hot)&1:raise ValueError('Burst loop placement/size mismatch '+symbol)
 if (at>>20)!=0x081 or ((at+size-1)>>20)!=0x081:raise ValueError('Burst loop outside ART page')
 add(name,at,old,hot+b'\x00\xbf'*((size-len(hot))//2))
# Scenes experiment (bluebox-scenes, ../SCENES.md), on the PC-map machinery
# (../../bluebox-pc-map/INVESTIGATION.md): Program Change recalls a scene,
# Bank Select (CC 0) loads the project with that Bank # number.
if len((ROOT/'build'/'cm4.bin').read_bytes())>0x4000:raise ValueError('Cobalt M4 code reaches the PC map payload')
pc_code=(ROOT/'build'/'pcm4.bin').read_bytes()
if len(pc_code)>0x4000:raise ValueError('PC map code cave too large')
add('M4 PC map code',0x080bc000,bytes(len(pc_code)),pc_code)
def bw(at,target):
 code=bytearray(branch(at,target));code[3]&=0xbf;return bytes(code)
add('PC map MIDI program change capture',0x0810341a,bytes.fromhex('c028cdd1b2e7'),bw(0x0810341a,PC['pc_midi_isr'])+bytes.fromhex('00bf'))
add('PC map main-loop dispatch',0x081269d8,branch(0x081269d8,0x08100730),branch(0x081269d8,PC['pc_main']))
add('PC map settings defaults',0x08120aae,branch(0x08120aae,0x0812028e),branch(0x08120aae,PC['pc_settings_defaults']))
add('PC map settings capacity 68 to 144',0x08120a56,bytes.fromhex('4421'),bytes.fromhex('9021'))
for at,old,new in ((0x08134cce,'43f63410','44f64c00'),(0x08134ce0,'43f63412','44f64e02')):  # 0x295 records (Bank # slots + scene/MIDI tags + DIN Out/Sync + Clock Src)
 add('PC map name table size '+hex(at),at,bytes.fromhex(old),bytes.fromhex(new))
add('PC map name table bound 0x08134c06',0x08134c06,bytes.fromhex('40f20a23'),bytes.fromhex('40f29423'))
for at in (0x0813600c,0x0813603a,0x0813606c,0x0813609a,0x081360ca,0x081360fa,0x0813613c):
 add('PC map name table bound '+hex(at),at,bytes.fromhex('40f20a22'),bytes.fromhex('40f29422'))
add('PC map name lookup end',0x08136180,bytes.fromhex('04f564553435'),bytes.fromhex('04f590454c35'))  # +0x484c
add('PC map rename follows name',0x0813e822,branch(0x0813e822,0x08122468),branch(0x0813e822,PC['pc_rename']))
add('PC map delete clears slot',0x0813e5c0,branch(0x0813e5c0,0x08122f50),branch(0x0813e5c0,PC['pc_delete']))
add('PC map Load page after PC',0x08125e2a,branch(0x08125e2a,0x08122508),branch(0x08125e2a,PC['pc_after_load']))
add('PC map Tools hides PC slots',0x08121e82,branch(0x08121e82,0x08138546),branch(0x08121e82,PC['pc_tools_append']))
add('PC map MIDI realtime timestamp',0x081033e0,bytes.fromhex('0023cdf81130'),bw(0x081033e0,PC['pc_midi_realtime'])+bytes.fromhex('00bf'))
add('Scenes Bank Select capture',0x08103378,bytes.fromhex('b02804d0'),bw(0x08103378,PC['pc_midi_cc']))
add('MIDI out: USB converter passes channel messages',0x08103c3e,bytes.fromhex('9028b9d0'),bw(0x08103c3e,PC['mo_convert']))
for at in (0x081033c0,0x0810343a):  # input merger 0x08103c62: DIN Out thru modes
 add('MIDI out: DIN thru input merger '+hex(at),at,branch(at,0x08103c62),branch(at,PC['mo_next']))
add('Scenes project writer',0x0812109e,branch(0x0812109e,0x08106c92),branch(0x0812109e,PC['sc_write_hook']))
def beqw(at,target):
 off=(target&~1)-(at+4)
 if off%2 or not -(1<<20)<=off<(1<<20):raise ValueError('conditional branch range')
 s=(off>>20)&1;j2=(off>>19)&1;j1=(off>>18)&1
 return struct.pack('<HH',0xf000|(s<<10)|(0<<6)|((off>>12)&63),0x8000|(j1<<13)|(j2<<11)|((off>>1)&2047))
if beqw(0x0813e16e,0x0813e390)!=bytes.fromhex('00f00f81'):raise ValueError('beq.w encoder')
add('Scenes A button (scene page open / leave)',0x08123902,branch(0x08123902,0x08123818),branch(0x08123902,PC['pc_button_a']))
add('B on Tools (MIDI row) opens the MIDI page, B there goes back',0x081239e0,branch(0x081239e0,0x08123818),branch(0x081239e0,PC['pc_button_b']))
add('PROJ list marks the loaded project',0x0813dfba,branch(0x0813dfba,0x0810204e),branch(0x0813dfba,PC['pc_list_row']))
add('New project clears the scenes',0x0813e8a2,branch(0x0813e8a2,0x08122e3c),branch(0x0813e8a2,PC['pc_new_project']))
add('First project (none on the card) clears the scenes',0x08125cfc,branch(0x08125cfc,0x08122e3c),branch(0x08125cfc,PC['pc_new_project']))
add('Scene page Select button',0x0814ca40,struct.pack('<I',0x0813cfb9),struct.pack('<I',PC['pc_edit_event']|1))
add('PROJ Save assigns the chosen Bank #',0x0813e4a8,branch(0x0813e4a8,0x081224d4),branch(0x0813e4a8,PC['pc_project_save']))
add('Scenes on-screen feedback before Cobalt flip',0x08137496,branch(0x08137496,0x0810134c),branch(0x08137496,PC['pc_present']))
add('Scenes page cell on page 0x19',0x081227f8,branch(0x081227f8,0x0813c384),branch(0x081227f8,PC['pc_projset_post']))
add('Scenes edit page rows',0x0813d20a,branch(0x0813d20a,0x08121cf4),branch(0x0813d20a,PC['pc_page_rows']))
add('Scenes edit page edits',0x08125e04,branch(0x08125e04,0x08124b44),branch(0x08125e04,PC['pc_knob']))
add('Tools MIDI row has no value',0x0814ba3c,struct.pack('<I',0x08131211),struct.pack('<I',PC['pc_tools_event']|1))
add('Row list tap on the MIDI row opens the MIDI page',0x0814cac4,struct.pack('<I',0x0813d89d),struct.pack('<I',PC['pc_list_event']|1))
add('Scenes project reader',0x08121416,branch(0x08121416,0x081079c0),branch(0x08121416,PC['sc_read']))
manifest=dict(name='Cobalt for 1010music bluebox desktop',version='1.1.0-midi.m9',firmware='desktop 1.5.2',size=len(stock),stock_sha256=STOCK_HASH,patched_sha256=hashlib.sha256(patched).hexdigest(),patches=patches,symbols={str(c):symbols(c) for c in [7,4]})
manifest['symbols']['gate7']=ngs
manifest['symbols']['pcm4']=PC
fields=[
 dict(key='accent',label='Selection accent',slots=[4,32],default='#9455fa'),
 dict(key='background',label='Base background',slots=[2],default='#141414'),
 dict(key='text',label='Primary text',slots=[3],default='#fcfcfc'),
 dict(key='button_idle',label='Button fill · idle',slots=[1],default='#56565a'),
 dict(key='button_active',label='Button fill · active / selected',slots=[5],default='#09d7f5'),
 dict(key='button_pressed',label='Button fill · pressed',slots=[31],default='#09d7f5'),
 dict(key='button_border',label='Button outlines',slots=[10],default='#09d7f5'),
 dict(key='control_active',label='FX active label / indicator',slots=[27],default='#09d7f5')]
for field in fields:
 field['offsets']=[0x0814d230-0x08040000+4*i for i in field['slots']]
# Mirror exposed roles into the dedicated conversion CLUT. Guard all copied
# unexposed entries too: the default table must equal native palette0..33.
clut=symbols(4)['theme_knob_palette']-0x08040000
for slot in range(34):
 if patched[clut+slot*4:clut+slot*4+4]!=patched[0x10d230+slot*4:0x10d234+slot*4]:raise ValueError('conversion palette mismatch '+str(slot))
for field in fields:
 field['offsets'] += [clut+slot*4 for slot in field['slots']]
fields.append(dict(key='knob',label='Knob outlines and pointers',slots=[],default='#09d7f5',offsets=[symbols(4)['theme_knob_rgb']-0x08040000]))
for field in fields:field['group']='core'
# Extended palette: every remaining native entry 1..33. These are plain data
# words mirrored into the SRAM palette and the conversion CLUT exactly like the
# traced roles. Placement comes from the four slot-finder builds photographed on
# hardware (alpha.5 theme 2); 'seen' entries changed visibly, the others showed
# no change on the screens checked. Keys stay slot numbers so themes remain valid.
EXTENDED=[
 ('labels',15,'Button & tab labels',True,'Labels on lit buttons and tabs (Vol, Double Row, MUTE on), TRACK MUTE/REC/SOLO labels, tuner note and centre line, EQ band nodes'),
 ('labels',30,'Selected setting text',True,'Text of the highlighted row in the Tools settings list'),
 ('labels',13,'Selected file text',True,'Text of the highlighted project in the PROJ file list'),
 ('states',23,'Solo',True,'Soloed track headers and outlines, SOLO button when on'),
 ('states',6,'Record arm · tuner far off',True,'Record-armed track headers and outlines, tuner bar when far off pitch'),
 ('states',11,'Record on · tuner in tune',True,'TRACK REC button when on, tuner needle when in tune'),
 ('states',20,'Tuner · slightly off',True,'Tuner bar when slightly off pitch'),
 ('graphs',8,'Graph background',True,'EQ and scope panels, tuner scale ticks, muted track numbers; some PROJ tab labels by state'),
 ('graphs',7,'Spectrum · scope channel 1',True,'EQ analyser bars and peak caps, scope channel 1, TRACK side-chain envelope (scope channel 2 uses the selection accent)'),
 ('graphs',24,'EQ curve',True,'EQ response line and the selected band node'),
 ('graphs',16,'Scope grid',True,'Scope grid lines'),
 ('graphs',25,'Learn list cells',True,'File-name cells in the MIDI Learn list'),
 ('unseen',19,'Dark red · B',False,'Stock #bb2222; possibly tuner bar trail'),
 ('unseen',12,'Dark red · A',False,'Stock #bb2222'),
 ('unseen',21,'Dark red · C',False,'Stock #bb2222'),
 ('unseen',18,'Green · B',False,'Stock #22bb22'),
 ('unseen',28,'Olive',False,'Stock #777100'),
 ('unseen',29,'Cyan · B',False,'Stock #09d7f5'),
 ('unseen',26,'Dark teal',False,'Stock #046b7a'),
 ('unseen',33,'White · C',False,'Stock #ffffff'),
 ('unseen',22,'Light grey',False,'Stock #aaaaaa'),
 ('unseen',9,'Dim grey',False,'Stock #666666'),
 ('unseen',14,'Black',False,'Stock #000000; not the header strip or track wells'),
 ('unseen',17,'Mid grey',False,'Stock #707070')]
core_slots={s for f in fields for s in f['slots']}
if sorted(core_slots|{s for _,s,_,_,_ in EXTENDED})!=list(range(1,34)):raise ValueError('extended palette must cover each native entry 1..33 once')
# Entries that showed no change on any photographed screen stay stock and are
# not offered; their keys in older theme files are ignored on import.
LEGACY_KEYS=[f'slot{slot:02d}' for group,slot,_,_,_ in EXTENDED if group=='unseen']
for group,slot,label,seen,hint in EXTENDED:
 if group=='unseen':continue
 word=patched[0x10d230+slot*4:0x10d234+slot*4]
 if word[3]!=255:raise ValueError('extended palette alpha '+str(slot))
 fields.append(dict(key=f'slot{slot:02d}',label=label,slots=[slot],default='#%06x'%(int.from_bytes(word,'little')&0xffffff),
  offsets=[0x10d230+slot*4,clut+slot*4],group=group,hint=hint))
# Compressor gain-reduction colour: one literal word in the M4 code cave, passed
# to the mod's generic pixel() for the header GR bar and the Thresh-row bar.
# Stored as 0x00RRGGBB (alpha 0), exactly as compiled.
# Located by value: the literal moves whenever the M4 code changes. Exactly one
# aligned 0x0079f7e0 word may exist in the M4 cave (checked).
cave=bytes(patched[0x78000:0x80000]);hits=[i for i in range(0,len(cave)-3,4) if cave[i:i+4]==struct.pack('<I',0x79f7e0)]
if len(hits)!=1:raise ValueError('compressor colour literal must occur exactly once in the M4 cave: '+str(len(hits)))
gr_color=0x78000+hits[0]
fields.append(dict(key='compressor',label='Compressor GR meter',slots=[],default='#79f7e0',offsets=[gr_color],alpha=0,group='mod',
 hint='Gain-reduction bar in the header and on the compressor Thresh row (mod code)'))
manifest['theme']=dict(schema=1,fields=fields,presets={
 'Default':{f['key']:f['default'] for f in fields},
 'Factory default':{f['key']:('#c24e9b' if f['key']=='accent' else f['default']) for f in fields},
 'Ocean':dict(accent='#1f87b5',background='#101b26',text='#edf6fc',button_idle='#33485c',button_active='#247f9c',button_pressed='#2b84a1',button_border='#5ac7df',control_active='#5ac7df',knob='#5ac7df',slot15='#edf6fc',slot30='#ffffff',slot13='#ffffff',slot23='#1f8a7a',slot06='#c93a44',slot11='#1e8a66',slot08='#0a141d',slot07='#3fb6e0',slot24='#f2c14e',slot16='#1d3446',slot25='#1a2c3b',compressor='#7fe0ff'),
 'Amber':dict(accent='#a8691a',background='#1c1813',text='#faf0dc',button_idle='#514538',button_active='#a36b24',button_pressed='#a86a20',button_border='#e0ad64',control_active='#e0ad64',knob='#e0ad64',slot15='#faf0dc',slot30='#ffffff',slot13='#fff8ea',slot23='#4f7a2e',slot06='#c0392b',slot11='#4f7d1f',slot08='#141009',slot07='#e8a849',slot24='#ff6a3d',slot16='#3a2f20',slot25='#2e2519',compressor='#ffd27a'),
 'Cyberpunk':dict(accent='#b52ba8',background='#101222',text='#edfaff',button_idle='#34304e',button_active='#7a2397',button_pressed='#922baf',button_border='#23dfed',control_active='#23dfed',knob='#23dfed',slot15='#edfaff',slot30='#ffffff',slot13='#ffffff',slot23='#00808a',slot06='#e01e5a',slot11='#138a50',slot08='#0a0b18',slot07='#23dfed',slot24='#ff3fd2',slot16='#24284a',slot25='#221f3a',compressor='#23dfed'),
 'Red':dict(accent='#c52e48',background='#211416',text='#fff1ed',button_idle='#59363b',button_active='#98303b',button_pressed='#b63842',button_border='#ff797e',control_active='#ff797e',knob='#ff797e',slot15='#fff1ed',slot30='#ffffff',slot13='#ffffff',slot23='#2a7a73',slot06='#d9480f',slot11='#23885a',slot08='#170d0f',slot07='#ff797e',slot24='#ffc06b',slot16='#3a2226',slot25='#2c1a1d',compressor='#ffb3a0'),
 'Pink':dict(accent='#b33888',background='#211621',text='#fff0fa',button_idle='#593b52',button_active='#95356f',button_pressed='#af438b',button_border='#ff9fdf',control_active='#ff9fdf',knob='#ff9fdf',slot15='#fff0fa',slot30='#ffffff',slot13='#ffffff',slot23='#2c7f74',slot06='#d0364f',slot11='#23885f',slot08='#170f17',slot07='#ff9fdf',slot24='#9fd8ff',slot16='#3a2738',slot25='#2c1d2b',compressor='#ffc6ec'),
 'Green':dict(accent='#277c4e',background='#12201a',text='#edfff4',button_idle='#345444',button_active='#256949',button_pressed='#2c8059',button_border='#8cf0b6',control_active='#8cf0b6',knob='#8cf0b6',slot15='#edfff4',slot30='#ffffff',slot13='#ffffff',slot23='#9a7a12',slot06='#c93838',slot11='#2f8f50',slot08='#0b1510',slot07='#5fdc8f',slot24='#f0d264',slot16='#1f3a2c',slot25='#1c2e25',compressor='#b6ffd6'),
 'Synthwave':dict(accent='#c71f72',background='#1a1030',text='#fff4fb',button_idle='#3b2a5c',button_active='#6f2bb0',button_pressed='#8434c9',button_border='#ffb86b',control_active='#ffb86b',knob='#ff7ad9',slot15='#fff4fb',slot30='#ffffff',slot13='#ffffff',slot23='#1f8794',slot06='#e0303a',slot11='#1b8a5e',slot08='#120a24',slot07='#36e2ff',slot24='#ffd166',slot16='#2c1f4a',slot25='#2a1c45',compressor='#36e2ff'),
 'Lagoon':dict(accent='#b8472b',background='#0e1f22',text='#f2fffd',button_idle='#234448',button_active='#17716b',button_pressed='#1d8178',button_border='#7fffd4',control_active='#ffd36e',knob='#7fffd4',slot15='#f2fffd',slot30='#ffffff',slot13='#ffffff',slot23='#7048b8',slot06='#d23a52',slot11='#258a4f',slot08='#0a1618',slot07='#7fffd4',slot24='#ff8a5b',slot16='#1d3a3d',slot25='#183134',compressor='#7fffd4'),
 'Bubblegum':dict(accent='#bf2e78',background='#22162b',text='#fff7fc',button_idle='#4a3357',button_active='#6447c4',button_pressed='#7556e0',button_border='#8be9fd',control_active='#fff176',knob='#8be9fd',slot15='#fff7fc',slot30='#ffffff',slot13='#ffffff',slot23='#1f8a7c',slot06='#d6334a',slot11='#24875a',slot08='#170f1d',slot07='#8be9fd',slot24='#fff176',slot16='#3a2a46',slot25='#33243f',compressor='#fff176'),
 'Arcade':dict(accent='#c2185b',background='#0d0d1a',text='#ffffff',button_idle='#2b2b55',button_active='#1565c0',button_pressed='#1976d2',button_border='#00e5ff',control_active='#ffcc00',knob='#00e5ff',slot15='#ffffff',slot30='#ffffff',slot13='#ffffff',slot23='#00806b',slot06='#d84315',slot11='#1b8a43',slot08='#08081a',slot07='#00e5ff',slot24='#ffcc00',slot16='#22224a',slot25='#1d1d40',compressor='#ffcc00'),
 'Graphite':dict(accent='#c2410c',background='#121212',text='#f5f5f5',button_idle='#3a3a3a',button_active='#4a4a4a',button_pressed='#5c5c5c',button_border='#8a8a8a',control_active='#ff8a50',knob='#ff8a50',slot15='#f5f5f5',slot30='#ffffff',slot13='#ffffff',slot23='#2f7290',slot06='#c62828',slot11='#2e7d46',slot08='#0c0c0c',slot07='#ff8a50',slot24='#8ad4ff',slot16='#2a2a2a',slot25='#262626',compressor='#ff8a50'),
 'Sunset':dict(accent='#c2365a',background='#1e1216',text='#fff3ea',button_idle='#4a2c33',button_active='#b4471f',button_pressed='#c0502a',button_border='#ffb070',control_active='#ffd27a',knob='#ffb070',slot15='#fff3ea',slot30='#ffffff',slot13='#ffffff',slot23='#2a7f80',slot06='#d6283c',slot11='#2a804a',slot08='#140c0f',slot07='#ffb070',slot24='#ff5c8a',slot16='#3a2229',slot25='#2c1a20',compressor='#ffd27a'),
 'Mint Chip':dict(accent='#2b7f62',background='#1a1411',text='#f4fff9',button_idle='#4a3a30',button_active='#3e7a64',button_pressed='#47836c',button_border='#9ff5d0',control_active='#9ff5d0',knob='#9ff5d0',slot15='#f4fff9',slot30='#ffffff',slot13='#ffffff',slot23='#9a5a22',slot06='#c8423a',slot11='#2a7f48',slot08='#120d0b',slot07='#9ff5d0',slot24='#ffb38a',slot16='#33281f',slot25='#2b211a',compressor='#9ff5d0'),
 'Glacier':dict(accent='#2f6fb0',background='#0f1620',text='#f0f8ff',button_idle='#34465a',button_active='#3a6f95',button_pressed='#427aa0',button_border='#bfe6ff',control_active='#bfe6ff',knob='#dff3ff',slot15='#f0f8ff',slot30='#ffffff',slot13='#ffffff',slot23='#2f7f70',slot06='#c8404a',slot11='#2a7f52',slot08='#0a1018',slot07='#bfe6ff',slot24='#ffb3c7',slot16='#1f2e40',slot25='#1a2636',compressor='#dff3ff'),
 'Phosphor':dict(accent='#1f7a3a',background='#050a06',text='#c8ffcf',button_idle='#143a1f',button_active='#1f6b33',button_pressed='#24763a',button_border='#39ff6a',control_active='#ffb000',knob='#39ff6a',slot15='#c8ffcf',slot30='#eaffee',slot13='#eaffee',slot23='#8a5e00',slot06='#c43c2c',slot11='#237a33',slot08='#030803',slot07='#39ff6a',slot24='#ffb000',slot16='#0f2a14',slot25='#0c2010',compressor='#ffb000'),
 'Grape Soda':dict(accent='#7b3fc4',background='#160f22',text='#f8f2ff',button_idle='#3a2c52',button_active='#5a2fa0',button_pressed='#6a3ab8',button_border='#c9a6ff',control_active='#b6ff6a',knob='#c9a6ff',slot15='#f8f2ff',slot30='#ffffff',slot13='#ffffff',slot23='#237a72',slot06='#d63a5a',slot11='#2a7f48',slot08='#0f0a18',slot07='#c9a6ff',slot24='#b6ff6a',slot16='#2a2040',slot25='#241a38',compressor='#b6ff6a')})
for name,theme in manifest['theme']['presets'].items():
 theme.setdefault('knob',theme['button_border'])
 theme.setdefault('control_active',theme['button_border'])
 for f in fields:theme.setdefault(f['key'],f['default'])
 manifest['theme']['presets'][name]={f['key']:theme[f['key']] for f in fields}
for name,theme in manifest['theme']['presets'].items():
 (ROOT/'themes'/(name.lower().replace(' ','-')+'.json')).write_text(json.dumps(dict(schema=1,colors=theme),indent=2)+'\n')
manifest['theme']['legacy_keys']=LEGACY_KEYS
manifest['accent']=dict(default='#9455fa',offsets=fields[0]['offsets'])
manifest['symbols']['gr7']=gr_symbols
manifest['symbols']['delay7']=ds
manifest['symbols']['gate7']=ngs
manifest['meters_only_sha256']='76fb996aa76ecfa0d8234aca1210bf360e234a49653b8fe213c97f131deaa29f'
manifest['cpu_only_sha256']='bc86a68620be4916c64220acb51bd04a6b2bb5c2d53517614588f58cc9b0fb9a'
(ROOT/'patch_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
template=(ROOT/'tools'/'patcher_template.py').read_text()
(ROOT/'patch_bluebox.py').write_text(template.replace('__MANIFEST_JSON__',repr(json.dumps(manifest,separators=(',',':')))))
html=(ROOT/'tools'/'patcher_template.html').read_text().replace('__VERSION__',manifest['version'])
CORE_HINTS=dict(accent='Selected track headers, control-group frame, selected list row, scope channel 2',background='Menus and empty screen areas',text='Track numbers, clock, idle button labels, knob labels, list and value text',
 button_idle='Track headers, title bars, idle buttons, list rows, keys, muted track meters',button_active='Lit tabs and toggles, level meters, selected file row',button_pressed='Button while pressed',button_border='Native outline colour (not drawn in the preview)',
 control_active='Labels on lit FX toggles (FILT, BEAT, PING)',knob='Knob arcs and pointers, GATE label, XY pad markers')
def row(f):
 name=escape(f['label']);hint=f.get('hint',CORE_HINTS.get(f['key'],''))
 return ('<div class="swatch-row" data-key="'+f['key']+'"><input id="'+f['key']+'" class="sw" type="color" value="'+f['default']+'" aria-label="'+name+' colour">'
  '<span class="meta"><span class="name">'+name+'</span><span class="hint">'+escape(hint)+'</span></span>'
  '<input id="'+f['key']+'Hex" class="hex" type="text" value="'+f['default']+'" maxlength="7" spellcheck="false" aria-label="'+name+' hex colour"></div>')
field_html='<div class="swatches">'+''.join(row(f) for f in fields)+'</div>'
(ROOT/'patch_bluebox.html').write_text(html.replace('__THEME_FIELDS_HTML__',field_html).replace('__MANIFEST_JSON__',json.dumps(manifest,separators=(',',':'))))
# GitHub Pages: publish docs/ (index.html is the same self-contained page).
(ROOT/'docs').mkdir(exist_ok=True)
(ROOT/'docs'/'index.html').write_text((ROOT/'patch_bluebox.html').read_text())
(ROOT/'docs'/'.nojekyll').write_text('')
print('Built Cobalt v1.1.0-midi.m9 patcher. Expected output SHA256:',manifest['patched_sha256'])
print('Added code bytes:',sum(len(base64.b64decode(p['new'])) for p in patches if p['name'].endswith('code')))
