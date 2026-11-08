#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace CODY::host_snapshot {
// Borrowed strings remain alive for the complete synchronous import call.
// Names are schema identities; array order has no domain meaning.
struct FieldInput {
    const char *path;
    const char *value;
};

constexpr std::size_t kMaximumInputFields = 2048;
constexpr std::size_t kMaximumComponents = 128;
constexpr std::size_t kMaximumPathBytes = 96;
constexpr std::size_t kMaximumLiteralBytes = 512;
constexpr std::size_t kComponentMetadataFields = 6;
}

#ifdef _WIN32
#define CODY_HOST_SNAPSHOT_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_SNAPSHOT_API extern "C"
#endif

// No parameters: allocates a host-only observation owner on its heap.
// Exception: Returns nullptr on allocation failure, never a firmware canonical owner.
CODY_HOST_SNAPSHOT_API void *cody_host_snapshot_create();

// handle: Exclusive observation owner released after its synchronous calls finish.
CODY_HOST_SNAPSHOT_API void cody_host_snapshot_destroy(void *handle);

// handle: Exclusive host-only destination, committed only after the whole batch succeeds.
// fields: Borrowed bounded leaf records whose identities must be unique.
// count: Exact nonzero record count.
// now_us: Original positive virtual observation time for production aging.
// failed_index: Receives the rejected record index, or count for a batch-wide failure.
// Exception: Returns original conversion, schema, allocation or admission errors without partial commit.
CODY_HOST_SNAPSHOT_API int cody_host_snapshot_import(void *handle,
    const CODY::host_snapshot::FieldInput *fields, std::size_t count,
    int64_t now_us, std::size_t *failed_index);

// handle: Owner of the original synthetic observations.
// paths: Exact component identities requested as a single batch.
// count: Exact bounded component count, including zero for an empty projection.
// values: Receives six int64 metadata fields per component in the requested storage order.
// capacity: Exact output capacity in int64 elements.
// Exception: Returns original lookup/allocation errors without partially exposing metadata.
CODY_HOST_SNAPSHOT_API int cody_host_snapshot_components(void *handle,
    const char *const *paths, std::size_t count, int64_t *values, std::size_t capacity);

// handle: Exclusive host observation/output owner.
// path: Exact public firmware API route.
// now_us: Original positive virtual observation time.
// data: Receives borrowed output only after serialization succeeds.
// size: Receives exact output byte count on success.
// Exception: Returns original route/serialization errors without exposing partial output.
CODY_HOST_SNAPSHOT_API int cody_host_snapshot_json(void *handle, const char *path,
    int64_t now_us, const char **data, std::size_t *size);
