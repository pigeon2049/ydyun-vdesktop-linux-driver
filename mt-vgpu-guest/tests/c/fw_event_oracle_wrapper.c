#include "../../kernel/mt_fw_event.h"

struct queue { u8 *data; u32 *records, count, fail_after; };
static u32 read32(void *p, u32 o) { u32 v; memcpy(&v, ((struct queue *)p)->data + o, 4); return v; }
static void write32(void *p, u32 o, u32 v) { memcpy(((struct queue *)p)->data + o, &v, 4); }
static void copy_from(void *p, void *dst, u32 o, u32 n) { memcpy(dst, ((struct queue *)p)->data + o, n); }
static void barrier(void *p) { (void)p; }
static int consume(void *opaque, u32 dm, const struct mt_fw_event *e)
{
	struct queue *q = opaque;
	u32 *r = q->records + q->count * 8;
	if (q->count == q->fail_after)
		return -EAGAIN;
	r[0] = dm;
	r[1] = mt_fw_event_kind(e);
	memcpy(r + 2, e->words, sizeof(e->words));
	q->count++;
	return 0;
}
int drain(void *data, u32 bytes, u32 budget, u32 fail_after, u32 *records, u32 *count)
{
	const struct mt_fw_event_ops ops = {read32, write32, copy_from, barrier, barrier};
	struct queue q = {data, records, 0, fail_after};
	return mt_fw_event_drain(&ops, &q, bytes, budget, consume, &q, count);
}
int matches(const struct mt_fw_event *e, u32 pending, u32 oldest)
{ return mt_fw_event_matches(e, pending, oldest); }
void marker(void *command, u32 fence) { mt_fw_marker_command(command, fence); }
int paused(u32 flag) { return mt_fw_submission_paused(flag); }
