#!/usr/bin/env python3
"""Compare Linux fixed shared mappings with reference enumeration/page calls."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
from process_resources_reference import ProcessResourcesOracle

ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'build/firmware/process-resources.so'
path.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
               ROOT/'tests/process_resources_oracle_wrapper.c','-o',path],check=True)
lib=C.CDLL(str(path));lib.shared_ranges.argtypes=[C.POINTER(C.c_uint64),C.c_uint32,C.c_uint32]
cases=[];page_count=0
for dynamic in (False,True):
    for alternative in (True,False):
        for mask in (0,1,4,5):
            # Fresh original allocator state for each initialization sequence.
            x=ProcessResourcesOracle()
            resources,pages=x.run(mask,alternative,dynamic)
            page_count+=len(pages)
            out=(C.c_uint64*36)();count=lib.shared_ranges(out,mask,dynamic)
            assert count==len(resources),(count,resources)
            mappings=[tuple(out[i*4:i*4+4]) for i in range(count)]
            assert mappings==[(va,pa,size,0) for _,va,pa,size in resources]
            assert [(va+off,pa+off,flags) for va,pa,size,flags in mappings
                    for off in range(0,size,4096)]==pages
            cases.append(dict(dynamic_pools=dynamic,alternative_paging_allocator=alternative,
                              optional_mask=mask,resources=resources,pages=len(pages),linux_compared=True))
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.x.pe.sha256,
    cases=cases,total_page_calls=page_count,linux_compared_cases=16,
    executed=['02a34c MMU description','01ccd8 Guest heap/reservation layout',
              '01dafc/01d98c/01d3c8 dynamic pool creation with independent backing',
              '01a558 platform creation using adapter+0x790 resource plan',
              '019c5c/019cc4/01e1e4 static allocation selection and initialization',
              '014d58 process resource enumeration','00a630 reserved-VA mapping','009258 reservation lookup'],
    modeled=['OS RAM/locks','022218/022f38 OS backing allocation and physical page lists',
             'optional resource presence; 0230b4 capability false',
             '018bd0 page insertion boundary'],
    paging_command=dict(resource_index=3,bytes=0x400000,va=hex(0x81ff800000),
                        platform_descriptor_offset=hex(0x138),platform_va_offset=hex(0x140)),
    creation_preserves_pattern=['PDS','USC','Fence','Paging Command'],
    hardware_access=False,gpu_execution=False,
    limits='Fixed resources and three independently created pools; Linux upload/suballocation and GPU execution remain unverified.')
(ROOT/'reports/process-resources-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2))
