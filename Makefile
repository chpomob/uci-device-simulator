CC = gcc
CFLAGS = -Iinclude -Wall -Wextra -std=c11 -g
LDFLAGS =
BUILD_DIR = build

COMMON_SRCS = \
	src/spec/uci_sim_spec.c \
	src/core/uci_sim_packet.c \
	src/model/uci_sim_device.c \
	src/handlers/uci_sim_handlers.c

APP_SRCS = $(COMMON_SRCS) src/transport/tcp/uci_sim_tcp_server.c src/main.c
TEST_SRCS = $(COMMON_SRCS) tests/test_sim_core.c
INTEROP_TEST_SRCS = $(COMMON_SRCS) src/transport/tcp/uci_sim_tcp_server.c tests/test_interop_tcp.c

APP_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
TEST_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(TEST_SRCS))
INTEROP_TEST_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(INTEROP_TEST_SRCS))

all: test uci-device-sim

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
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_sim_core $(BUILD_DIR)/test_interop_tcp
	./$(BUILD_DIR)/test_sim_core
	./$(BUILD_DIR)/test_interop_tcp

clean:
	rm -rf $(BUILD_DIR)
