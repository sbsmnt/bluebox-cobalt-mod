"""S27/S28 new-project template: a project named DEFAULT (any case, or
_DEFAULT) is the template for
File > New. The Projects scan's list clear and appends (0x0811f726,
0x0811f770, 0x0811f7be) note _DEFAULT and leave it out of the PROJ list
while User Settings "Hide _DEFAULT" is On. New loads _DEFAULT by name (as
LoadBank does after reading the list), and on load done the stock SaveAs
Template (0x08122e7c) makes the new project from it. The SD card, PresetMgr
thread and the engine IPC are stubbed; the hooks, the settings store and
the scene bank are the real ones."""
from firmware_harness import *
import firmware_harness as H
PC=M['symbols']['pcm4']
SESSION=0x300173d0;SETTINGS=SESSION+0x737c;NAMES=0x24010000
u=machine();heap=[0xc0100000]
u.mem_write(0xc0100000,b'\xf0'*0x10000) # S28: the device heap is not zeroed
def alloc(u):
 a=heap[0];heap[0]=(a+u.reg_read(UC_ARM_REG_R0)+15)&~15;u.reg_write(UC_ARM_REG_R0,a)
stub(u,0x8103128,alloc)
u.mem_write(0x30000000,bytes(u.mem_read(0x0814d230,0x400)))
call(u,0x8134cb8,0);call(u,0x8120a52,SETTINGS)
def s(u,p):return bytes(u.mem_read(p,80)).split(b'\0')[0].decode()
listed=[];loads=[];news=[];templates=[];busy=[1]
stub(u,0x8101e94,lambda u:None)
stub(u,0x8101e9a,lambda u:listed.append(s(u,u.reg_read(UC_ARM_REG_R1))))
stub(u,0x811e86c,lambda u:loads.append(s(u,u.reg_read(UC_ARM_REG_R1))))
stub(u,0x8122e3c,lambda u:news.append(s(u,u.reg_read(UC_ARM_REG_R1))))
stub(u,0x8122e7c,lambda u:templates.append(s(u,u.reg_read(UC_ARM_REG_R1))))
stub(u,0x8105260,lambda u:u.reg_write(UC_ARM_REG_R0,busy[0]))
for f in (0x81087e8,0x8122cf8,0x8122c2c,0x8108784,0x811e910):stub(u,f,lambda u:None)
def name(n):u.mem_write(NAMES,n.encode()+b'\0');return NAMES
def scan(*names):
 del listed[:];call(u,PC['pc_list_clear'],0x300172fc)
 for n in names:call(u,PC['pc_list_add'],0x300172fc,name(n))
 return list(listed)
def new(n):
 del loads[:],news[:],templates[:];call(u,PC['pc_new_project'],SESSION,name(n))
def done():call(u,PC['pc_load_result'],0x30016fd0,0)
def hide(v):call(u,0x8120264,SETTINGS,0x1ea,v)
u.mem_write(SESSION+0x73a2,b'\1')

done();check(not templates and not news,'S28: boot load with a non-zeroed heap creates no project')
check(call(u,0x8120234,SETTINGS,0x1ea)==0,'Hide _DEFAULT defaults Off')
check(scan('Alpha','Beta')==['Alpha','Beta'],'scan without a template lists every project')
new('New Project');check(news==['New Project'] and not loads,'no _DEFAULT: File > New is the stock New')
check(scan('Alpha','_DEFAULT','Beta')==['Alpha','_DEFAULT','Beta'],'Hide Off: _DEFAULT is listed')
new('Song 2')
check(loads==['_DEFAULT'] and not news and not templates,'with _DEFAULT: New loads _DEFAULT by name first')
done()
check(templates==['Song 2'] and not news,'load done: SaveAs Template makes "Song 2" from the loaded _DEFAULT')
del templates[:];done();check(not templates,'later loads do nothing more')
hide(1)
check(scan('Alpha','_DEFAULT','Beta')==['Alpha','Beta'],'Hide On: _DEFAULT left out of the PROJ list')
new('Song 3');done()
check(loads==['_DEFAULT'] and templates==['Song 3'],'hidden _DEFAULT is still the template')
new('_DEFAULT');check(news==['_DEFAULT'] and not loads,'New named _DEFAULT itself: stock New')
scan('Alpha');new('Song 4');check(news==['Song 4'] and not loads,'_DEFAULT deleted (not in the last scan): stock New')
scan('_DEFAULT')
busy[0]=0;new('Song 5');check(news==['Song 5'] and not loads,'another load pending: stock New')
busy[0]=1
u.mem_write(SESSION+0x73a2,b'\0');new('Song 6');done()
check(not templates and news==['Song 6'],'_DEFAULT did not load: stock New after all')
u.mem_write(SESSION+0x73a2,b'\1')
new('x'*70);check(news==['x'*70] and not loads,'names too long to hold: stock New')
hide(0)
# S28: DEFAULT, typed on the device (no underscore on its keyboard)
check(scan('Alpha','DEFAULT')==['Alpha','DEFAULT'],'DEFAULT is listed')
new('Song 7');done()
check(loads==['DEFAULT'] and templates==['Song 7'],'DEFAULT is the template')
scan('default','Beta');new('Song 8');done()
check(loads==['default'] and templates==['Song 8'],'any case: default loads by its listed name')
new('Default');check(news==['Default'] and not loads,'New named Default itself: stock New')
hide(1);check(scan('Alpha','DEFAULT')==['Alpha'],'Hide On leaves DEFAULT out');hide(0)
scan('DEFAULT');new('Song 9');call(u,PC['pc_load_request'],0x30016fd0,name('Beta'));done()
check(loads==['DEFAULT','Beta'] and not templates,'another load before the template finished: the New is dropped')
done();check(not templates,'and stays dropped')
check(scan('DEFAULTS','MY_DEFAULT')==['DEFAULTS','MY_DEFAULT'],'other names are not the template')
new('Song 10');check(news==['Song 10'] and not loads,'without the template: stock New')
print('Template checks:',H.checks)
