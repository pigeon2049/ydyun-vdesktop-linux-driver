CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror
OUT ?= ../build/userspace
all: $(OUT)/mt-copy $(OUT)/mt-copy-check $(OUT)/mt-drm-check $(OUT)/mt-fill-check $(OUT)/mt-surface-check $(OUT)/mt-3d-check $(OUT)/mt-ta-readback
$(OUT):
	mkdir -p $@
$(OUT)/%: %.c mt-drm-check-common.h ../include/mt_copy_uapi.h ../include/mt_drm_uapi.h | $(OUT)
	$(CC) -std=c11 $(CFLAGS) $< -o $@
.PHONY: all
