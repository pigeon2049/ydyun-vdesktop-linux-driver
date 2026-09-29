/* Read only the two connection-state registers traced in mtkm64.sys.
 * No PCI configuration writes, DMA, BAR1 access, or driver binding.
 * See PROTOCOL-NOTES.md, functions 1400254dc, 140023afc, 140007d68.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(__x86_64__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error This probe is validated only for little-endian x86_64 guests.
#endif

#define DEVICE "/sys/bus/pci/devices/0000:00:0e.0"

static void fail(const char *message)
{
    fprintf(stderr, "mt-status: %s\n", message);
    exit(EXIT_FAILURE);
}

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)((unsigned int)p[0] | (unsigned int)p[1] << 8);
}

static void check_unbound(void)
{
    struct stat st;
    if (lstat(DEVICE "/driver", &st) == 0)
        fail("PCI device has a bound driver; refusing concurrent MMIO access");
    if (errno != ENOENT)
        fail("cannot establish whether a driver is bound");
}

int main(int argc, char **argv)
{
    unsigned char config[256], visited[256] = {0};
    unsigned int cap;
    int fd, found = 0;
    uint64_t start, end, flags;
    FILE *resource;
    void *mapping;
    uint32_t driver, firmware;

    if (argc != 2 || strcmp(argv[1], "--read-status") != 0) {
        fprintf(stderr, "Usage: %s --read-status\n"
                "Reads two 32-bit MMIO registers on unbound 0000:00:0e.0.\n", argv[0]);
        return EXIT_FAILURE;
    }
    check_unbound();
    fd = open(DEVICE "/config", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        fail("cannot open PCI config (run with sudo)");
    if (pread(fd, config, sizeof(config), 0) != (ssize_t)sizeof(config))
        fail("cannot read complete 256-byte PCI configuration");
    close(fd);
    if (le16(config) != 0x1ed5 || le16(config + 2) != 0x0222 ||
        le16(config + 0x2c) != 0x1ed5 || le16(config + 0x2e) != 0x1101)
        fail("PCI identity does not match the investigated S3000 vGPU");
    if (!(le16(config + 4) & 2) || !(le16(config + 6) & 0x10) ||
        (config[0x0e] & 0x7f) != 0)
        fail("memory decoding, capabilities, or PCI header preconditions failed");
    cap = config[0x34];
    while (cap) {
        if (cap < 0x40 || cap > 0xfc || (cap & 3) || visited[cap])
            fail("invalid PCI capability chain");
        visited[cap] = 1;
        if (config[cap] == 0xaa && le16(config + cap + 2) == 0xaaaa)
            found = 1;
        cap = config[cap + 1];
    }
    if (!found)
        fail("Moore Threads Guest capability signature absent");
    resource = fopen(DEVICE "/resource", "r");
    if (!resource || fscanf(resource, "%" SCNx64 " %" SCNx64 " %" SCNx64,
                            &start, &end, &flags) != 3)
        fail("cannot read BAR0 resource metadata");
    fclose(resource);
    if (!start || end < start || end - start != 0xffff || !(flags & 0x200))
        fail("BAR0 is not the expected 64 KiB memory window");
    check_unbound();
    fd = open(DEVICE "/resource0", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        fail("cannot open BAR0 read-only");
    mapping = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED)
        fail("cannot map BAR0 read-only");
    /* Aligned volatile DWORD loads match the original read primitive. */
    driver = *(volatile const uint32_t *)((const char *)mapping + 0x890);
    firmware = *(volatile const uint32_t *)((const char *)mapping + 0x898);
    munmap(mapping, 4096);
    close(fd);
    printf("{\"pci\":\"0000:00:0e.0\",\"bar\":0,"
           "\"access\":\"read-only-u32\",\"driver_state\":%" PRIu32 ","
           "\"firmware_state\":%" PRIu32 ",\"hardware_acceleration_verified\":false}\n",
           driver, firmware);
    return EXIT_SUCCESS;
}
