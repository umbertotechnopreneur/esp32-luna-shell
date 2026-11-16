#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

// Host-only static Studio contract. No firmware enum or mutable owner pointer
// crosses this boundary. Pixels and this identity retire in one display lock.
struct CodyHostStudioFrameIdentity {
    uint32_t abi = 3;
    uint32_t frame_revision = 0;
    uint32_t at_ms = 0;
    uint32_t source_animation_sample_ms = 0;
    uint32_t call_count = 0;
    char target[48]{};
    char scenario_revision[65]{};
    char rendering_mode[24]{};
    uint64_t source_id = 0;
    uint64_t chat_id = 0;
    uint64_t turn_id = 0;
    uint64_t call_ids[3]{};
    int32_t service_error = 0;
    char service_outcome[24]{};
    uint32_t question_count = 0;
    uint64_t question_ids[3]{};
};
static_assert(sizeof(CodyHostStudioFrameIdentity) == 264, "Studio frame identity ABI must remain fixed");

// Route evidence accompanies the destination pixels under the display lock.
// Origin identity was read from the last completed frame before the real input.
struct CodyHostStudioRouteReceipt {
    uint32_t abi = 1;
    uint32_t origin_frame_revision = 0;
    uint32_t destination_frame_revision = 0;
    char origin_scenario_revision[65]{};
    char route_id[32]{};
};

// Explicit bounded request, copied before the display owner processes a gesture.
struct CodyHostStudioRouteRequest {
    uint32_t abi = 1;
    uint32_t size = 0;
    char route_id[32]{};
    char origin_scenario_revision[65]{};
    char destination_scenario_revision[65]{};
};

// A worker acknowledgement describes a real completed frame, not an assumed
// frame at the requested time. Snapshot copies this and pixels under one lock.
struct CodyHostStudioWorkerReceipt {
    uint32_t abi = 1;
    uint32_t frame_revision = 0;
    uint32_t requested_ms = 0;
    uint32_t frame_at_ms = 0;
    uint32_t tick_count = 0;
    char target[48]{};
    char scenario_revision[65]{};
};

struct CodyHostStudioWorkerRequest {
    uint32_t abi = 1;
    uint32_t size = 0;
    uint32_t requested_ms = 0;
    char scenario_revision[65]{};
};

// Explicit synthetic source records. Enum spellings are decoded and checked in
// C++; Python never passes firmware enum ordinals or a pointer to owner memory.
struct CodyHostStudioCall {
    uint64_t id = 0;
    uint64_t turn_id = 0;
    char tool_name[129]{};
    char label[97]{};
    char category[16]{};
    char state[16]{};
    char outcome[16]{};
};

// Explicit pending-question observations, with source-scoped stable parents.
// The canonical query owns Open state; no reply, ticket or delivery is implied.
struct CodyHostStudioQuestion {
    uint64_t id = 0;
    uint64_t chat_id = 0;
    uint64_t turn_id = 0;
    char prompt[129]{};
};

// Live playback observation uses stable textual cue states, not firmware enum ordinals.
struct CodyHostStudioLiveCue {
    uint32_t id = 0;
    uint32_t sequence = 0;
    int32_t error = 0;
    char state[24]{};
};
struct CodyHostStudioLiveStatus {
    uint32_t abi = 1;
    uint32_t animation_complete = 0;
    uint32_t audio_complete = 0;
    uint32_t cue_count = 0;
    int32_t worker_error = 0;
    CodyHostStudioLiveCue cues[8]{};
};

// Bounded canonical account/history inputs. Values are synthetic observations;
// validity masks and source age remain explicit rather than inferred from zero.
struct CodyHostStudioStatistics {
    uint32_t source_age_ms = 0;
    uint64_t daily_tokens[8]{};
    uint32_t daily_valid_mask = 0;
    char credits_kind[16]{};
    int64_t credit_milliunits = 0;
    uint32_t reset_vouchers = 0;
    char plan[25]{};
    char plan_label[25]{};
    char plan_price[25]{};
    uint64_t interval_tokens[96]{};
    uint32_t interval_valid_mask[3]{};
    uint32_t context_used_tokens = 0;
    uint32_t context_capacity_tokens = 0;
    uint64_t context_cumulative_tokens = 0;
    char context_chat_title[129]{};
};

struct CodyHostStudioCase {
    uint32_t abi = 3;
    uint32_t size = 0;
    CodyHostStudioFrameIdentity identity{};
    uint32_t brightness_percent = 80;
    uint32_t auto_rotate = 0;
    uint32_t orientation_degrees = 0;
    uint32_t sounds_enabled = 0;
    uint32_t playback_volume = 90;
    uint32_t quiet_enabled = 0;
    uint32_t quiet_start_minute = 1320;
    uint32_t quiet_end_minute = 420;
    uint32_t quiet_edit_start = 1;
    uint32_t quiet_draft_minute = 1320;
    int64_t weekly_reset_unix_seconds = 0;
    uint32_t weekly_remaining_basis_points = 0;
    uint32_t weekly_source_age_ms = 0;
    int64_t short_reset_unix_seconds = 0;
    uint32_t short_remaining_basis_points = 0;
    uint32_t short_window_minutes = 0;
    uint32_t short_source_age_ms = 0;
    char provisioning_outcome[24]{};
    char codex_link_outcome[16]{};
    uint64_t domain_source_id = 0;
    uint64_t domain_chat_id = 0;
    uint64_t domain_turn_id = 0;
    uint32_t domain_generation = 0;
    uint32_t domain_call_count = 0;
    char domain_chat_title[129]{};
    char domain_execution[16]{};
    CodyHostStudioCall domain_calls[8]{};
    CodyHostStudioStatistics statistics{};
    char domain_questions_outcome[16]{};
    uint32_t domain_question_count = 0;
    CodyHostStudioQuestion domain_questions[3]{};
};

#if defined(_WIN32)
#define CODY_HOST_STUDIO_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_STUDIO_API extern "C" __attribute__((visibility("default")))
#endif

// identity: Actual prepared route and scenario; a null pointer clears staging.
// Exception: No exceptions; called only by the serialized rendering owner.
void cody_host_display_stage_studio_identity(const CodyHostStudioFrameIdentity *identity);

// receipt: Prepared route evidence, or null to clear the next frame's route.
// Exception: No exceptions; staging is completed by frame_end with the pixels.
void cody_host_display_stage_studio_route(const CodyHostStudioRouteReceipt *receipt);

// identity: Receives the last committed Studio identity before an owner gesture.
// Exception: Returns invalid argument or unavailable without modifying the output.
esp_err_t cody_host_display_read_studio_identity(CodyHostStudioFrameIdentity *identity);

// receipt: Acknowledgement for the frame already completed by the worker.
// Exception: Rejects mismatched current frame identity before publishing metadata.
esp_err_t cody_host_display_ack_studio_worker(const CodyHostStudioWorkerReceipt *receipt);

// request: Bounded ABI/version/size-checked synthetic case, copied before enqueue.
// Exception: Returns validation before settings mutation, owner/frame errors or a real wait timeout.
#if defined(CODY_HOST_REAL_AUDIO)
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_prepare_case(const CodyHostStudioCase *request);
// No parameters: exact request capacity for the current host-only ABI.
// Exception: No exceptions.
CODY_HOST_STUDIO_API size_t cody_host_studio_case_size();
// output: Receives three 64-byte captions for healthy, unavailable and recovered local inbox states.
// capacity: Complete destination capacity, at least 192 bytes.
// Exception: Preserves native presenter errors and rejects invalid output capacity.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_notification_subtitles(char *output, size_t capacity);
// level: Bounded synthetic PCM level for an isolated deterministic host test.
// Exception: Rejects invalid levels, active AFE and nonstatic clocks.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_test_microphone_seed(uint32_t level);
// stored: Receives the owner's synthetic microphone level.
// projected: Receives the microphone level in the current motion projection.
// valid: Receives the current projection's microphone-valid flag.
// error: Receives the current projection's microphone error.
// playback_preserved: Receives whether playback facts are unchanged by projection.
// Exception: Rejects missing outputs and preserves original snapshot errors.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_test_microphone_read(
    uint32_t *stored, uint32_t *projected, uint8_t *valid, int32_t *error, uint8_t *playback_preserved);
// Start an isolated live-clock boot animation from a prepared source frame.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_start_live(uint32_t sounds_enabled, uint32_t playback_volume);
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_live_status(CodyHostStudioLiveStatus *status, size_t capacity);

// request: Explicit route and scenario revisions, copied to the display owner.
// Exception: Returns validation, observed-origin, gesture or destination errors.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_replay_route(const CodyHostStudioRouteRequest *request);

// target: Native source animation identity.
// index: Authored frame index; index zero also obtains count and cycle duration.
// count, start_ms, duration_ms, cycle_ms: Receives compiled animation metadata.
// Exception: Rejects unknown animations and out-of-range indices without writing outputs.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_animation_frame(const char *target, uint32_t index,
    uint32_t *count, uint32_t *start_ms, uint32_t *duration_ms, uint32_t *cycle_ms);

// name: Stable host capability name such as "wake-word".
// available: Receives the boolean feature selected in the compiled native profile.
// Exception: Rejects invalid or unknown names without modifying the output.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_has_capability(const char *name, uint8_t *available);

// request: Isolated controlled-worker advance, copied to the display owner.
// Exception: Rejects backward/unsupported times and preserves owner failures.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_replay_worker(const CodyHostStudioWorkerRequest *request);
#endif

// pixels: Receives the complete latest native RGB565 frame.
// count: Exact logical framebuffer capacity, 466 x 466 pixels.
// identity: Receives the identity committed with those exact pixels.
// identity_bytes: Exact caller-owned identity capacity, checked before any write.
// Exception: Rejects invalid buffers, missing frames, or a non-Studio frame.
CODY_HOST_STUDIO_API esp_err_t cody_host_display_studio_snapshot(uint16_t *pixels,
    size_t count, CodyHostStudioFrameIdentity *identity, size_t identity_bytes);

// No parameters: reports the current native output capacity before a ctypes allocation is passed.
// Exception: No exceptions; the caller must reject a different output ABI size.
CODY_HOST_STUDIO_API size_t cody_host_studio_frame_identity_size();

// pixels, count: Complete logical RGB565 output and exact capacity.
// identity, identity_bytes: Same-frame identity output and exact capacity.
// receipt, receipt_bytes: Worker acknowledgement output and exact capacity.
// Exception: Validates all capacities before any writes and rejects stale evidence.
CODY_HOST_STUDIO_API esp_err_t cody_host_display_studio_worker_snapshot(uint16_t *pixels, size_t count,
    CodyHostStudioFrameIdentity *identity, size_t identity_bytes,
    CodyHostStudioWorkerReceipt *receipt, size_t receipt_bytes);

// No parameters: provides the compiled worker receipt capacity before allocation.
// Exception: No exceptions.
CODY_HOST_STUDIO_API size_t cody_host_studio_worker_receipt_size();

// pixels, count: Exact native logical framebuffer destination and capacity.
// identity, identity_bytes: Same completed-frame identity and exact capacity.
// route, route_bytes: Route evidence retired with those pixels and exact capacity.
// Exception: Rejects invalid capacities and frames without observed route evidence.
CODY_HOST_STUDIO_API esp_err_t cody_host_display_studio_route_snapshot(uint16_t *pixels, size_t count,
    CodyHostStudioFrameIdentity *identity, size_t identity_bytes, CodyHostStudioRouteReceipt *route, size_t route_bytes);

// No parameters: returns the exact output capacity of the route receipt.
CODY_HOST_STUDIO_API size_t cody_host_studio_route_receipt_size();

namespace CODY::Domain { struct StateQuery; struct StateQueryResult; }
namespace CODY::host_studio_domain {
// request: Bounded synthetic page, with exact selection and coherent call scopes.
// Exception: Rejects invalid IDs, fields and enum spellings without publication.
esp_err_t validate(const CodyHostStudioCase &request);

// request: Validated native case; non-domain cases explicitly retire the fixture.
// Exception: Preserves validation, allocation and generation-overflow errors.
esp_err_t publish(const CodyHostStudioCase &request);

// request: Canonical bounded query from the unchanged production presenter.
// result: Copied synthetic projection; unchanged on unsupported or invalid queries.
// Exception: Preserves unavailable, wrong-scope and stale-cursor failures.
esp_err_t query(const Domain::StateQuery &request, Domain::StateQueryResult &result);
}
