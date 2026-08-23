#include "LUNASHELL_liveness.h"

#include <array>
#include <cstdlib>
#include <cstddef>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if !CONFIG_ESP_TASK_WDT_EN || !CONFIG_ESP_TASK_WDT_INIT || \
    !CONFIG_ESP_TASK_WDT_PANIC
#error "LUNASHELL requires the auto-initialized panic Task WDT"
#endif

namespace {

constexpr char kTag[] = "LUNASHELL_liveness";
constexpr TickType_t kLockTimeout = pdMS_TO_TICKS(100);
constexpr uint32_t kTraceMagic = 0x4C574454U;  // "LWDT"
constexpr uint32_t kTraceVersion = 1U;
constexpr uint32_t kNoPoint =
    static_cast<uint32_t>(LUNASHELL_LIVENESS_POINT_COUNT);
static_assert(
    LUNASHELL_LIVENESS_POINT_COUNT <= 32,
    "Liveness active-mask capacity exceeded"
);

struct WatchSlot {
    const char *name;
    esp_task_wdt_user_handle_t handle;
    bool active;
};

struct RetainedTimeoutTrace {
    uint32_t magic;
    uint32_t version;
    uint32_t operation_sequence;
    uint32_t active_mask;
    uint32_t last_progress_point;
    uint32_t integrity;
};

// ESP-IDF retains these pointers in the Task-WDT registry and may print them
// from timeout context. Keep the backing text in internal DRAM, not flash.
DRAM_ATTR char s_gui_watch_name[] = "gui-progress";
DRAM_ATTR char s_display_watch_name[] = "lvgl-worker";
DRAM_ATTR char s_settings_watch_name[] = "settings-nvs";
DRAM_ATTR char s_provisioning_watch_name[] = "provisioning";
DRAM_ATTR char s_boot_audio_watch_name[] = "boot-audio";
DRAM_ATTR char s_snapshot_watch_name[] = "snapshot";
DRAM_ATTR char s_radios_watch_name[] = "radios";
DRAM_ATTR char s_wifi_watch_name[] = "wifi-driver";
DRAM_ATTR char s_wifi_scan_watch_name[] = "wifi-scan";
DRAM_ATTR char s_wifi_events_watch_name[] = "wifi-events";
DRAM_ATTR char s_startup_watch_name[] = "startup";

std::array<WatchSlot, LUNASHELL_LIVENESS_POINT_COUNT> s_slots{{
    {s_gui_watch_name, nullptr, false},
    {s_display_watch_name, nullptr, false},
    {s_settings_watch_name, nullptr, false},
    {s_provisioning_watch_name, nullptr, false},
    {s_boot_audio_watch_name, nullptr, false},
    {s_snapshot_watch_name, nullptr, false},
    {s_radios_watch_name, nullptr, false},
    {s_wifi_watch_name, nullptr, false},
    {s_wifi_scan_watch_name, nullptr, false},
    {s_wifi_events_watch_name, nullptr, false},
    {s_startup_watch_name, nullptr, false},
}};

StaticSemaphore_t s_lock_storage;
SemaphoreHandle_t s_lock;
bool s_initialized;
bool s_last_progress_valid;
TickType_t s_last_progress_tick;
esp_err_t s_last_error = ESP_ERR_INVALID_STATE;
LUNASHELL_liveness_timeout_trace_t s_previous_timeout{};

// These 32-bit values are copied directly by the Task-WDT ISR. They must not
// require a C++ runtime helper, heap access, locks, or flash-resident code.
DRAM_ATTR volatile uint32_t s_active_mask;
DRAM_ATTR volatile uint32_t s_operation_sequence;
DRAM_ATTR volatile uint32_t s_last_progress_point = kNoPoint;
RTC_NOINIT_ATTR volatile RetainedTimeoutTrace s_retained_timeout;

bool point_valid(LUNASHELL_liveness_point_t point)
{
    return point >= LUNASHELL_LIVENESS_GUI &&
           point < LUNASHELL_LIVENESS_POINT_COUNT;
}

uint32_t point_mask(LUNASHELL_liveness_point_t point)
{
    return 1U << static_cast<uint32_t>(point);
}

uint32_t trace_integrity(
    uint32_t operation_sequence,
    uint32_t active_mask,
    uint32_t last_progress_point
)
{
    return kTraceMagic ^ kTraceVersion ^ operation_sequence ^ active_mask ^
           last_progress_point;
}

bool retained_trace_valid()
{
    return s_retained_timeout.magic == kTraceMagic &&
           s_retained_timeout.version == kTraceVersion &&
           s_retained_timeout.integrity == trace_integrity(
               s_retained_timeout.operation_sequence,
               s_retained_timeout.active_mask,
               s_retained_timeout.last_progress_point
           );
}

esp_err_t take_lock(const char *operation)
{
    if (!s_initialized || s_lock == nullptr) {
        ESP_LOGE(kTag, "%s rejected: liveness service is not initialized", operation);
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, kLockTimeout) != pdTRUE) {
        ESP_LOGE(kTag, "%s failed: liveness lock timed out", operation);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void release_lock()
{
    if (s_lock == nullptr || xSemaphoreGive(s_lock) != pdTRUE) {
        ESP_LOGE(kTag, "Liveness lock release invariant failed");
        std::abort();
    }
}

void note_progress(LUNASHELL_liveness_point_t point)
{
    s_last_progress_tick = xTaskGetTickCount();
    s_last_progress_valid = true;
    s_last_progress_point = static_cast<uint32_t>(point);
}

[[noreturn]] void abort_watchdog_invariant(
    const char *operation,
    const char *point_name,
    esp_err_t error
)
{
    ESP_LOGE(
        kTag,
        "%s left watchdog coverage ambiguous for %s: %s",
        operation,
        point_name,
        esp_err_to_name(error)
    );
    esp_system_abort("LUNASHELL liveness invariant failed");
}

uint32_t configured_idle_mask()
{
    uint32_t mask = 0;
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
    mask |= 1U << 0;
#endif
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1 && \
    CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
    mask |= 1U << 1;
#endif
    return mask;
}

void consume_previous_timeout_trace()
{
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    const bool trace_valid = retained_trace_valid();

    if (trace_valid && reset_reason == ESP_RST_TASK_WDT) {
        s_previous_timeout.valid = true;
        s_previous_timeout.operation_sequence =
            s_retained_timeout.operation_sequence;
        s_previous_timeout.active_mask = s_retained_timeout.active_mask;
        const uint32_t point = s_retained_timeout.last_progress_point;
        s_previous_timeout.last_progress_point =
            point < kNoPoint
                ? static_cast<LUNASHELL_liveness_point_t>(point)
                : LUNASHELL_LIVENESS_POINT_COUNT;
        ESP_LOGW(
            kTag,
            "Previous Task-WDT reset: operation=%lu active-mask=0x%08lx "
            "last-progress=%s",
            static_cast<unsigned long>(
                s_previous_timeout.operation_sequence
            ),
            static_cast<unsigned long>(s_previous_timeout.active_mask),
            LUNASHELL_liveness_point_name(
                s_previous_timeout.last_progress_point
            )
        );
    } else if (trace_valid) {
        ESP_LOGW(
            kTag,
            "Discarding stale Task-WDT breadcrumb after reset reason %u",
            static_cast<unsigned>(reset_reason)
        );
    }

    // Consume the record exactly once. A later boot cannot misattribute an old
    // timeout if this boot resets before another Task-WDT interrupt occurs.
    s_retained_timeout.magic = 0;
}

}  // namespace

extern "C" void IRAM_ATTR esp_task_wdt_isr_user_handler(void)
{
    // This is a best-effort diagnostic snapshot. The integrity word rejects
    // interrupted writes to RTC memory; the individual volatile runtime
    // fields can still reflect adjacent progress events, which is acceptable
    // because they are not used for recovery decisions.
    // Publish magic last. If reset interrupts these writes, the next boot will
    // reject the incomplete record instead of reporting a fabricated trace.
    s_retained_timeout.magic = 0;
    s_retained_timeout.version = kTraceVersion;
    s_retained_timeout.operation_sequence = s_operation_sequence;
    s_retained_timeout.active_mask = s_active_mask;
    s_retained_timeout.last_progress_point = s_last_progress_point;
    s_retained_timeout.integrity =
        kTraceMagic ^ kTraceVersion ^
        s_retained_timeout.operation_sequence ^
        s_retained_timeout.active_mask ^
        s_retained_timeout.last_progress_point;
    s_retained_timeout.magic = kTraceMagic;
}

extern "C" const char *LUNASHELL_liveness_point_name(
    LUNASHELL_liveness_point_t point
)
{
    if (!point_valid(point)) {
        return "none";
    }
    return s_slots[static_cast<std::size_t>(point)].name;
}

extern "C" esp_err_t LUNASHELL_liveness_init(void)
{
    if (s_initialized) {
        const esp_err_t status = esp_task_wdt_status(nullptr);
        if (status == ESP_OK || status == ESP_ERR_NOT_FOUND) {
            return ESP_OK;
        }
        ESP_LOGE(
            kTag,
            "Repeated initialization found Task-WDT unavailable: %s",
            esp_err_to_name(status)
        );
        return status;
    }

    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    if (s_lock == nullptr) {
        ESP_LOGE(kTag, "Initialization failed: static mutex creation failed");
        return ESP_ERR_NO_MEM;
    }

    consume_previous_timeout_trace();

    const esp_task_wdt_config_t config{
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U,
        .idle_core_mask = configured_idle_mask(),
        .trigger_panic = true,
    };
    const esp_err_t error = esp_task_wdt_reconfigure(&config);
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Task-WDT policy application failed: %s",
            esp_err_to_name(error)
        );
        s_lock = nullptr;
        return error;
    }

    s_active_mask = 0;
    s_operation_sequence = 0;
    s_last_progress_point = kNoPoint;
    s_last_progress_valid = false;
    s_last_error = ESP_OK;
    s_initialized = true;
    ESP_LOGI(
        kTag,
        "Task-WDT ready: timeout=%lus panic=enabled idle-mask=0x%lx",
        static_cast<unsigned long>(CONFIG_ESP_TASK_WDT_TIMEOUT_S),
        static_cast<unsigned long>(config.idle_core_mask)
    );
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_liveness_watch_begin(
    LUNASHELL_liveness_point_t point
)
{
    if (!point_valid(point)) {
        ESP_LOGE(kTag, "Watch begin rejected: invalid point %d", point);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = take_lock("watch begin");
    if (error != ESP_OK) {
        return error;
    }

    WatchSlot &slot = s_slots[static_cast<std::size_t>(point)];
    if (slot.active) {
        error = ESP_ERR_INVALID_STATE;
        s_last_error = error;
        ESP_LOGE(
            kTag,
            "Watch begin rejected for %s: active=%s",
            slot.name,
            slot.active ? "true" : "false"
        );
        release_lock();
        return error;
    }

    error = esp_task_wdt_add_user(slot.name, &slot.handle);
    if (error != ESP_OK) {
        s_last_error = error;
        ESP_LOGE(
            kTag,
            "Watch subscription failed for %s: %s",
            slot.name,
            esp_err_to_name(error)
        );
        release_lock();
        return error;
    }

    error = esp_task_wdt_reset_user(slot.handle);
    if (error != ESP_OK) {
        const esp_err_t cleanup_error = esp_task_wdt_delete_user(slot.handle);
        if (cleanup_error == ESP_OK) {
            slot.handle = nullptr;
        } else {
            slot.active = true;
            s_active_mask = s_active_mask | point_mask(point);
            abort_watchdog_invariant(
                "Initial-watch rollback",
                slot.name,
                cleanup_error
            );
        }
        s_last_error = error;
        ESP_LOGE(
            kTag,
            "Initial Watchdog feed failed for %s: %s",
            slot.name,
            esp_err_to_name(error)
        );
        release_lock();
        return error;
    }

    slot.active = true;
    s_operation_sequence = s_operation_sequence + 1U;
    s_active_mask = s_active_mask | point_mask(point);
    note_progress(point);
    s_last_error = ESP_OK;
    release_lock();
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_liveness_watch_progress(
    LUNASHELL_liveness_point_t point
)
{
    if (!point_valid(point)) {
        ESP_LOGE(kTag, "Watch progress rejected: invalid point %d", point);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = take_lock("watch progress");
    if (error != ESP_OK) {
        return error;
    }

    WatchSlot &slot = s_slots[static_cast<std::size_t>(point)];
    if (!slot.active || slot.handle == nullptr) {
        error = ESP_ERR_INVALID_STATE;
        s_last_error = error;
        ESP_LOGE(kTag, "Watch progress rejected for inactive %s", slot.name);
        release_lock();
        return error;
    }

    error = esp_task_wdt_reset_user(slot.handle);
    if (error == ESP_OK) {
        note_progress(point);
    } else {
        s_last_error = error;
        abort_watchdog_invariant(
            "Progress feed",
            slot.name,
            error
        );
    }
    release_lock();
    return error;
}

extern "C" esp_err_t LUNASHELL_liveness_watch_end(
    LUNASHELL_liveness_point_t point,
    esp_err_t operation_result
)
{
    if (!point_valid(point)) {
        ESP_LOGE(kTag, "Watch end rejected: invalid point %d", point);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t lock_error = take_lock("watch end");
    if (lock_error != ESP_OK) {
        abort_watchdog_invariant(
            "Watch-end lock",
            LUNASHELL_liveness_point_name(point),
            lock_error
        );
    }

    WatchSlot &slot = s_slots[static_cast<std::size_t>(point)];
    if (!slot.active || slot.handle == nullptr) {
        const esp_err_t state_error = ESP_ERR_INVALID_STATE;
        s_last_error = state_error;
        ESP_LOGE(kTag, "Watch end rejected for inactive %s", slot.name);
        release_lock();
        abort_watchdog_invariant("Watch end", slot.name, state_error);
    }

    const esp_err_t cleanup_error = esp_task_wdt_delete_user(slot.handle);
    if (cleanup_error == ESP_OK) {
        slot.handle = nullptr;
        slot.active = false;
        s_active_mask = s_active_mask & ~point_mask(point);
        note_progress(point);
    } else {
        abort_watchdog_invariant(
            "Watch cleanup",
            slot.name,
            cleanup_error
        );
    }

    if (operation_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Watched operation %s failed: %s",
            slot.name,
            esp_err_to_name(operation_result)
        );
    }
    // The watched component owns and publishes its operation result. Liveness
    // health tracks only failures of the watchdog contract itself.
    s_last_error = cleanup_error;
    release_lock();

    return operation_result != ESP_OK ? operation_result : cleanup_error;
}

extern "C" esp_err_t LUNASHELL_liveness_get_status(
    LUNASHELL_liveness_status_t *status
)
{
    if (status == nullptr) {
        ESP_LOGE(kTag, "Status read rejected: output is null");
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error = take_lock("status read");
    if (error != ESP_OK) {
        return error;
    }

    LUNASHELL_liveness_status_t current{};
    current.initialized = true;
    current.boot_reset_reason = static_cast<uint32_t>(esp_reset_reason());
    current.watchdog_timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U;
    current.operation_sequence = s_operation_sequence;
    current.active_mask = s_active_mask;
    current.last_progress_valid = s_last_progress_valid;
    current.last_progress_point = s_last_progress_valid
                                      ? static_cast<LUNASHELL_liveness_point_t>(
                                            s_last_progress_point
                                        )
                                      : LUNASHELL_LIVENESS_POINT_COUNT;
    current.last_progress_age_ms = s_last_progress_valid
                                       ? static_cast<uint32_t>(
                                             pdTICKS_TO_MS(
                                                 xTaskGetTickCount() -
                                                 s_last_progress_tick
                                             )
                                         )
                                       : 0;
    current.last_error = s_last_error;
    current.previous_timeout = s_previous_timeout;
    *status = current;

    release_lock();
    return ESP_OK;
}
