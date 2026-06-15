# UCI Device Simulator — Makefile

CC   = gcc
CFLAGS = -Iinclude -Wall -Wextra -std=c11 -g
LDFLAGS =
BUILD = build
DEPFLAGS = -MMD -MP

# ── Per-target source lists ──
SRCS_TCP := \
	src/main.c \
	src/core/uci_sim_engine.c \
	src/core/uci_sim_packet.c \
	src/core/uci_sim_validation.c \
	src/core/uci_sim_measurement.c \
	src/spec/uci_sim_profile.c \
	src/spec/uci_sim_scenario.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c \
	src/transport/tcp/uci_sim_tcp_server.c

SRCS_CHARDEV := \
	src/main_chardev.c \
	src/core/uci_sim_engine.c \
	src/core/uci_sim_packet.c \
	src/core/uci_sim_validation.c \
	src/core/uci_sim_measurement.c \
	src/spec/uci_sim_profile.c \
	src/spec/uci_sim_scenario.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c \
	src/transport/chardev/uci_sim_chardev.c

SRCS_TEST_CORE := \
	tests/test_sim_core.c \
	src/core/uci_sim_engine.c \
	src/core/uci_sim_packet.c \
	src/core/uci_sim_validation.c \
	src/core/uci_sim_measurement.c \
	src/spec/uci_sim_profile.c \
	src/spec/uci_sim_scenario.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c

SRCS_TEST_TCP := \
	tests/test_interop_tcp.c \
	src/core/uci_sim_engine.c \
	src/core/uci_sim_packet.c \
	src/core/uci_sim_validation.c \
	src/core/uci_sim_measurement.c \
	src/spec/uci_sim_profile.c \
	src/spec/uci_sim_scenario.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c \
	src/transport/tcp/uci_sim_tcp_server.c

SRCS_TEST_CHARDEV := \
	tests/test_interop_chardev.c \
	src/core/uci_sim_engine.c \
	src/core/uci_sim_packet.c \
	src/core/uci_sim_validation.c \
	src/core/uci_sim_measurement.c \
	src/spec/uci_sim_profile.c \
	src/spec/uci_sim_scenario.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c \
	src/transport/chardev/uci_sim_chardev.c

# ── Object lists ──
OBJS_TCP          := $(SRCS_TCP:%.c=$(BUILD)/%.o)
OBJS_CHARDEV      := $(SRCS_CHARDEV:%.c=$(BUILD)/%.o)
OBJS_TEST_CORE    := $(SRCS_TEST_CORE:%.c=$(BUILD)/%.o)
OBJS_TEST_TCP     := $(SRCS_TEST_TCP:%.c=$(BUILD)/%.o)
OBJS_TEST_CHARDEV := $(SRCS_TEST_CHARDEV:%.c=$(BUILD)/%.o)
ALL_OBJS          := $(OBJS_TCP) $(OBJS_CHARDEV) $(OBJS_TEST_CORE) $(OBJS_TEST_TCP) $(OBJS_TEST_CHARDEV)
ALL_DEPS          := $(ALL_OBJS:.o=.d)

# ── Targets ──
.PHONY: all test clean help

all: help

help:
	@echo "Targets:"
	@echo "  make tcp      TCP server simulator"
	@echo "  make chardev  PTY chardev simulator"
	@echo "  make test     Run unit tests"
	@echo "  make clean    Remove build/"

tcp: $(BUILD)/uci-device-sim-tcp
chardev: $(BUILD)/uci-device-sim-chardev
test: uci_test tcp_test chardev_test

uci_test: $(BUILD)/test_sim_core
	@echo ""; echo "═══ Testing ..."; echo ""
	@./$(BUILD)/test_sim_core

tcp_test: $(BUILD)/test_interop_tcp
	@echo ""; echo "═══ TCP interop test ..."; echo ""
	@./$(BUILD)/test_interop_tcp

chardev_test: $(BUILD)/test_interop_chardev
	@echo ""; echo "═══ Chardev interop test ..."; echo ""
	@./$(BUILD)/test_interop_chardev

# Test binaries need the core library too
$(BUILD)/test_sim_core: $(OBJS_TEST_CORE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_interop_tcp: $(OBJS_TEST_TCP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_interop_chardev: $(OBJS_TEST_CHARDEV)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/uci-device-sim-tcp: $(OBJS_TCP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/uci-device-sim-chardev: $(OBJS_CHARDEV)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Per-file compilation ──
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(ALL_DEPS)

clean:
	rm -rf $(BUILD)
