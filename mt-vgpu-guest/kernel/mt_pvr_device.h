/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_PVR_DEVICE_H
#define MT_PVR_DEVICE_H

/* Connection and feature-block layout the legacy MASA driver expects.
 *
 * The UMD treats its own connection object as an opaque handle, but it reads
 * fixed offsets out of it, so those offsets are part of the ABI:
 *
 *   GetSrvHandle(conn)            conn+0x00   (decompiled.c:12120)
 *   MTClientApiToInfoPageIdx path conn+0x28   (decompiled.c:15837)
 *   MTSRVGetClientEventFilter     conn+0x50   (decompiled.c:15834)
 *   _AcquireHWPerfSetting         conn+0x60   (decompiled.c:15527)
 *   device-memory context slot    conn+0x78   (decompiled.c:13683)
 *   GetFeatures(conn)             conn+0xa0 + 0x620
 *   sync arenas                   conn+0xb0, conn+0xb8
 *
 * Every offset below was confirmed against a live offline session in bA13, and
 * the static asserts keep the layout from drifting.
 *
 * Feature block values: the offline session succeeded with an all-zero block
 * except where noted, so the defaults here are zeros on purpose. Setting a
 * non-zero value changes which code paths the UMD takes (for example
 * features+0x54 selects the DDK feature set and gates the sync allocation
 * width), so each deviation needs its own evidence.
 */

#include "mt_pvr_wire.h"

/* core allow-list the UMD compares the Connect result against
 * (decompiled.c:13659/13663). Reporting the second value satisfies both the
 * exact compare and the four sub-gates that follow it.
 */
#define MT_PVR_BVNC_ALLOW 0x0023000406600017ULL

/* info page fields the UMD validates (decompiled.c:92450 area). */
#define MT_PVR_INFO_DEVICES 1U
#define MT_PVR_INFO_CAPS 0xb57U
#define MT_PVR_INFO_BUILD 0x688a847U
#define MT_PVR_INFO_BYTES 0x10000U

/* mmap offset the info-page PMR is expected at, and the DRM offset space the
 * driver is known to use. The handle is the offset shifted down by 12.
 */
#define MT_PVR_INFO_OFFSET 0x1001000ULL

/* The feature block.
 *
 * The UMD reads scattered offsets out of it (core count at +0x04, the DDK
 * feature set at +0x54, timeout knobs at +0x68..+0x74, DDKDebug at +0x100) and
 * reaches the block through conn+0xa0+0x620. Rather than guess a layout we
 * have never seen named, keep it a blob with named accessors: the offline
 * session passed with everything zero except the core count, and a field we
 * cannot name is a field we must not invent.
 */
#define MT_PVR_FEATURE_BYTES 0x120U
#define MT_PVR_FEATURE_CORE_COUNT 0x04U
#define MT_PVR_FEATURE_SET 0x54U

struct mt_pvr_features {
	u8 bytes[MT_PVR_FEATURE_BYTES];
};

static inline u32 mt_pvr_feature_u32(const struct mt_pvr_features *f, u32 offset)
{
	u32 value = 0;

	if (offset + sizeof(value) <= MT_PVR_FEATURE_BYTES)
		memcpy(&value, f->bytes + offset, sizeof(value));
	return value;
}

static inline void mt_pvr_feature_set_u32(struct mt_pvr_features *f, u32 offset,
					  u32 value)
{
	if (offset + sizeof(value) <= MT_PVR_FEATURE_BYTES)
		memcpy(f->bytes + offset, &value, sizeof(value));
}

struct MT_PVR_PACKED mt_pvr_conn {
	u64 srv_handle;		/* +0x00 GetSrvHandle(conn) */
	u32 version;		/* +0x08 driver writes 1 */
	u32 pid;		/* +0x0c */
	u32 pad_10;
	u32 init_flags;		/* +0x14 set from the INIT call */
	u64 limit_info[2];	/* +0x18 limit fields written by Connect */
	u64 info_page;		/* +0x28 */
	u64 pad_30[3];
	u64 tl_stream;		/* +0x48 */
	u64 hwperf_um;		/* +0x50 */
	u64 pad_58;
	u64 hwperf_setting;	/* +0x60 */
	u64 devmem_mutex;	/* +0x68 */
	u32 devmem_refs;	/* +0x70 */
	u32 pad_74;
	u64 devmem_ctx;		/* +0x78 */
	u64 pad_80[4];
	u64 features;		/* +0xa0 */
	u64 pad_a8;
	u64 sync_arena;		/* +0xb0 */
	u64 sync_span;		/* +0xb8 */
};

static_assert(offsetof(struct mt_pvr_conn, srv_handle) == 0x00);
static_assert(offsetof(struct mt_pvr_conn, version) == 0x08);
static_assert(offsetof(struct mt_pvr_conn, pid) == 0x0c);
static_assert(offsetof(struct mt_pvr_conn, init_flags) == 0x14);
static_assert(offsetof(struct mt_pvr_conn, info_page) == 0x28);
static_assert(offsetof(struct mt_pvr_conn, tl_stream) == 0x48);
static_assert(offsetof(struct mt_pvr_conn, hwperf_um) == 0x50);
static_assert(offsetof(struct mt_pvr_conn, hwperf_setting) == 0x60);
static_assert(offsetof(struct mt_pvr_conn, devmem_mutex) == 0x68);
static_assert(offsetof(struct mt_pvr_conn, devmem_refs) == 0x70);
static_assert(offsetof(struct mt_pvr_conn, devmem_ctx) == 0x78);
static_assert(offsetof(struct mt_pvr_conn, features) == 0xa0);
static_assert(offsetof(struct mt_pvr_conn, sync_arena) == 0xb0);
static_assert(offsetof(struct mt_pvr_conn, sync_span) == 0xb8);

/* Offset the UMD adds to conn+0xa0 before reading the feature block. */
#define MT_PVR_FEATURE_SKEW 0x620U

static inline void mt_pvr_conn_init(struct mt_pvr_conn *conn, u32 pid,
				    u32 init_flags)
{
	memset(conn, 0, sizeof(*conn));
	/* The UMD passes the connection pointer straight back to the bridge
	 * dispatcher as the services handle, so it must be self-referential.
	 */
	conn->srv_handle = (u64)(uintptr_t)conn;
	conn->version = 1;
	conn->pid = pid;
	conn->init_flags = init_flags;
}

/* Fill the feature block. Zeros are the validated default; the core count is
 * the one field the driver reads for allocation sizing.
 */
static inline void mt_pvr_features_init(struct mt_pvr_features *features,
					u32 core_count)
{
	memset(features, 0, sizeof(*features));
	mt_pvr_feature_set_u32(features, MT_PVR_FEATURE_CORE_COUNT, core_count);
	/* features+0x54 must stay below 2: the UMD reads it as the DDK feature
	 * set and picks the older, validated allocation path.
	 */
}

/* Opt-in DDK feature-set advertisement (r78: >= 2 makes the UMD take the
 * gated sync allocation path, the only way DDK2 becomes reachable). Default
 * callers pass 0 and keep the validated legacy path; any non-zero value is an
 * explicit, separately approved experiment.
 */
static inline void mt_pvr_features_set_ddk(struct mt_pvr_features *features,
					   u32 ddk_feature_set)
{
	if (ddk_feature_set)
		mt_pvr_feature_set_u32(features, MT_PVR_FEATURE_SET,
				       ddk_feature_set);
}

/* Connect result the driver requires. Anything else sends it down the failure
 * branch at 0x3b8c5 with error 78.
 */
static inline void mt_pvr_connect_result(struct mt_pvr_connect_out *out)
{
	memset(out, 0, sizeof(*out));
	out->packed_bvnc = MT_PVR_BVNC_ALLOW;
	out->error = 0;
	out->capability_flags = 0;
	out->kernel_arch = 0;
}

/* Build the info page in place. The driver reads three fields and rejects the
 * page otherwise (decompiled.c:92477, 9247d).
 */
static inline void mt_pvr_info_page_init(void *page, size_t bytes)
{
	u32 *words;

	if (bytes < MT_PVR_INFO_BYTES)
		return;
	memset(page, 0, bytes);
	words = (u32 *)page;
	words[0x00 / 4] = MT_PVR_INFO_DEVICES;
	words[0x44 / 4] = MT_PVR_INFO_CAPS;
	words[0x48 / 4] = MT_PVR_INFO_BUILD;
}

static inline int mt_pvr_bvnc_allowed(u64 bvnc)
{
	return bvnc == 0x0001000000000000ULL || bvnc == MT_PVR_BVNC_ALLOW;
}

#endif /* MT_PVR_DEVICE_H */
