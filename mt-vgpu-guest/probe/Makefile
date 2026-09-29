CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror
OUT := ../build/probe

.PHONY: all clean
all: $(OUT)/mt-status

$(OUT)/mt-status: mt-status.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OUT)/mt-status
