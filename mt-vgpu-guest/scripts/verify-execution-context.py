#!/usr/bin/env python3
"""Execute reference software-context/root association in isolated RAM."""
import json
from pathlib import Path
from reference_oracle import ReferenceOracle
ROOT=Path(__file__).resolve().parents[1]
A,MODE,DEVICE,PROCESS,ARGS,NODES,SEGMENTS,ROOTARG=range(0x200000,0x208000,0x1000)

def main():
 o=ReferenceOracle()
 o.hooks[0x140040728]=o.ret
 o.hooks[0x1400083b0]=lambda: (o.uc.mem_write(o.arg(0),bytes(o.arg(1))),o.ret())
 o.hooks[0x140008084]=o.ret
 o.hooks[0x140012f10]=lambda:o.ret(0)
 o.hooks[0x14000eec8]=o.ret
 o.hooks[0x14001e240]=o.ret
 o.put64(A,MODE);o.put64(DEVICE,A);o.put64(DEVICE+0x38,PROCESS)
 o.put64(DEVICE+0x18,0xabc000);o.put64(DEVICE+0x20,0xdef000)
 o.put64(A+0x418,0x220000);o.put32(A+0x42c,7);o.put64(A+0x2d8,NODES)
 allocations=[]
 def aux():
  allocations.append([o.arg(i) for i in range(9)])
  o.put64(o.arg(8),0x234000);o.ret(0)
 o.hooks[0x14001e000]=aux
 contexts=[]
 for idx,typ in enumerate((1,5,2,6,7,8)):
  o.uc.mem_write(ARGS,bytes(64));o.put64(ARGS,0x12345678);o.put32(ARGS+8,idx)
  node=NODES+idx*0x78;o.put32(node+8,typ)
  rc=o.run(0x1411ca690,[DEVICE,ARGS],[(0x1411ca690,0x1411ca9c4),(0x14000bd70,0x14000bd7b),(0x14000be24,0x14000be2c)])
  assert rc==0
  ctx=o.get64(ARGS)
  assert o.get64(ctx)==DEVICE and o.get64(ctx+0x10)==node
  assert o.get64(ctx+0x28)==0xabc000 and o.get64(ctx+0x38)==0xdef000
  assert o.get64(ctx+0x30)==0 and o.get64(ctx+0x70)==0x12345678
  assert o.get64(ctx+0x40)!=0 and bool(o.get64(ctx+0x48))==(typ==1)
  assert o.get64(ctx+0x68)==0x234000
  assert allocations[-1][4:6]==[0x5000,0x80]
  assert int.from_bytes(o.uc.mem_read(ARGS+0x24,4),'little')==0x4000
  assert int.from_bytes(o.uc.mem_read(ARGS+0x2c,4),'little')==400
  contexts.append(dict(node_index=idx,node_type=typ,root_initial=0,aux_bytes=0x5000,aux_alignment=0x80))
 translated=[]
 def translate():
  assert o.arg(0)==0x220000
  translated.append(o.arg(1));o.ret(o.arg(1)+0x600000000)
 o.hooks[0x140021d90]=translate # Platform CPU->GPU translation modeled, not verified here.
 o.put64(A+0xf8,SEGMENTS)
 for idx in range(3):o.put64(SEGMENTS+idx*0x20+8,(idx+1)*0x10000000)
 cases=0
 for seg in range(4):
  for offset in (0,4096,0x12345000):
   o.put32(ROOTARG+8,seg);o.put64(ROOTARG+0x10,offset)
   o.run(0x14000a750,[A,ctx,ROOTARG],[(0x14000a750,0x14000a7a4),(0x14000a600,0x14000a62e)])
   expected=offset+(seg*0x10000000 if seg else 0)
   assert translated[-1]==expected
   assert o.get64(ctx+0x30)==expected+0x600000000
   assert o.get64(PROCESS+0xa8)==expected+0x600000000
   cases+=1
 # Execute process identity assignment after modeled address-space setup.
 for addr in (0x1400184e4,0x140014b94,0x140014d58):o.hooks[addr]=lambda:o.ret(0)
 tokens=[]
 for start in (0,1,0x100000000,0xf123456789abcdef):
  o.put64(A+0x1618,start)
  rc=o.run(0x140015048,[A,ARGS,ROOTARG],[(0x140015048,0x140015177)])
  assert rc==0
  proc=o.get64(ROOTARG)
  assert o.get64(proc+0xa0)==start and o.get64(A+0x1618)==start+1
  tokens.append(start)
 report=dict(passed=True,reference_sha256=o.pe.sha256,contexts=contexts,root_associations=cases,
  process_tokens=tokens,hardware_accessed=False,
  modeled=['OS allocation/free/logging','platform address translation','auxiliary context BO allocation','process address-space setup'],
  limits='Software object associations only; auxiliary GPU context format, platform translation, firmware execution and context withdrawal not verified')
 (ROOT/'reports/execution-context-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
