#include "LUNASHELL_acquisition_system.hpp"
#include "LUNASHELL_acquisition_internal.hpp"
#include "LUNASHELL_platform.h"

#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstring>
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ipc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "xt_utils.h"

namespace LUNASHELL::snapshot_detail::system_acquisition {
namespace {
constexpr char TAG[] = "LUNASHELL_system_acquisition";
std::mutex s_processor_usage_mutex;
static_assert(sizeof(size_t) == sizeof(uint32_t));
DRAM_ATTR volatile uint32_t s_allocation_failure_count = 0;
DRAM_ATTR volatile uint32_t s_largest_failed_allocation_bytes = 0;
DRAM_ATTR volatile uint32_t s_failed_allocation_caps_seen_mask = 0;
std::atomic_bool s_memory_telemetry_active{false};

// counter: Internal-DRAM telemetry counter shared by concurrent allocation failure hooks.
// candidate: Failed allocation size whose maximum is retained without taking a mutex.
FORCE_INLINE_ATTR void allocation_counter_update_max(
    volatile uint32_t *counter,
    uint32_t candidate
)
{
    uint32_t observed = *counter;
    while (observed < candidate) {
        if (xt_utils_compare_and_set(counter, observed, candidate)) {
            return;
        }
        observed = *counter;
    }
}

// counter: Internal-DRAM capability mask shared by concurrent allocation failure hooks.
// bits: Actual failed allocation capabilities retained through an atomic compare-and-set.
FORCE_INLINE_ATTR void allocation_counter_or(
    volatile uint32_t *counter,
    uint32_t bits
)
{
    uint32_t observed = *counter;
    while ((observed | bits) != observed) {
        if (xt_utils_compare_and_set(counter, observed, observed | bits)) {
            return;
        }
        observed = *counter;
    }
}

// counter: Internal-DRAM failure count; saturation preserves monotonic telemetry at UINT32_MAX.
FORCE_INLINE_ATTR void allocation_counter_increment_saturating(
    volatile uint32_t *counter
)
{
    uint32_t observed = *counter;
    while (observed != UINT32_MAX) {
        if (xt_utils_compare_and_set(counter, observed, observed + 1U)) {
            return;
        }
        observed = *counter;
    }
}

// requested_bytes: Original rejected allocation size; target size_t is verified to fit the counter.
// caps: Original heap capability requirements, accumulated without allocating or logging.
// function_name: Borrowed allocator diagnostic identity; deliberately unused by this IRAM callback.
void IRAM_ATTR allocation_failed_hook(
    size_t requested_bytes,
    uint32_t caps,
    const char *function_name
)
{
    (void)function_name;

    allocation_counter_update_max(
        &s_largest_failed_allocation_bytes,
        static_cast<uint32_t>(requested_bytes)
    );
    allocation_counter_or(&s_failed_allocation_caps_seen_mask, caps);
    __asm__ __volatile__("memw" ::: "memory");
    allocation_counter_increment_saturating(&s_allocation_failure_count);
}

struct ProcessorUsageState {
    bool initialized = false;
    configRUN_TIME_COUNTER_TYPE sampled_at[2] = {};
    configRUN_TIME_COUNTER_TYPE idle_runtime[2] = {};
};

ProcessorUsageState s_processor_usage;

struct ProcessorRuntimeSample {
    int requested_core = 0;
    configRUN_TIME_COUNTER_TYPE sampled_at = 0;
    configRUN_TIME_COUNTER_TYPE idle_runtime = 0;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

// context: Caller-owned sample; the blocking IPC call keeps it alive until completion.
void capture_processor_runtime_on_core(void *context)
{
    auto &sample = *static_cast<ProcessorRuntimeSample *>(context);
    const int running_core = esp_cpu_get_core_id();
    if (running_core != sample.requested_core) {
        sample.error = ESP_ERR_INVALID_STATE;
        return;
    }
    // The IPC task has displaced idle on this core. FreeRTOS has therefore
    // accounted its entire previous idle slice, and it cannot advance while
    // this callback runs. A remote read alone sees the last context switch.
    sample.idle_runtime = ulTaskGetIdleRunTimeCounterForCore(running_core);
    sample.sampled_at = static_cast<configRUN_TIME_COUNTER_TYPE>(esp_timer_get_time());
    sample.error = ESP_OK;
}

struct StaticMemoryMetadata {
    bool ready = false;
    uint32_t flash_capacity_bytes = 0;
    uint32_t app_partition_bytes = 0;
    uint32_t firmware_image_bytes = 0;
};

struct StaticProcessorMetadata {
    bool ready = false;
    uint8_t core_count = 0;
};

StaticMemoryMetadata s_static_memory_metadata;
StaticProcessorMetadata s_static_processor_metadata;
}  // namespace

// No parameters: acquire memory and per-core counters through their existing owners.
// Exception: Returns the first genuine acquisition or canonical publication failure.
esp_err_t sample()
{
    LUNASHELL::MemorySnapshot memory;
    LUNASHELL::ProcessorSnapshot processor;
    const int64_t attempted_at_us = esp_timer_get_time();
    esp_err_t memory_error = ESP_OK;
    esp_err_t processor_error = ESP_OK;

    if (s_static_memory_metadata.ready) {
        memory.flash_capacity_bytes =
            s_static_memory_metadata.flash_capacity_bytes;
        memory.app_partition_bytes =
            s_static_memory_metadata.app_partition_bytes;
        memory.firmware_image_bytes =
            s_static_memory_metadata.firmware_image_bytes;
    } else {
        memory_error = esp_flash_get_size(
            nullptr,
            &memory.flash_capacity_bytes
        );
        const esp_partition_t *running_partition =
            esp_ota_get_running_partition();
        if (running_partition != nullptr) {
            memory.app_partition_bytes = running_partition->size;
            const esp_partition_pos_t partition_position = {
                .offset = running_partition->address,
                .size = running_partition->size,
            };
            esp_image_metadata_t image_metadata = {};
            const esp_err_t metadata_error =
                esp_image_get_metadata(&partition_position, &image_metadata);
            record_first_error(metadata_error, &memory_error);
            if (metadata_error == ESP_OK) {
                memory.firmware_image_bytes = image_metadata.image_len;
            }
        } else {
            record_first_error(ESP_ERR_NOT_FOUND, &memory_error);
        }
        if (memory_error == ESP_OK) {
            s_static_memory_metadata = {
                .ready = true,
                .flash_capacity_bytes = memory.flash_capacity_bytes,
                .app_partition_bytes = memory.app_partition_bytes,
                .firmware_image_bytes = memory.firmware_image_bytes,
            };
        }
    }

    constexpr uint32_t internal_caps =
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    multi_heap_info_t default_heap_info = {};
    multi_heap_info_t internal_heap_info = {};
    multi_heap_info_t psram_heap_info = {};
    heap_caps_get_info(&default_heap_info, MALLOC_CAP_DEFAULT);
    heap_caps_get_info(&internal_heap_info, internal_caps);
    heap_caps_get_info(&psram_heap_info, MALLOC_CAP_SPIRAM);

    const auto heap_info_is_valid = [](const multi_heap_info_t &info) {
        return info.largest_free_block <= info.total_free_bytes &&
               info.minimum_free_bytes <= info.total_free_bytes &&
               info.allocated_blocks <= info.total_blocks &&
               info.free_blocks <= info.total_blocks;
    };
    if (!heap_info_is_valid(default_heap_info) ||
        !heap_info_is_valid(internal_heap_info) ||
        !heap_info_is_valid(psram_heap_info)) {
        record_first_error(ESP_ERR_INVALID_RESPONSE, &memory_error);
    }

    const size_t default_heap_total =
        heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    const size_t internal_heap_total =
        heap_caps_get_total_size(internal_caps);
    const size_t psram_heap_total =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_capacity = esp_psram_get_size();
    if (default_heap_total == 0U ||
        internal_heap_total == 0U ||
        psram_heap_total == 0U ||
        psram_capacity == 0U) {
        record_first_error(ESP_ERR_INVALID_RESPONSE, &memory_error);
    }

    memory.heap_total_bytes = static_cast<uint32_t>(default_heap_total);
    memory.heap_free_bytes =
        static_cast<uint32_t>(default_heap_info.total_free_bytes);
    memory.heap_minimum_free_bytes =
        static_cast<uint32_t>(default_heap_info.minimum_free_bytes);
    memory.internal_heap_total_bytes =
        static_cast<uint32_t>(internal_heap_total);
    memory.internal_free_bytes =
        static_cast<uint32_t>(internal_heap_info.total_free_bytes);
    memory.internal_largest_block_bytes =
        static_cast<uint32_t>(internal_heap_info.largest_free_block);
    memory.internal_minimum_free_bytes =
        static_cast<uint32_t>(internal_heap_info.minimum_free_bytes);
    memory.internal_allocated_blocks =
        static_cast<uint32_t>(internal_heap_info.allocated_blocks);
    memory.internal_free_blocks =
        static_cast<uint32_t>(internal_heap_info.free_blocks);
    memory.internal_total_blocks =
        static_cast<uint32_t>(internal_heap_info.total_blocks);
    memory.internal_memory_pressure =
        memory.internal_free_bytes < 32768U ||
        memory.internal_largest_block_bytes < 16384U;
    memory.psram_capacity_bytes = static_cast<uint32_t>(psram_capacity);
    memory.psram_heap_total_bytes =
        static_cast<uint32_t>(psram_heap_total);
    memory.psram_free_bytes =
        static_cast<uint32_t>(psram_heap_info.total_free_bytes);
    if (memory.heap_free_bytes > memory.heap_total_bytes ||
        memory.internal_free_bytes > memory.internal_heap_total_bytes ||
        memory.psram_free_bytes > memory.psram_heap_total_bytes) {
        record_first_error(ESP_ERR_INVALID_RESPONSE, &memory_error);
    } else {
        memory.psram_used_bytes =
            memory.psram_heap_total_bytes - memory.psram_free_bytes;
    }
    memory.psram_largest_block_bytes =
        static_cast<uint32_t>(psram_heap_info.largest_free_block);
    memory.psram_minimum_free_bytes =
        static_cast<uint32_t>(psram_heap_info.minimum_free_bytes);
    memory.psram_allocated_blocks =
        static_cast<uint32_t>(psram_heap_info.allocated_blocks);
    memory.psram_free_blocks =
        static_cast<uint32_t>(psram_heap_info.free_blocks);
    memory.psram_total_blocks =
        static_cast<uint32_t>(psram_heap_info.total_blocks);
    memory.allocation_failure_monitor_active =
        s_memory_telemetry_active.load(std::memory_order_acquire);
    memory.allocation_failure_count =
        s_allocation_failure_count;
    __asm__ __volatile__("memw" ::: "memory");
    memory.largest_failed_allocation_bytes =
        s_largest_failed_allocation_bytes;
    memory.failed_allocation_caps_seen_mask =
        s_failed_allocation_caps_seen_mask;
    if (!memory.allocation_failure_monitor_active) {
        record_first_error(ESP_ERR_INVALID_STATE, &memory_error);
    }
    const int64_t memory_completed_at_us = esp_timer_get_time();

    if (s_static_processor_metadata.ready) {
        processor.core_count = s_static_processor_metadata.core_count;
    } else {
        esp_chip_info_t chip = {};
        esp_chip_info(&chip);
        processor.core_count = static_cast<uint8_t>(chip.cores);
        if (chip.cores < 1 || chip.cores > 2) {
            record_first_error(
                ESP_ERR_INVALID_RESPONSE,
                &processor_error
            );
        }
        if (processor_error == ESP_OK) {
            s_static_processor_metadata = {
                .ready = true,
                .core_count = processor.core_count,
            };
        }
    }
    const int core_count = processor.core_count;
    const int sampling_core = esp_cpu_get_core_id();
    if (sampling_core < 0 || sampling_core >= core_count ||
        sampling_core >= 2) {
        record_first_error(
            ESP_ERR_INVALID_RESPONSE,
            &processor_error
        );
    } else {
        processor.sampling_core = static_cast<uint8_t>(sampling_core);
    }

    if (processor_error == ESP_OK) {
        std::lock_guard<std::mutex> lock(s_processor_usage_mutex);
        ProcessorRuntimeSample runtime_samples[2];
        for (int core = 0; core < core_count; ++core) {
            ProcessorRuntimeSample &sample = runtime_samples[core];
            sample.requested_core = core;
            const esp_err_t ipc_error = esp_ipc_call_blocking(
                core, capture_processor_runtime_on_core, &sample
            );
            const esp_err_t capture_error = ipc_error != ESP_OK ? ipc_error : sample.error;
            if (capture_error != ESP_OK) {
                ESP_LOGE(TAG, "CPU core %d runtime capture failed (%s)",
                         core, esp_err_to_name(capture_error));
                record_first_error(capture_error, &processor_error);
                break;
            }
            processor.cores[core].available = true;
        }
        if (processor_error == ESP_OK && s_processor_usage.initialized) {
            for (int core = 0; core < core_count; ++core) {
                const ProcessorRuntimeSample &sample = runtime_samples[core];
                const configRUN_TIME_COUNTER_TYPE elapsed =
                    sample.sampled_at - s_processor_usage.sampled_at[core];
                if (elapsed == 0) {
                    ESP_LOGE(TAG, "CPU core %d runtime interval is zero (%s)",
                             core, esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                    record_first_error(ESP_ERR_INVALID_RESPONSE, &processor_error);
                    break;
                }
                const configRUN_TIME_COUNTER_TYPE idle_elapsed =
                    sample.idle_runtime -
                    s_processor_usage.idle_runtime[core];
                if (idle_elapsed > elapsed) {
                    ESP_LOGE(
                        TAG,
                        "CPU core %d sample rejected: idle=%llu elapsed=%llu",
                        core,
                        static_cast<unsigned long long>(idle_elapsed),
                        static_cast<unsigned long long>(elapsed)
                    );
                    record_first_error(
                        ESP_ERR_INVALID_RESPONSE,
                        &processor_error
                    );
                    break;
                }
                LUNASHELL::CoreSnapshot &core_snapshot =
                    processor.cores[core];
                core_snapshot.utilization_percent =
                    static_cast<uint8_t>(
                        ((elapsed - idle_elapsed) * 100ULL +
                         elapsed / 2ULL) /
                        elapsed
                    );
                core_snapshot.utilization_available = true;
            }
        }
        if (processor_error == ESP_OK) {
            s_processor_usage.initialized = true;
            for (int core = 0; core < core_count; ++core) {
                s_processor_usage.sampled_at[core] = runtime_samples[core].sampled_at;
                s_processor_usage.idle_runtime[core] = runtime_samples[core].idle_runtime;
            }
        }
    }

    if (processor_error == ESP_OK) {
        TaskHandle_t task = xTaskGetCurrentTaskHandle();
        if (task == nullptr) {
            record_first_error(ESP_ERR_NOT_FOUND, &processor_error);
        } else {
            const char *task_name = pcTaskGetName(task);
            if (task_name == nullptr || task_name[0] == '\0') {
                record_first_error(
                    ESP_ERR_INVALID_RESPONSE,
                    &processor_error
                );
            } else {
                LUNASHELL::CoreSnapshot &sampled_core =
                    processor.cores[processor.sampling_core];
                strlcpy(
                    sampled_core.current_task,
                    task_name,
                    sizeof(sampled_core.current_task)
                );
                sampled_core.priority = uxTaskPriorityGet(task);
                sampled_core.stack_high_water_words =
                    uxTaskGetStackHighWaterMark(task);
            }
        }
    }
    const int64_t processor_completed_at_us = esp_timer_get_time();

    if (memory_error == ESP_OK) {
        mark_sample_success(
            memory,
            attempted_at_us,
            memory_completed_at_us
        );
    }
    if (processor_error == ESP_OK) {
        mark_sample_success(
            processor,
            attempted_at_us,
            processor_completed_at_us
        );
    }
    const esp_err_t publish_error = update_snapshot(
        [&memory, &processor, memory_error, processor_error, attempted_at_us](
            LUNASHELL::DeviceSnapshot &snapshot
        ) {
            if (memory_error == ESP_OK) {
                snapshot.memory = memory;
            } else {
                mark_sample_failure(
                    snapshot.memory,
                    memory_error,
                    attempted_at_us
                );
            }
            if (processor_error == ESP_OK) {
                snapshot.processor = processor;
            } else {
                mark_sample_failure(
                    snapshot.processor,
                    processor_error,
                    attempted_at_us
                );
            }
        }
    );

    if (memory_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Memory sample failed (%s)",
            esp_err_to_name(memory_error)
        );
    }
    if (processor_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Processor sample failed (%s)",
            esp_err_to_name(processor_error)
        );
    }
    if (publish_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "System-state publication failed (%s)",
            esp_err_to_name(publish_error)
        );
    }

    esp_err_t aggregate = ESP_OK;
    record_first_error(memory_error, &aggregate);
    record_first_error(processor_error, &aggregate);
    record_first_error(publish_error, &aggregate);
    return aggregate;
}

}  // namespace LUNASHELL::snapshot_detail::system_acquisition

using namespace LUNASHELL::snapshot_detail::system_acquisition;

// No parameters: register the single allocation failure observer once.
// Exception: Returns the actual registration error.
extern "C" esp_err_t LUNASHELL_memory_telemetry_init(void)
{
    bool expected = false;
    if (!s_memory_telemetry_active.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        return ESP_OK;
    }
    const esp_err_t error =
        heap_caps_register_failed_alloc_callback(allocation_failed_hook);
    if (error != ESP_OK) {
        s_memory_telemetry_active.store(false, std::memory_order_release);
        return error;
    }
    return ESP_OK;
}
