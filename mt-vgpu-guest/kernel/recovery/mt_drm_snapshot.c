// SPDX-License-Identifier: GPL-2.0
/* Read-only snapshot of the retained r31 root plus its binding metadata. */
#include "../mt_guest_device.h"
static bool enable;
module_param(enable, bool, 0400);
static unsigned long long token = 1;
module_param(token, ullong, 0400);
static unsigned int expected_contexts = 2;
module_param(expected_contexts, uint, 0400);
static unsigned int expected_pages = 18;
module_param(expected_pages, uint, 0400);
static bool require_sealed = true;
module_param(require_sealed, bool, 0400);
static struct pci_dev *device;
static struct module *owner;
static void *snapshot;
#define SNAPSHOT_BYTES (4096 + 131072)
static ssize_t root_read(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	return memory_read_from_buffer(buf, count, &off, snapshot, SNAPSHOT_BYTES);
}
static BIN_ATTR_ADMIN_RO(root, SNAPSHOT_BYTES);
static struct bin_attribute *bins[] = {&bin_attr_root, NULL};
static const struct attribute_group group = {.name="mt_drm_snapshot", .bin_attrs=bins};
static int __init start(void)
{
	struct mt_guest_device *d;
	struct mt_pool_slice *slice;
	struct mt_execution_context *context = NULL;
	struct mt_gpu_vm *vm;
	struct mt_bo_vram_handle *handle;
	void *mapping;
	u32 i;
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;
	if (!enable) return -EPERM;
	if (!expected_contexts || expected_contexts > 8 || token >= expected_contexts)
		return -EINVAL;
	if (!expected_pages || expected_pages > 32)
		return -EINVAL;
	snapshot = kvzalloc(SNAPSHOT_BYTES, GFP_KERNEL);
	if (!snapshot) return -ENOMEM;
	device = mt_guest_find_s3000();
	if (!device) goto free_snapshot;
	device_lock(&device->dev);
	if (device->vendor != 0x1ed5 || device->device != 0x0222 ||
	    !device->driver || strcmp(device->driver->name,MT_GUEST_DRIVER_NAME)) goto unlock_device;
	owner = device->driver->driver.owner;
	if (!owner || !try_module_get(owner)) goto unlock_device;
	d = pci_get_drvdata(device);
	if (!d) goto put_owner;
	mutex_lock(&d->state.trial_lock);
	if (d->markers.total || d->execution.processes != expected_contexts ||
	    d->execution.contexts != expected_contexts)
		goto unlock_session;
	for (slice=d->reserved_pools.slices[1].slices; slice; slice=slice->next) {
		if (slice->context && slice->context->process && slice->context->process->token==token) {
			if (context) goto unlock_session;
			context=slice->context;
		}
	}
	if (!context || context->active_jobs || context->process->store != &d->execution)
		goto unlock_session;
	vm=context->process->vm;
	if (!vm || vm->active_uses || vm->sealed != require_sealed || !vm->uploaded || vm->owners!=1 ||
	    vm->capacity!=131072 || vm->count!=20 || vm->used_pages!=expected_pages ||
	    vm->tables->store!=&d->buffers || vm->tables->ops!=d->buffers.ops)
		goto unlock_session;
	ret=mt_bo_cpu_begin(vm->tables,&mapping);
	if (ret) goto unlock_session;
	handle=vm->tables->backing.handle;
	if (handle->system)
		memcpy(snapshot+4096,mapping,131072);
	else
		memcpy_fromio(snapshot+4096,(void __iomem *)mapping,131072);
	ret=mt_bo_cpu_end(vm->tables);
	if (ret) goto unlock_session;
	memcpy(snapshot,"MTDRMR31",8);
	mt_fw_put64(snapshot,8,vm->tables->backing.gpu_pa);
	mt_fw_put32(snapshot,16,vm->capacity);
	mt_fw_put32(snapshot,20,vm->used_pages);
	mt_fw_put32(snapshot,24,vm->count);
	mt_fw_put64(snapshot,32,context->process->token);
	for(i=0;i<vm->count;i++) {
		struct mt_vm_binding *b=&vm->bindings[i];
		u32 off=64+i*32;
		mt_fw_put64(snapshot,off,b->va);
		mt_fw_put64(snapshot,off+8,b->bo->backing.gpu_pa);
		mt_fw_put32(snapshot,off+16,b->offset);
		mt_fw_put32(snapshot,off+20,b->bytes);
		mt_fw_put32(snapshot,off+24,b->flags);
		mt_fw_put32(snapshot,off+28,b->bo->page_pa ? 1 : 0);
	}
	ret=sysfs_create_group(&device->dev.kobj,&group);
	if (!ret) {
		mutex_unlock(&d->state.trial_lock);
		device_unlock(&device->dev);
		return 0;
	}
unlock_session:
	mutex_unlock(&d->state.trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&device->dev);
	pci_dev_put(device);
free_snapshot:
	kvfree(snapshot);
	return ret;
}
static void __exit stop(void)
{
	sysfs_remove_group(&device->dev.kobj,&group);
	module_put(owner);
	pci_dev_put(device);
	kvfree(snapshot);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only retained DRM root snapshot");
