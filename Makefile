CC = gcc
CFLAGS = -Iinclude -Wall -Wextra -std=c11 -g
LDFLAGS =
BUILD_DIR = build
DEPFLAGS = -MMD -MP

COMMON_SRCS = \
	src/spec/uci_sim_spec.c \
	src/spec/uci_sim_profile.c \
	src/spec/uci_sim_scenario.c \
	src/core/uci_sim_clock.c \
	src/core/uci_sim_packet.c \
	src/core/uci_sim_measurement.c \
	src/core/uci_sim_validation.c \
	src/core/uci_sim_engine.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c

APP_SRCS = $(COMMON_SRCS) src/transport/tcp/uci_sim_tcp_server.c src/main.c
TEST_SRCS = $(COMMON_SRCS) tests/test_sim_core.c
INTEROP_TEST_SRCS = $(COMMON_SRCS) src/transport/tcp/uci_sim_tcp_server.c tests/test_interop_tcp.c

APP_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
TEST_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(TEST_SRCS))
INTEROP_TEST_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(INTEROP_TEST_SRCS))
DEPS = $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(INTEROP_TEST_OBJS:.o=.d)

# ─────────────────────────────────────────────────────────────────────────────
# Default first target: show help                                                #
# ───────────────────────────────────────────────────────────────────────────────
.PHONY: all test uci-device-sim clean help

all: test uci-device-sim

help:
	@echo 'UCI Device Simulator — build & test commands'
	@echo ''
	@echo 'Build:'
	@echo '  make                Build and run all tests (default)'
	@echo '  help                Show this help message'
	@echo '  clean               Remove build directory and artifacts'
	@echo ''
	@echo 'Binary:'
	@echo '  make uci-device-sim   Build the simulator binary'
	@echo '                        ./build/uci-device-sim <host> <port> [default|delayed_notifications|ranging_stream]'
	@echo ''
	@echo 'Tests:'
	@echo '  make test             Run all tests (default)'
	@echo '  make uci_test         Run core simulator unit tests'
	@echo '  make interop_test     Run TCP interoperability tests'

uci-device-sim: $(BUILD_DIR)/uci-device-sim

$(BUILD_DIR)/uci-device-sim: $(APP_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJS) $(LDFLAGS)

$(BUILD_DIR)/test_sim_core: $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)

$(BUILD_DIR)/test_interop_tcp: $(INTEROP_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(INTEROP_TEST_OBJS) $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_sim_core $(BUILD_DIR)/test_interop_tcp
	@./$(BUILD_DIR)/test_sim_core
	@./$(BUILD_DIR)/test_interop_tcp

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
