/* SPDX-License-Identifier: GPL-2.0 */
/* Second LD_PRELOAD object: name the busid the UMD expects.
 *
 * PVRSRVConnectionCreateDevice fails before it issues a single ioctl, and the
 * UMD's own debug output is unusable: its debug sink is a function pointer
 * that is a no-op in this build (no debug file is ever opened), so the
 * "drmOpenByBusid: Searching for BusID %s" line that would answer this cannot
 * be reached.
 *
 * The comparison itself is still observable. drmOpenByBusid does
 *
 *     strcasecmp(drmGetBusid(fd), wanted);
 *     if (strncasecmp(drmGetBusid(fd), "pci", 3) == 0) continue scanning;
 *
 * so intercepting strcasecmp/strncasecmp and logging only the pairs where
 * either side looks like a busid ("pci:0000:00:0e.0") prints exactly the two
 * strings that fail to match, with nothing guessed and no volume from the
 * thousands of unrelated string compares the UMD does.
 *
 * Load order: UMD_SHIM_BRIDGE_SHIM.so, then this one. It is passive -- it
 * forwards every call unchanged and only adds a log line.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static FILE *busid_log;
static int resolving;

static void busid_open_log(void)
{
	const char *path;

	if (busid_log)
		return;
	path = getenv("UMD_BUSID_TRACE");
	if (!path || !*path)
		return;
	busid_log = fopen(path, "a");
	if (busid_log)
		setvbuf(busid_log, NULL, _IOLBF, 0);
}

/* A DRM busid is "pci:0000:00:0e.0" or a platform/USB equivalent: a lowercase
 * type, a colon, then digits. Requiring the colon keeps this from matching the
 * UMD's ordinary string comparisons.
 */
static int busid_shaped(const char *s)
{
	const char *p;

	if (!s)
		return 0;
	for (p = s; *p; p++) {
		if (*p == ':')
			return 1;
		if (*p < 0x20)
			return 0;
	}
	return 0;
}

/* Compare against "pci" without calling strcmp/strcasecmp: those are the very
 * symbols this file interposes, and using them here recursed until the process
 * died. */
static int is_pci_literal(const char *s)
{
	return s && s[0] == 'p' && s[1] == 'c' && s[2] == 'i' && s[3] == '\0';
}

static int starts_with_pci(const char *s)
{
	return s && s[0] == 'p' && s[1] == 'c' && s[2] == 'i' && s[3] == ':';
}

static void busid_log_cmp(const char *fn, const char *a, const char *b, int r)
{
	busid_open_log();
	if (!busid_log)
		return;
	/* Log anything busid-shaped on either side, anything starting with the
	 * literal "pci:", and any comparison involving the exact busid our own
	 * node reports.
	 *
	 * The first, coarser filter logged nothing, which ruled out a busid
	 * mismatch reached through libc. That points at the vendor's own string
	 * compare, or at a libdrm-internal buffer compare, so the filter now
	 * keys on the "pci:" prefix instead of on a colon anywhere. Volume stays
	 * low because the UMD compares very few busid-shaped strings.
	 */
	if (!busid_shaped(a) && !busid_shaped(b) &&
	    !starts_with_pci(a) && !starts_with_pci(b) &&
	    !is_pci_literal(a) && !is_pci_literal(b))
		return;
	fprintf(busid_log,
		"{\"op\":\"%s\",\"a\":\"%s\",\"b\":\"%s\",\"match\":%s}\n",
		fn, a ? a : "(null)", b ? b : "(null)",
		r == 0 ? "true" : "false");
}

int strcasecmp(const char *a, const char *b)
{
	static int (*real)(const char *, const char *);
	int r;

	if (!real) {
		if (resolving)
			return 0;
		resolving = 1;
		real = dlsym(RTLD_NEXT, "strcasecmp");
		resolving = 0;
		if (!real)
			return 0;
	}
	r = real(a, b);
	busid_log_cmp("strcasecmp", a, b, r);
	return r;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
	static int (*real)(const char *, const char *, size_t);
	int r;

	if (!real) {
		if (resolving)
			return 0;
		resolving = 1;
		real = dlsym(RTLD_NEXT, "strncasecmp");
		resolving = 0;
		if (!real)
			return 0;
	}
	r = real(a, b, n);
	busid_log_cmp("strncasecmp", a, b, r);
	return r;
}

int strcmp(const char *a, const char *b)
{
	static int (*real)(const char *, const char *);
	int r;

	if (!real) {
		if (resolving)
			return 0;
		resolving = 1;
		real = dlsym(RTLD_NEXT, "strcmp");
		resolving = 0;
		if (!real)
			return 0;
	}
	r = real(a, b);
	busid_log_cmp("strcmp", a, b, r);
	return r;
}

/* libdrm and the PVR string helpers use the *bounded* compares for busids, so
 * interposing only the unbounded ones logged nothing at all. These two are the
 * remaining places a busid comparison can hide.
 */
int strncmp(const char *a, const char *b, size_t n)
{
	static int (*real)(const char *, const char *, size_t);
	int r;

	if (!real) {
		if (resolving)
			return 0;
		resolving = 1;
		real = dlsym(RTLD_NEXT, "strncmp");
		resolving = 0;
		if (!real)
			return 0;
	}
	r = real(a, b, n);
	busid_log_cmp("strncmp", a, b, r);
	return r;
}

int memcmp(const void *a, const void *b, size_t n)
{
	static int (*real)(const void *, const void *, size_t);
	int r;

	if (!real) {
		if (resolving)
			return 0;
		resolving = 1;
		real = dlsym(RTLD_NEXT, "memcmp");
		resolving = 0;
		if (!real)
			return 0;
	}
	r = real(a, b, n);
	/* memcmp has no NUL guarantee, so only log when both sides look like
	 * busids and the comparison covered at least their prefix. */
	if (busid_shaped(a) || busid_shaped(b))
		busid_log_cmp("memcmp", (const char *)a, (const char *)b, r);
	return r;
}
