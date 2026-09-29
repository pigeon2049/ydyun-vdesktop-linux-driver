/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_CONNECTION_H
#define MT_GUEST_FW_CONNECTION_H

#include "mt_fw_queue.h"

/* Guest branches of 1400168b4/140016b4c. Caller owns the published resource
 * lifetime and serializes the whole transition. A negative return does NOT
 * authorize freeing or restoring memory: a command may still be in flight.
 * Initial READY/Guest-OFF and complete image publication are preconditions.
 */
struct mt_fw_connection_ops {
	u32 (*firmware_state)(void *opaque);
	u32 (*firmware_started)(void *opaque);
	void (*guest_state)(void *opaque, u32 state);
	void (*notify_online)(void *opaque);
	int (*send_command)(void *opaque, u32 opcode);
	int (*work_idle)(void *opaque);
	int (*control_idle)(void *opaque);
	void (*delay_25ms)(void *opaque);
	/* Shared page 0, byte 0 (140023874). Zero requires the recovery gate. */
	u32 (*gpu_normal)(void *opaque);
};

static inline int mt_fw_connection_ops_valid(const struct mt_fw_connection_ops *o)
{
	return o && o->firmware_state && o->firmware_started && o->guest_state &&
		o->notify_online && o->send_command && o->work_idle && o->control_idle &&
		o->delay_25ms && o->gpu_normal;
}

static inline int mt_fw_connect(const struct mt_fw_connection_ops *o, void *opaque)
{
	u32 poll, started;
	int ret;
	if (!mt_fw_connection_ops_valid(o))
		return -EINVAL;
	o->notify_online(opaque);
	/* Reference checks health once, then waits for READY before touching
	 * Guest state or enqueuing anything. Online notification precedes it.
	 */
	if (!o->gpu_normal(opaque)) {
		for (poll = 0; poll < 400; poll++) {
			if (o->firmware_state(opaque) == 1)
				break;
			o->notify_online(opaque);
			o->delay_25ms(opaque);
		}
		if (poll == 400)
			return -ETIMEDOUT;
	}
	o->guest_state(opaque, 1);
	ret = o->send_command(opaque, MT_FW_CONNECT);
	if (ret)
		return ret;
	for (poll = 0; poll < 400; poll++) {
		started = o->firmware_started(opaque);
		if (o->firmware_state(opaque) == 2 && started) {
			o->guest_state(opaque, 2);
			return 0;
		}
		if (o->firmware_state(opaque) == 4)
			return -ECONNREFUSED;
		if (poll && !(poll % 100)) {
			o->notify_online(opaque);
			ret = o->send_command(opaque, MT_FW_CONNECT);
			if (ret)
				return ret;
		}
		o->delay_25ms(opaque);
	}
	return -ETIMEDOUT;
}

static inline int mt_fw_disconnect(const struct mt_fw_connection_ops *o, void *opaque)
{
	u32 poll = 0, work_timed_out = 0, state;
	int idle, ret;
	if (!mt_fw_connection_ops_valid(o))
		return -EINVAL;
	if (!o->gpu_normal(opaque))
		return -EHOSTDOWN;
	while ((idle = o->work_idle(opaque)) != 1) {
		if (idle < 0)
			return idle;
		if (o->firmware_state(opaque) != 2)
			return -ESHUTDOWN;
		if (poll > 400) {
			work_timed_out = 1;
			break;
		}
		o->delay_25ms(opaque);
		poll++;
	}
	ret = o->send_command(opaque, MT_FW_DISCONNECT);
	if (ret)
		return ret;
	/* Reference still sends disconnect after work-drain timeout, but does
	 * not claim it was acknowledged and does not publish Guest OFF here.
	 */
	if (work_timed_out)
		return -ETIMEDOUT;
	for (poll = 0; poll < 400; poll++) {
		idle = o->control_idle(opaque);
		if (idle < 0)
			return idle;
		if (idle == 1) {
			o->guest_state(opaque, 0);
			return 0;
		}
		state = o->firmware_state(opaque);
		if (state != 2) {
			/* Firmware may consume DISCONNECT and publish its final event
			 * between the first queue snapshot and this state read. The
			 * Linux polling consumer must drain/recheck once before exiting.
			 * READY alone is never an acknowledgement: the already-submitted
			 * command and every DM0 event must also have retired. */
			if (state == 1 && !o->firmware_started(opaque)) {
				idle = o->control_idle(opaque);
				if (idle < 0)
					return idle;
				if (idle == 1 && o->firmware_state(opaque) == 1 &&
				    !o->firmware_started(opaque)) {
					o->guest_state(opaque, 0);
					return 0;
				}
			}
			return -ESHUTDOWN;
		}
		o->delay_25ms(opaque);
	}
	return -ETIMEDOUT;
}

#endif
