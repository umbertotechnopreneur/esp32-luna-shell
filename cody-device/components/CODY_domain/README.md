# Portable Cody domain

`include/CODY_domain_types.hpp` defines the C++17 value contract for
`CODY-NEXT-01`. `include/CODY_domain_capacity.hpp` budgets those actual types,
indexes, field evidence, text, tombstones, a full staging generation and scratch.
They include only standard C++ headers and allocate no storage.

The schema is independent of the renderer, ESP-IDF, operating system, board,
transport and microphone. See the [domain contract](../../../docs/cody/domain-model.md)
for identity, ownership, lifecycle, admission and recovery requirements.

`include/CODY_state_protocol.hpp` adds phase-02 wire limits and pure preliminary
chunk/revision decisions. The [protocol contract](../../../cody-desktop/plugins/cody-codex-plugin/protocol/CODY_STATE_V1.md)
defines the JSON schema, examples, authentication and synchronization rules.
These helpers do not authenticate, decode JSON or commit a domain generation.

These are source definitions, not a running registry. No production consumer or
build target is connected yet. Live protocol binding belongs to phases 03/05,
portable build integration to `CODY-NEXT-14`, and store/reducer implementation
to `CODY-NEXT-05`. Use the existing public console for separately authorized
compilation and scenarios; no alternate executable entry point is added here.
