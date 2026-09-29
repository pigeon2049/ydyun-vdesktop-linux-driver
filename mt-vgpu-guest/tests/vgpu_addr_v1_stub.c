#include <stdint.h>

uint64_t mtgpu_guest_v1_device_paddr_to_host_device_paddr(void *config,
							  uint64_t gdpa)
{
	return config ? (0xabc0000000000000ULL | (gdpa & 0xffffffffULL)) : 0;
}
