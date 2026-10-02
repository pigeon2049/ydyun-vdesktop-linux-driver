CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror
OUT := ../build/probe

.PHONY: all clean
all: $(OUT)/mt-status $(OUT)/umd_bridge_shim.so $(OUT)/umd_connect_harness \
	     $(OUT)/pvr_node_probe $(OUT)/pvr_dma_smoke $(OUT)/pvr_cover_probe \
	     $(OUT)/pvr_kick_probe $(OUT)/umd_busid_trace.so

$(OUT)/mt-status: mt-status.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@

$(OUT)/umd_bridge_shim.so: umd_bridge_shim.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -shared -fPIC $< -o $@ -ldl

$(OUT)/umd_connect_harness: umd_connect_harness.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@ -ldl

$(OUT)/pvr_node_probe: pvr_node_probe.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I ../kernel $< -o $@

$(OUT)/pvr_dma_smoke: pvr_dma_smoke.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I ../kernel $< -o $@

$(OUT)/pvr_cover_probe: pvr_cover_probe.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I ../kernel $< -o $@

$(OUT)/pvr_kick_probe: pvr_kick_probe.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I ../kernel $< -o $@

$(OUT)/umd_busid_trace.so: umd_busid_trace.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -shared -fPIC $< -o $@ -ldl

clean:
	rm -f $(OUT)/mt-status $(OUT)/umd_bridge_shim.so $(OUT)/umd_connect_harness \
	      $(OUT)/pvr_node_probe $(OUT)/pvr_dma_smoke $(OUT)/pvr_cover_probe \
	      $(OUT)/pvr_kick_probe $(OUT)/umd_busid_trace.so
