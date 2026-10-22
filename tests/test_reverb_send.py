from reverb_fixture import *
for style in [0,14]:
 a,an,ae=rsetup(stock);b,bn,be=rsetup();same=True;energy=0.
 for n in range(2000):
  evs=[(0x155,style),(0x159,1000),(0x15a,1000),(0x13e,800),(0x146,0),(0x13d,0),(0x14a,0),(0x148,1000),(0x14f,0),(0x15c,0)] if n==0 else []
  inp=signal(.3,n) if 600<=n<630 else [0.]*32
  aa,ra=rblock(a,inp,evs=evs);bb,rb=rblock(b,inp,evs=evs)
  same=same and aa==bb and ra==rb==7
  if n>650:energy+=sum(x*x for x in struct.unpack('<64f',bb))
 check(same and an==bn,'send zero: native reverb PCM/chain match stock, style '+str(style))
 check(a.mem_read(REV,0x984)==b.mem_read(REV,0x984) and a.mem_read(0xc0010000,ae-0xc0010000)==b.mem_read(0xc0010000,be-0xc0010000) and a.mem_read(0,0x10000)==b.mem_read(0,0x10000),'send zero: native reverb object/SDRAM/ITCM state exact, style '+str(style))
 print('INFO reverb energy',style,energy,'bypass',b.mem_read(REV+0x6e,1).hex(),'object',b.mem_read(REV+0x70,52).hex())
 check(energy>0,'real reverb emits a nonzero tail in style '+str(style))
# Compare pending-send injection with explicitly mixed identical native input.
a,_,_=rsetup(stock);b,_,_=rsetup();same=True;energy=0
for n in range(2000):
 evs=[(0x155,0),(0x159,1000),(0x15a,1000),(0x13d,0),(0x15c,0)] if n==0 else []
 dry=signal(.1,n) if 600<=n<620 else [0.]*32
 send=signal(.2,n,700) if 600<=n<630 else [0.]*32
 # Native buffer samples are float32; match the addition rounding in C.
 f=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
 combined=[f(f(x)+f(y)) for x,y in zip(dry,send)]
 aa,ra=rblock(a,combined,evs=evs)
 w32(b,DS,0x44454c35);w32(b,DS+56,32);b.mem_write(DS+60,struct.pack('<64f',*(send+send)))
 bb,rb=rblock(b,dry,evs=evs,silent=0 if 600<=n<620 else 1)
 same=same and aa==bb and ra==rb==7 and u32(b,DS+56)==0
 if n>650:energy+=sum(x*x for x in struct.unpack('<64f',bb))
check(same,'actual stock reverb response to queued send is bit-exact to explicit input mixing, including silent destination')
check(energy>0,'queued delay send excites a real stock reverb tail')
# Native bypass leaves its input dry: the custom send must be discarded there.
a,_,_=rsetup(stock);b,_,_=rsetup()
for bypass in [1,1,0,1,0]:
 dry=[.1]*32;send=[.2]*32
 combined=dry if bypass else [struct.unpack('<f',struct.pack('<f',.1+.2))[0]]*32
 aa,_=rblock(a,combined,evs=[(0x15c,bypass)])
 w32(b,DS,0x44454c35);w32(b,DS+56,32);b.mem_write(DS+60,struct.pack('<64f',*(send+send)))
 bb,_=rblock(b,dry,evs=[(0x15c,bypass)])
 check(aa==bb and u32(b,DS+56)==0,'native reverb bypass transition '+str(bypass)+': discard/send follows queued state with no extra dry return')

print(str(H.checks)+' real desktop reverb/send checks passed; physical graph order/routes/timing remain hardware tests.')
