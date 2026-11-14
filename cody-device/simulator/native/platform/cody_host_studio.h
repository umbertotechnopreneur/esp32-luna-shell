#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

// Host-only static Studio contract. No firmware enum or mutable owner pointer
// crosses this boundary. Pixels and this identity retire in one display lock.
struct CodyHostStudioFrameIdentity {
    uint32_t abi = 2;
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
};
static_assert(sizeof(CodyHostStudioFrameIdentity) == 240, "Studio frame identity ABI must remain fixed");

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

struct CodyHostStudioCase {
    uint32_t abi = 1;
    uint32_t size = 0;
    CodyHostStudioFrameIdentity identity{};
    uint32_t brightness_percent = 80;
    uint32_t auto_rotate = 0;
    uint32_t orientation_degrees = 0;
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

// request: Bounded ABI/version/size-checked synthetic case, copied before enqueue.
// Exception: Returns validation before settings mutation, owner/frame errors or a real wait timeout.
#if defined(CODY_HOST_REAL_AUDIO)
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_prepare_case(const CodyHostStudioCase *request);

// request: Explicit route and scenario revisions, copied to the display owner.
// Exception: Returns validation, observed-origin, gesture or destination errors.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_replay_route(const CodyHostStudioRouteRequest *request);

// target: Native source animation identity.
// index: Authored frame index; index zero also obtains count and cycle duration.
// count, start_ms, duration_ms, cycle_ms: Receives compiled animation metadata.
// Exception: Rejects unknown animations and out-of-range indices without writing outputs.
CODY_HOST_STUDIO_API esp_err_t cody_host_studio_animation_frame(const char *target, uint32_t index,
    uint32_t *count, uint32_t *start_ms, uint32_t *duration_ms, uint32_t *cycle_ms);
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
