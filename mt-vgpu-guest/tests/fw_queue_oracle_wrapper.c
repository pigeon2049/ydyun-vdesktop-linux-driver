#include "../kernel/mt_fw_queue.h"

struct memory_queue { u8 *data; u32 *trace; };
static void record(struct memory_queue *q, u32 value)
{
	if (q->trace && q->trace[0] < 64) {
		q->trace[++q->trace[0]] = value;
	}
}
static u32 read32(void *opaque, u32 offset)
{
	struct memory_queue *q = opaque;
	u32 value;
	record(q, 0x10000000 | offset);
	memcpy(&value, q->data + offset, 4);
	return value;
}
static void write32(void *opaque, u32 offset, u32 value)
{
	struct memory_queue *q = opaque;
	record(q, 0x40000000 | offset);
	memcpy(q->data + offset, &value, 4);
}
static void copy_to(void *opaque, u32 offset, const void *source, u32 size)
{
	struct memory_queue *q = opaque;
	record(q, 0x20000000 | offset);
	memcpy(q->data + offset, source, size);
}
static void barrier(void *opaque) { record(opaque, 0x30000000); }
static void kick(void *opaque, u32 dm) { record(opaque, 0x50000000 | dm); }
static const struct mt_fw_queue_ops ops = { read32, write32, copy_to, barrier, kick };

int submit(void *data, u32 bytes, u32 dm, u32 ring, void *command, u32 *trace)
{
	struct memory_queue q = { data, trace };
	return mt_fw_queue_submit(&ops, &q, bytes, dm, ring, command);
}
int idle(void *data, u32 bytes, u32 dm)
{
	struct memory_queue q = { data, NULL };
	return mt_fw_queue_dm_idle(&ops, &q, bytes, dm);
}
int work_idle(void *data, u32 bytes)
{
	struct memory_queue q = { data, NULL };
	return mt_fw_queue_work_idle(&ops, &q, bytes);
}
void command(void *data, u32 opcode, u32 pid, const u32 *parameters)
{
	mt_fw_kernel_command(data, opcode, pid, parameters);
}
int initialize(void *data, u32 bytes) { return mt_fw_queue_init_image(data, bytes); }
