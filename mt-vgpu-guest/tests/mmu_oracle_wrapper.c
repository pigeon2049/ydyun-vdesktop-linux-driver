#include "../kernel/mt_mmu.h"

uint64_t encode_pc(uint64_t pa, uint64_t flags) { return mt_mmu_pc(pa, flags); }
uint64_t encode_pd(uint64_t pa, uint64_t flags) { return mt_mmu_pd(pa, flags); }
uint64_t encode_pt(uint64_t pa, uint64_t flags) { return mt_mmu_pt(pa, flags); }
int build(void *out, uint64_t tables, uint64_t fw, uint64_t va)
{
    return mt_mmu_build_firmware(out, tables, fw, va);
}
