# UCI Device Simulator — Makefile

CC   = gcc
CFLAGS = -Iinclude -Wall -Wextra -std=c11 -g
LDFLAGS =
BUILD = build
DEPFLAGS = -MMD -MP

# ── Core sources ──
CORE_SRCS := \
	src/core/uci_sim_clock.c \
	src/core/uci_sim_engine.c \
	src/core/uci_sim_packet.c \
	src/core/uci_sim_validation.c \
	src/core/uci_sim_measurement.c \
	src/spec/uci_sim_profile.c \
	src/spec/uci_sim_scenario.c \
	src/spec/uci_sim_spec.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c

# ── Transports ──
TCP_SRC   = src/transport/tcp/uci_sim_tcp_server.c
CD_SRC    = src/transport/chardev/uci_sim_chardev.c

# ── Full app source lists ──
APP_TCP_SRCS  = $(CORE_SRCS) $(TCP_SRC)    src/main.c
APP_CD_SRCS   = $(CORE_SRCS) $(CD_SRC)      src/main_chardev.c
APP_DUAL_SRCS = $(CORE_SRCS) $(TCP_SRC) $(CD_SRC) src/main_dual.c

# ── Object lists ──
$(eval APP_TCP_OBJS  := $(patsubst %.c,$(BUILD)/%.o,$(APP_TCP_SRCS)))
$(eval APP_CD_OBJS   := $(patsubst %.c,$(BUILD)/%.o,$(APP_CD_SRCS)))
$(eval APP_DUAL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(APP_DUAL_SRCS)))
$(eval TEST_OBJS     := $(patsubst %.c,$(BUILD)/%.o,tests/test_sim_core.c))
$(eval TCPTEST_OBJS  := $(patsubst %.c,$(BUILD)/%.o,tests/test_interop_tcp.c))
$(eval CHARD_TEST_OBJS := $(patsubst %.c,$(BUILD)/%.o,tests/test_interop_chardev.c))
$(eval ALL_OBJS      := $(APP_TCP_OBJS) $(APP_CD_OBJS) $(APP_DUAL_OBJS) $(TEST_OBJS) $(TCPTEST_OBJS) $(CHARD_TEST_OBJS))
$(eval ALL_DEPS      := $(ALL_OBJS:.o=.d))

# ── Targets ──
.PHONY: all test clean help

all: help

help:
	@echo "Targets:"
	@echo "  make tcp      TCP server simulator"
	@echo "  make chardev  PTY chardev simulator"
	@echo "  make dual     chardev + TCP simulator"
	@echo "  make test     Run unit tests"
	@echo "  make clean    Remove build/"

tcp: $(BUILD)/uci-device-sim-tcp
chardev: $(BUILD)/uci-device-sim-chardev
dual: $(BUILD)/uci-device-sim-dual
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
$(BUILD)/test_sim_core: $(TEST_OBJS) $(patsubst %.c,$(BUILD)/%.o, $(CORE_SRCS))
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_interop_tcp: $(TCPTEST_OBJS) $(patsubst %.c,$(BUILD)/%.o, $(CORE_SRCS) $(TCP_SRC))
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_interop_chardev: $(CHARD_TEST_OBJS) $(patsubst %.c,$(BUILD)/%.o, $(CORE_SRCS) $(CD_SRC))
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/uci-device-sim-tcp: $(APP_TCP_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/uci-device-sim-chardev: $(APP_CD_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/uci-device-sim-dual: $(APP_DUAL_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Per-file compilation ──
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(ALL_DEPS)

clean:
	rm -rf $(BUILD)
