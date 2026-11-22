"""S14 experiment: a synth track with Input None gets a stand-in input on the
audio core. Every interface -> audio core value send (bl 0x08108784) goes
through synth_send; the native send is stubbed to record what would reach
the audio core. The model (project) keeps None."""
from gate_ui_fixture import *
import firmware_harness as H
CHSRC=0x112;SYN_ON=0x1bf;RING=0x24070000
sent=[]
stub(u,0x8108784,lambda q:sent.append((q.reg_read(UC_ARM_REG_R1),q.reg_read(UC_ARM_REG_R2),q.reg_read(UC_ARM_REG_R3)&0xffffffff)))
setup(0,7,-96000)
def model(ch,pid,value):call(u,0x8120264,APP+0xc08+ch*0x30,pid,value&0xffffffff)
def get(ch,pid):return call(u,0x8120234,APP+0xc08+ch*0x30,pid)
def send(ch,pid,value):
 sent.clear();call(u,S['synth_send'],RING,ch,pid,value&0xffffffff);return sent[:]
model(7,CHSRC,0);model(7,SYN_ON,1)
check(send(7,CHSRC,0)==[(7,CHSRC,1)],'Synth On + Input None: the audio core gets input 1 as a stand-in')
check(get(7,CHSRC)==0,'the project still stores Input None')
check(send(7,CHSRC,13)==[(7,CHSRC,13)],'a real input passes through unchanged')
check(send(7,SYN_ON,1)==[(7,SYN_ON,1),(7,CHSRC,1)],'Synth On is followed by the stand-in input (boot, load, toggle)')
model(7,SYN_ON,0)
check(send(7,SYN_ON,0)==[(7,SYN_ON,0),(7,CHSRC,0)],'Synth Off is followed by None again')
check(send(7,CHSRC,0)==[(7,CHSRC,0)],'Synth Off + Input None stays None')
model(7,CHSRC,16);model(7,SYN_ON,1)
check(send(7,SYN_ON,1)==[(7,SYN_ON,1),(7,CHSRC,16)],'with a real input, Synth On re-sends that input')
check(send(7,0x4,500)==[(7,0x4,500)] and send(0x0c,CHSRC,0)==[(0x0c,CHSRC,0)],'other parameters and non-track targets pass through')
def bl_target(image,a):
 o=a-0x08040000;h1,h2=struct.unpack_from('<HH',image,o)
 s=(h1>>10)&1;j1=(h2>>13)&1;j2=(h2>>11)&1;i1=1-(j1^s);i2=1-(j2^s)
 off=(s<<24)|(i1<<23)|(i2<<22)|((h1&1023)<<12)|((h2&2047)<<1)
 if s:off-=1<<25
 return a+4+off
sites=[p['address'] for p in M['patches'] if p['name'].startswith('synth input stand-in send')]
check(len(sites)==60 and all(bl_target(patched,a)==S['synth_send']&~1 for a in sites),'all 60 native value sends go through synth_send')
print(H.checks,'S14 Input None stand-in checks passed; whether the audio core then plays the track needs hardware.')
