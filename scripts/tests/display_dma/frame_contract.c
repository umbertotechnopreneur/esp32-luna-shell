// Deterministic transport boundary for the extracted production BSP functions.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bsp_display_dma.h"

#define CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY 1
#define CONFIG_LUNASHELL_BUILD_ENABLE_UI 0
#define IRAM_ATTR
#define DMA_ATTR _Alignas(64)
#define __attribute__(attribute)
#define MALLOC_CAP_DMA 1U
#define MALLOC_CAP_INTERNAL 2U
#define MALLOC_CAP_8BIT 4U
#define __ATOMIC_ACQUIRE 0
#define __ATOMIC_RELEASE 0
#define __atomic_load_n(pointer, order) (*(pointer))
#define __atomic_store_n(pointer, value, order) (*(pointer) = (value))
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef struct { int unused; } esp_lcd_panel_io_event_data_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;

static bool display_completion_fault;
static bool display_dma_faulted;
static bool display_panel_output_enabled = true;
static bool display_rotation_consistent = true;
static bsp_display_dma_lease_t display_dma_fault_lease;
static uint32_t display_dma_retained_bytes;
static SemaphoreHandle_t display_completion = (void *)1;
static void *panel_handle = (void *)2;
static _Alignas(64) uint8_t pool[32768];
static _Alignas(64) uint8_t second_pool[32768];
static TaskHandle_t current_task = (void *)3;
static int64_t now_us;
static unsigned lock_depth;
static unsigned releases;
static size_t available_slots = 2;
static size_t available_rows = 16;
static bool fragmented_heap;
static bool fail_second_allocation;
static bool first_used;
static bool second_used;
static bool panel_ready = true;
static bool token;
static bool drop_completion;
static bool dma_active;
static bool payload_changed;
static const uint8_t *dma_pixels;
static size_t dma_bytes;
static uint32_t dma_hash;
static int64_t dma_completes_at_us;
static esp_err_t submission_error;
static const char *TAG = "HOST_BSP";

// tag: Actual production component label, retained at the host log boundary.
// format: Actual production failure description; values are not needed by the fixture.
static void host_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}
// error: Actual production error used in the source's diagnostic paths.
static const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "HOST_ERROR";
}

// No parameters: fake task clock; elapsed time is advanced explicitly by the test.
static int64_t esp_timer_get_time(void) { return now_us; }
// No parameters: caller identity is changed only in ownership rejection cases.
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
// No parameters: model panel initialization without probing physical hardware.
static bool bsp_display_is_ready(void) { return panel_ready; }
// timeout_ms: Ignored by the uncontended host lock, while DMA quarantine still rejects it.
static esp_err_t bsp_display_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (display_dma_faulted) { return ESP_ERR_INVALID_STATE; }
    ++lock_depth;
    return ESP_OK;
}
// No parameters: require balanced ownership on every tested exit.
static void bsp_display_unlock(void) { assert(lock_depth > 0); --lock_depth; }
// semaphore: The production ISR's single binary completion token.
// awakened: Receives a successful wake only when the token was previously absent.
static BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *awakened)
{
    assert(semaphore == display_completion);
    if (token) { return pdFALSE; }
    token = true;
    *awakened = pdTRUE;
    return pdTRUE;
}
// semaphore: Binary completion signal used by the actual production wait.
// ticks: Bounded virtual milliseconds; failed completion never gets an invented token.
static BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
// panel: Exact model panel handle used by production submission.
// x_start: Included column controlling the actual packed payload size.
// y_start: Included row controlling the actual packed payload size.
// x_end: Exclusive column controlling the actual packed payload size.
// y_end: Exclusive row controlling the actual packed payload size.
// pixels: Borrowed memory retained unchanged until simulated hardware completion.
static esp_err_t esp_lcd_panel_draw_bitmap(void *panel, int x_start, int y_start,
                                         int x_end, int y_end, const void *pixels);
// capabilities: Real allocator's required internal/DMA/byte-access capabilities.
static size_t heap_caps_get_largest_free_block(uint32_t capabilities);
// capabilities: Required allocation capabilities, unchanged by the host boundary.
static size_t heap_caps_get_free_size(uint32_t capabilities);
// capabilities: Required allocation capabilities, used only by emergency logging.
static size_t heap_caps_get_minimum_free_size(uint32_t capabilities);
// alignment: Actual requested alignment, checked against the production budget.
// bytes: Actual allocation size, retained per modeled block.
// capabilities: Actual internal DMA requirement, never a PSRAM allocation.
static void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, uint32_t capabilities);
// allocation: Owned block, never released while the simulated peripheral still reads it.
static void heap_caps_free(void *allocation);
// allocation: Host pointer checked only as an SDK capability boundary.
static bool esp_ptr_internal(const void *allocation) { return allocation != NULL; }
// allocation: Host pointer checked only as an SDK capability boundary.
static bool esp_ptr_dma_capable(const void *allocation) { return allocation != NULL; }

#define TAG DMA_OWNER_TAG
#include "dma_owner_under_test.inc"
#undef TAG
#include "driver_under_test.inc"

// pixels: Byte sequence whose immutability is checked at hardware retirement.
// bytes: Actual submitted payload, independent of slot alignment padding.
static uint32_t hash_pixels(const uint8_t *pixels, size_t bytes)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0; index < bytes; ++index) {
        hash = (hash ^ pixels[index]) * UINT32_C(16777619);
    }
    return hash;
}
// elapsed_us: CPU work or semaphore wait advanced on the virtual hardware clock.
static void advance_time(int64_t elapsed_us)
{
    now_us += elapsed_us;
    if (dma_active && !drop_completion && now_us >= dma_completes_at_us) {
        payload_changed |= hash_pixels(dma_pixels, dma_bytes) != dma_hash;
        dma_active = false;
        bsp_display_color_transfer_done(NULL, NULL, display_completion);
    }
}
// semaphore: Exact binary signal associated with the fake panel transport.
// ticks: Maximum virtual milliseconds consumed by this wait.
static BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    assert(semaphore == display_completion);
    if (!token) {
        int64_t wait_us = (int64_t)ticks * 1000;
        if (dma_active && !drop_completion && dma_completes_at_us - now_us < wait_us) {
            wait_us = dma_completes_at_us - now_us;
        }
        advance_time(wait_us > 0 ? wait_us : 0);
    }
    if (!token) { return pdFALSE; }
    token = false;
    return pdTRUE;
}
// panel: Exact model panel handle used by production submission.
// x_start: Included column controlling the actual packed payload size.
// y_start: Included row controlling the actual packed payload size.
// x_end: Exclusive column controlling the actual packed payload size.
// y_end: Exclusive row controlling the actual packed payload size.
// pixels: Borrowed memory retained unchanged until simulated hardware completion.
static esp_err_t esp_lcd_panel_draw_bitmap(void *panel, int x_start, int y_start,
                                         int x_end, int y_end, const void *pixels)
{
    assert(panel == panel_handle && !dma_active && !token);
    dma_pixels = pixels;
    dma_bytes = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * 2;
    dma_hash = hash_pixels(dma_pixels, dma_bytes);
    dma_active = true;
    now_us += 25;
    dma_completes_at_us = now_us + 2400;
    return submission_error;
}
// capabilities: Production internal/DMA/byte-access flags; fixture models only those blocks.
static size_t heap_caps_get_largest_free_block(uint32_t capabilities)
{
    assert(capabilities == (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (available_slots == 1) {
        return first_used ? 0 : BSP_DISPLAY_DMA_ALIGNED_BYTES(BSP_DISPLAY_DMA_ROW_BYTES * available_rows) +
            BSP_DISPLAY_DMA_HEADROOM_BYTES;
    }
    if (fragmented_heap) { return first_used && second_used ? 0 : 21504; }
    return first_used ? 0 : 60000;
}
// capabilities: Same flags as the production largest-block query.
static size_t heap_caps_get_free_size(uint32_t capabilities)
{
    (void)capabilities;
    return 60000 - (first_used ? 14912 : 0) - (second_used ? 14912 : 0);
}
// capabilities: Same flags as the production aggregate query.
static size_t heap_caps_get_minimum_free_size(uint32_t capabilities)
{
    return heap_caps_get_free_size(capabilities);
}
// alignment: Actual requested alignment, retained by each modeled block.
// bytes: Requested production size; the fake fragmented heap cannot hold a full 32-row block.
// capabilities: Actual internal DMA requirement, never changed to PSRAM by the test.
static void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, uint32_t capabilities)
{
    assert(alignment == 64 && bytes <= sizeof(pool));
    assert(bytes <= heap_caps_get_largest_free_block(capabilities));
    if (!first_used) { first_used = true; return pool; }
    if (!second_used && !fail_second_allocation) { second_used = true; return second_pool; }
    return NULL;
}
// allocation: Actual owner release; block state is checked before it is returned to the heap.
static void heap_caps_free(void *allocation)
{
    assert(!dma_active && !payload_changed);
    if (allocation == pool) { assert(first_used); first_used = false; }
    else { assert(allocation == second_pool && second_used); second_used = false; }
    ++releases;
}
// No parameters: an explicit virtual reset is the only way to discard quarantine.
static void reset_fixture(void)
{
    assert(lock_depth == 0);
    memset(&display_frame, 0, sizeof(display_frame));
    memset(&display_dma_fault_lease, 0, sizeof(display_dma_fault_lease));
    memset(pool, 0, sizeof(pool));
    display_dma_faulted = false;
    display_completion_fault = false;
    display_dma_retained_bytes = 0;
    dma_active = token = drop_completion = payload_changed = false;
    panel_ready = display_panel_output_enabled = true;
    current_task = (void *)3;
    available_slots = 2;
    available_rows = 16;
    fragmented_heap = fail_second_allocation = first_used = second_used = false;
    releases = 0;
    now_us = 1000;
    submission_error = ESP_OK;
}
// No parameters: borrow through the real production function with actual capacity assertions.
static void *borrow(void)
{
    void *pixels = NULL;
    size_t bytes = 0;
    assert(bsp_display_frame_buffer(&pixels, &bytes) == ESP_OK);
    assert(bytes == BSP_DISPLAY_DMA_ROW_BYTES * available_rows);
    return pixels;
}
// fragmented: Force the actual allocator to obtain two disjoint 14.9-KiB blocks.
static void normal_pipeline(bool fragmented)
{
    reset_fixture();
    fragmented_heap = fragmented;
    assert(bsp_display_frame_begin(500) == ESP_OK);
    assert((display_frame.lease.second_data != NULL) == fragmented);
    void *first = borrow();
    memset(first, 0x37, 16 * BSP_DISPLAY_DMA_ROW_BYTES);
    assert(bsp_display_frame_submit_rgb565(0, 0, 466, 16, first) == ESP_OK);
    void *second = borrow();
    assert(second != first && dma_active);
    memset(second, 0xA9, 16 * BSP_DISPLAY_DMA_ROW_BYTES);
    advance_time(4600);
    assert(!dma_active && !payload_changed);
    assert(bsp_display_frame_submit_rgb565(0, 16, 466, 32, second) == ESP_OK);
    assert(borrow() == first && dma_active);
    memset(first, 0xD4, 16 * BSP_DISPLAY_DMA_ROW_BYTES);
    assert(bsp_display_frame_submit_rgb565(0, 32, 466, 48, first) == ESP_OK);
    bsp_display_frame_result_t result;
    int64_t end_started = now_us;
    assert(bsp_display_frame_end(ESP_OK, &result) == ESP_OK);
    assert(now_us - end_started == 2400 && !dma_active && !payload_changed);
    assert(result.buffer_count == 2 && result.transfer_rows == 16 && result.rectangles == 3);
    assert(result.completion_wait_us == 4800 && result.completion_error == ESP_OK);
    assert(result.retained_dma_bytes == 0 && releases == (fragmented ? 2U : 1U) && lock_depth == 0);
    puts(fragmented ? "PASS fragmented heap: two disjoint slots, overlap, immutable payload, final drain" :
                      "PASS contiguous double-slot overlap, alternation, immutable payload, final drain");
}
// No parameters: low-memory degradation preserves ownership and reports actual capacity.
static void single_slot(void)
{
    reset_fixture();
    available_slots = 1;
    available_rows = 4;
    assert(bsp_display_frame_begin(500) == ESP_OK);
    void *first = borrow();
    assert(bsp_display_frame_submit_rgb565(0, 0, 466, 4, first) == ESP_OK);
    int64_t before = now_us;
    assert(borrow() == first && now_us - before == 2400 && !dma_active);
    bsp_display_frame_result_t result;
    assert(bsp_display_frame_end(ESP_OK, &result) == ESP_OK);
    assert(result.buffer_count == 1 && result.transfer_rows == 4 && releases == 1);
    puts("PASS one-slot reduced-row degradation waits before reuse");
}
// No parameters: a render failure still drains submitted pixels while keeping its original error.
static void renderer_failure(void)
{
    reset_fixture();
    assert(bsp_display_frame_begin(500) == ESP_OK);
    assert(bsp_display_frame_submit_rgb565(20, 40, 120, 42, borrow()) == ESP_OK);
    bsp_display_frame_result_t result;
    assert(bsp_display_frame_end(ESP_FAIL, &result) == ESP_FAIL);
    assert(result.error == ESP_FAIL && result.completion_error == ESP_OK);
    assert(!dma_active && releases == 1 && lock_depth == 0);
    puts("PASS renderer failure drains queued pixels and preserves original error");
}
// queue_error: When true, submission fails after an uncertain enqueue; otherwise completion is lost.
static void quarantined_failure(bool queue_error)
{
    reset_fixture();
    fragmented_heap = true;
    assert(bsp_display_frame_begin(10) == ESP_OK);
    void *first = borrow();
    if (queue_error) { submission_error = ESP_FAIL; }
    else { drop_completion = true; }
    esp_err_t submitted = bsp_display_frame_submit_rgb565(0, 0, 466, 16, first);
    assert(submitted == (queue_error ? ESP_FAIL : ESP_OK));
    bsp_display_frame_result_t result;
    esp_err_t expected = queue_error ? ESP_FAIL : ESP_ERR_TIMEOUT;
    assert(bsp_display_frame_end(ESP_OK, &result) == expected);
    assert(result.completion_error == expected && display_dma_faulted && dma_active);
    assert(result.retained_dma_bytes == 29824 && display_dma_fault_lease.data == pool);
    assert(display_dma_fault_lease.second_data == second_pool && first_used && second_used);
    assert(display_frame.lease.data == NULL && releases == 0 && lock_depth == 0);
    assert(bsp_display_frame_begin(500) == ESP_ERR_INVALID_STATE);
    puts(queue_error ? "PASS uncertain SDK submission retains both slots until reset" :
                       "PASS lost completion retains both slots until reset");
}
// No parameters: finished DMA may be retired after a missed deadline without retaining idle memory.
static void deadline_and_irq_fault(void)
{
    reset_fixture();
    assert(bsp_display_frame_begin(10) == ESP_OK);
    assert(bsp_display_frame_submit_rgb565(0, 0, 466, 16, borrow()) == ESP_OK);
    advance_time(11000);
    bsp_display_frame_result_t result;
    assert(bsp_display_frame_end(ESP_OK, &result) == ESP_ERR_TIMEOUT);
    assert(result.completion_error == ESP_OK && releases == 1 && !display_dma_faulted);
    reset_fixture();
    assert(bsp_display_frame_begin(500) == ESP_OK);
    assert(bsp_display_frame_submit_rgb565(0, 0, 466, 16, borrow()) == ESP_OK);
    advance_time(3000);
    bsp_display_color_transfer_done(NULL, NULL, display_completion);
    assert(bsp_display_frame_end(ESP_OK, &result) == ESP_ERR_INVALID_STATE);
    assert(result.completion_error == ESP_ERR_INVALID_STATE && releases == 0);
    puts("PASS deadline failure differs from uncertain DMA; duplicate IRQ stays a real failure");
}
// No parameters: reject foreign/interior/active slots and invalid geometry without an enqueue.
static void invalid_inputs(void)
{
    reset_fixture();
    assert(bsp_display_frame_begin(0) == ESP_ERR_INVALID_ARG);
    assert(bsp_display_frame_begin(500) == ESP_OK);
    assert(bsp_display_frame_begin(500) == ESP_ERR_INVALID_STATE);
    void *first = borrow();
    assert(bsp_display_frame_submit_rgb565(-1, 0, 10, 2, first) == ESP_ERR_INVALID_ARG);
    assert(bsp_display_frame_submit_rgb565(0, 0, 467, 2, first) == ESP_ERR_INVALID_ARG);
    assert(bsp_display_frame_submit_rgb565(0, 0, 466, 17, first) == ESP_ERR_INVALID_ARG);
    assert(bsp_display_frame_submit_rgb565(0, 0, 10, 2, pool + 2) == ESP_ERR_INVALID_ARG);
    current_task = (void *)4;
    void *pixels = (void *)1;
    size_t capacity = 123;
    assert(bsp_display_frame_buffer(&pixels, &capacity) == ESP_ERR_INVALID_STATE);
    assert(pixels == NULL && capacity == 0);
    current_task = (void *)3;
    assert(bsp_display_frame_submit_rgb565(0, 0, 10, 2, first) == ESP_OK);
    assert(bsp_display_frame_submit_rgb565(0, 2, 10, 4, first) == ESP_ERR_INVALID_ARG);
    bsp_display_frame_result_t result;
    assert(bsp_display_frame_end(ESP_OK, &result) == ESP_OK && result.rectangles == 1);
    puts("PASS invalid geometry, nested/foreign owner and foreign/active slots reject safely");
}
// No parameters: a synchronous write inside an async frame must drain before converting shared storage.
static void synchronous_interleave(void)
{
    reset_fixture();
    fragmented_heap = true;
    assert(bsp_display_frame_begin(500) == ESP_OK);
    assert(bsp_display_frame_submit_rgb565(0, 0, 466, 16, borrow()) == ESP_OK);
    uint8_t *second = borrow();
    second[0] = 0x34;
    second[1] = 0x12;
    assert(bsp_display_write_rgb565(0, 16, 2, 18, second,
                                  BSP_DISPLAY_RGB565_BYTE_ORDER_NATIVE) == ESP_OK);
    assert(!payload_changed && !dma_active && pool[0] == 0x12 && pool[1] == 0x34);
    bsp_display_frame_result_t result;
    assert(bsp_display_frame_end(ESP_OK, &result) == ESP_OK && releases == 2);
    puts("PASS synchronous/native-order interleave retires DMA before conversion/reuse");
}
// No parameters: partial allocation is released before admitting a safe one-slot lease.
static void partial_allocation(void)
{
    reset_fixture();
    fragmented_heap = true;
    fail_second_allocation = true;
    assert(bsp_display_frame_begin(500) == ESP_OK);
    assert(display_frame.lease.buffer_count == 1 && display_frame.lease.second_data == NULL);
    assert(releases == 1 && first_used && !second_used);
    bsp_display_frame_result_t result;
    assert(bsp_display_frame_end(ESP_OK, &result) == ESP_OK && releases == 2);
    puts("PASS failed second allocation frees partial ownership before single-slot admission");
}
// No parameters: run the actual driver lifetime contracts without a device or fabricated sensors.
int main(void)
{
    normal_pipeline(false);
    normal_pipeline(true);
    single_slot();
    renderer_failure();
    quarantined_failure(false);
    quarantined_failure(true);
    deadline_and_irq_fault();
    invalid_inputs();
    synchronous_interleave();
    partial_allocation();
    return 0;
}
