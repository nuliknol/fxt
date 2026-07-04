CC ?= cc
AR ?= ar
HIPCC ?= hipcc

BUILD_DIR ?= build
MT_ENABLE_HIP_BACKEND ?= 1

CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS +=
LDLIBS += -lpthread -lm

CORE_SRCS := \
	src/dataset_store.c \
	src/mt4_csv_reader.c \
	src/candle_encode.c \
	src/char_block.c \
	src/char_distance.c \
	src/gpu_dispatch.c \
	src/cluster_graph.c \
	src/cluster_kmedoids.c \
	src/cluster_dbscan.c \
	src/symbol_stream.c \
	src/rle.c \
	src/word_mining.c \
	src/phrase_mining.c \
	src/wave_dp.c \
	src/wave_predictor.c \
	src/live_store.c \
	src/micro_harvest.c \
	src/logger.c \
	src/job_manager.c \
	src/json_codec.c \
	src/http_api.c

CORE_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(CORE_SRCS))
SERVER_OBJ := $(BUILD_DIR)/src/server_main.o
TEST_OBJ := $(BUILD_DIR)/tests/test_core.o
PREDICTOR_TEST_OBJ := $(BUILD_DIR)/tests/test_predictive_model.o
LIVE_STORE_TEST_OBJ := $(BUILD_DIR)/tests/test_live_store.o
MICRO_HARVEST_TEST_OBJ := $(BUILD_DIR)/tests/test_micro_harvest.o

HIP_OBJS := \
	$(BUILD_DIR)/src/hip_distance_kernels.o \
	$(BUILD_DIR)/src/hip_wave_kernels.o
EXTRA_OBJS :=

ifeq ($(MT_ENABLE_HIP_BACKEND),1)
CC := $(HIPCC)
CPPFLAGS += -DTRAINER_ENABLE_HIP_BACKEND=1
EXTRA_OBJS += $(HIP_OBJS)
endif

ALL_OBJS := $(CORE_OBJS) $(SERVER_OBJ) $(TEST_OBJ) $(EXTRA_OBJS)
DEPS := $(ALL_OBJS:.o=.d)

.PHONY: all clean test predictive_model_tests live_store_tests micro_harvest_tests

all: $(BUILD_DIR)/trainer_daemon $(BUILD_DIR)/test_core

$(BUILD_DIR)/trainer_daemon: $(CORE_OBJS) $(SERVER_OBJ) $(EXTRA_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_core: $(CORE_OBJS) $(TEST_OBJ) $(EXTRA_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_predictive_model: $(CORE_OBJS) $(PREDICTOR_TEST_OBJ) $(EXTRA_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_live_store: $(CORE_OBJS) $(LIVE_STORE_TEST_OBJ) $(EXTRA_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/test_micro_harvest: $(CORE_OBJS) $(MICRO_HARVEST_TEST_OBJ) $(EXTRA_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -DTEST_SOURCE_DIR=\".\" -c $< -o $@

$(BUILD_DIR)/src/hip_distance_kernels.o: src/hip_distance_kernels.hip.cpp
	@mkdir -p $(dir $@)
	@if ! command -v $(HIPCC) >/dev/null 2>&1; then \
		echo "error: MT_ENABLE_HIP_BACKEND=1 but $(HIPCC) not found in PATH"; \
		exit 1; \
	fi
	$(HIPCC) -c $< -Iinclude -o $@

$(BUILD_DIR)/src/hip_wave_kernels.o: src/hip_wave_kernels.hip.cpp
	@mkdir -p $(dir $@)
	@if ! command -v $(HIPCC) >/dev/null 2>&1; then \
		echo "error: MT_ENABLE_HIP_BACKEND=1 but $(HIPCC) not found in PATH"; \
		exit 1; \
	fi
	$(HIPCC) -c $< -Iinclude -o $@

test: $(BUILD_DIR)/test_core
	./$(BUILD_DIR)/test_core

predictive_model_tests: $(BUILD_DIR)/test_predictive_model
	./$(BUILD_DIR)/test_predictive_model

live_store_tests: $(BUILD_DIR)/test_live_store
	./$(BUILD_DIR)/test_live_store

micro_harvest_tests: $(BUILD_DIR)/test_micro_harvest
	./$(BUILD_DIR)/test_micro_harvest

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
