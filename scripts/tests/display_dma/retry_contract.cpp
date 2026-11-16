// Exercise extracted production deferral and admission policies with the real snapshot schema.
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "esp_err.h"
#include "retry_under_test.inc"

constexpr int64_t kAdmittedAtUs = 1000;
constexpr int64_t kRetiredAtUs = kAdmittedAtUs + kFrameBudgetMs * 1000LL + 2000;

// No parameters: a fresh frame whose budget expired and whose DMA retired successfully.
// Exception: None; all values are synthetic host inputs to the production predicate.
static LUNASHELL::DisplayFrameSnapshot clean_timeout()
{
    LUNASHELL::DisplayFrameSnapshot metrics{};
    metrics.measured = true;
    metrics.failed_frames = 1;
    metrics.deadline_expired = true;
    metrics.last_duration_us = kFrameBudgetMs * 1000U;
    metrics.last_error = ESP_ERR_TIMEOUT;
    metrics.last_completion_error = ESP_OK;
    metrics.completed_at_us = kRetiredAtUs - 1000;
    return metrics;
}

// metrics: Canonical observation returned after this synthetic rendering attempt.
// Exception: Assertion failure if a rejected observation authorizes deferral.
static void rejected(const LUNASHELL::DisplayFrameSnapshot &metrics)
{
    assert(!can_defer_frame_timeout(metrics, ESP_OK, ESP_ERR_TIMEOUT, false,
                                   kAdmittedAtUs, kRetiredAtUs));
}

// No parameters: exercise the scheduled retry boundary and explicit lifecycle exemption.
// Exception: Assertions reject early animation retries or a delayed lifecycle handoff.
static void admission_backoff()
{
    constexpr int64_t retry_started_at_us = 5000;
    constexpr int64_t retry_at_us = retry_started_at_us + 100000;
    assert(frame_retry_pending(RenderAdmission::Scheduled, retry_started_at_us, retry_at_us));
    assert(frame_retry_pending(RenderAdmission::Scheduled, retry_at_us - 1, retry_at_us));
    assert(!frame_retry_pending(RenderAdmission::Scheduled, retry_at_us, retry_at_us));
    assert(!frame_retry_pending(RenderAdmission::Scheduled, retry_at_us + 1, retry_at_us));
    assert(!frame_retry_pending(RenderAdmission::Scheduled, 0, 0));
    assert(!frame_retry_pending(RenderAdmission::Scheduled, retry_started_at_us, 0));
    assert(!frame_retry_pending(RenderAdmission::LifecycleHandoff, retry_started_at_us, retry_at_us));
    std::puts("PASS scheduled frames honor 100-ms retry backoff; lifecycle handoffs remain immediate");
}

// No parameters: test retry admission, certified deferral and independent evidence/ownership failures.
// Exception: Assertions preserve the exact production predicate's rejection behavior.
int main()
{
    admission_backoff();
    const auto clean = clean_timeout();
    assert(can_defer_frame_timeout(clean, ESP_OK, ESP_ERR_TIMEOUT, false,
                                  kAdmittedAtUs, kRetiredAtUs));
    assert(can_defer_frame_timeout(clean, ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT, true,
                                  kAdmittedAtUs, kRetiredAtUs));
    assert(!can_defer_frame_timeout(clean, ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT, false,
                                   kAdmittedAtUs, kRetiredAtUs));
    assert(!can_defer_frame_timeout(clean, ESP_FAIL, ESP_ERR_TIMEOUT, true,
                                   kAdmittedAtUs, kRetiredAtUs));
    assert(!can_defer_frame_timeout(clean, ESP_OK, ESP_OK, false,
                                   kAdmittedAtUs, kRetiredAtUs));
    assert(!can_defer_frame_timeout(clean, ESP_OK, ESP_FAIL, false,
                                   kAdmittedAtUs, kRetiredAtUs));
    assert(!can_defer_frame_timeout(clean, ESP_OK, ESP_ERR_TIMEOUT, false,
                                   0, kRetiredAtUs));
    assert(!can_defer_frame_timeout(clean, ESP_OK, ESP_ERR_TIMEOUT, false,
                                   -1, kRetiredAtUs));
    assert(!can_defer_frame_timeout(clean, ESP_OK, ESP_ERR_TIMEOUT, false,
                                   kAdmittedAtUs, kAdmittedAtUs - 1));

    auto invalid = clean;
    invalid.measured = false;
    rejected(invalid);
    invalid = clean;
    invalid.deadline_expired = false;
    rejected(invalid);
    invalid = clean;
    invalid.completed_at_us = kAdmittedAtUs - 1;
    rejected(invalid);
    invalid = clean;
    invalid.completed_at_us = kAdmittedAtUs;
    rejected(invalid);
    invalid = clean;
    invalid.completed_at_us = kRetiredAtUs + 1;
    rejected(invalid);
    invalid = clean;
    invalid.last_duration_us = kFrameBudgetMs * 1000U - 1U;
    rejected(invalid);
    invalid = clean;
    invalid.last_error = ESP_OK;
    rejected(invalid);
    invalid = clean;
    invalid.last_error = ESP_FAIL;
    rejected(invalid);
    invalid = clean;
    invalid.last_completion_error = ESP_ERR_TIMEOUT;
    rejected(invalid);
    invalid = clean;
    invalid.last_completion_error = ESP_ERR_INVALID_STATE;
    rejected(invalid);
    invalid = clean;
    invalid.reset_required = true;
    rejected(invalid);
    invalid = clean;
    invalid.retained_dma_bytes = 1;
    rejected(invalid);
    invalid = clean;
    invalid.retained_consumer_bytes = 1;
    rejected(invalid);
    invalid = clean;
    invalid.retained_consumer_error = ESP_FAIL;
    rejected(invalid);

    auto boundary = clean;
    boundary.completed_at_us = kRetiredAtUs;
    assert(can_defer_frame_timeout(boundary, ESP_OK, ESP_ERR_TIMEOUT, false,
                                  kAdmittedAtUs, kRetiredAtUs));
    std::puts("PASS only fresh certified frame deadlines with fully retired ownership defer");
    std::puts("PASS raster, transport, stale snapshot and retention errors remain failures");
    return 0;
}
