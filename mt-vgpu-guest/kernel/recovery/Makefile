obj-m += mt_irq_recover.o
obj-m += mt_live_service.o
obj-m += mt_retained_dump.o
obj-m += mt_package_probe.o
obj-m += mt_hwr_request.o
obj-m += mt_master_notify.o
obj-m += mt_retired_disconnect.o
obj-m += mt_live_marker.o
obj-m += mt_live_tqx.o
obj-m += mt_tqx_snapshot.o
obj-m += mt_offline_info.o
KDIR ?= /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

obj-m += mt_idle_disconnect.o

obj-m += mt_live_tqx_repeat.o

obj-m += mt_live_copy_bridge.o

obj-m += mt_live_drm.o

obj-m += mt_drm_snapshot.o

obj-m += mt_live_graphics.o
obj-m += mt_live_surface.o
obj-m += mt_cold_disconnect.o
obj-m += mt_drain_pending.o
obj-m += mt_reconnect.o
obj-m += mt_fix_poll.o
obj-m += mt_live_3d.o
obj-m += mt_live_3d_drm.o
