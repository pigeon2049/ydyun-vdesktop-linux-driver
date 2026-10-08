/* r376: Probe-side TA VM API for bridge.
 * Replaces r375's bridge-side manual VM assembly.
 */
#ifndef _MT_PROBE_TA_VM_H
#define _MT_PROBE_TA_VM_H

struct mt_probe_ta_vm;
struct mt_bo;
struct mt_guest;
struct mt_system_memory;
struct mt_vm_binding;

/* Create a per-file TA VM with proper mt_gpu_vm_init().
 * Returns NULL on failure. */
struct mt_probe_ta_vm *mt_probe_ta_vm_create(void);

/* Destroy a TA VM. */
void mt_probe_ta_vm_destroy(struct mt_probe_ta_vm *tvm);

/* Bind mappings to a TA VM. Returns 0 on success, negative errno. */
int mt_probe_ta_vm_bind(struct mt_probe_ta_vm *tvm,
			const struct mt_vm_binding *bindings, u32 count);

/* Safe cross-module borrow. Uses probe's ops (no address mismatch).
 * Caller must hold no locks; this takes d->buffers.lock internally. */
int mt_probe_bo_borrow(struct mt_bo *bo, struct mt_guest *g,
		       struct mt_system_memory *m);

#endif /* _MT_PROBE_TA_VM_H */
