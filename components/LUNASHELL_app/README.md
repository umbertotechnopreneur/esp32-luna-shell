# Luna app

Full diagnostic UI on Brookesia System Core and declarative GUI documents.
Public contracts remain in `include/`; component selection stays in the root
CMake files. Cody uses its separate standalone renderer.

| Folder | Responsibility |
| --- | --- |
| [src/runtime](src/runtime/README.md) | App lifecycle, logs and alerts |
| [src/documents](src/documents/README.md) | Document lifecycle, bindings and named actions |
| [src/pairing](src/pairing/README.md) | Pairing presenter and QR preparation |
| [src/chrome](src/chrome/README.md) | Navigation, arcs, status and command hit testing |
| [src/assets](src/assets/README.md) | Background/mascot asset access |
| [src/shell](src/shell/README.md) | UI-minimal shell composition |

Document callbacks retain both document ID and loading generation. The
subscription boundary verifies the active app and owning generation before
dispatch. Failed connection clears the current attempt's subscriptions and
returns the real error.

Actions have unique names; setting actions pair explicit IDs with handlers.
Table positions are traversal details, never action identities. The UI-minimal
shell has its own source list and does not adopt document lifecycle ownership.

See [display/touch](../../docs/architecture/display-touch-runtime.md),
[snapshots](../../docs/architecture/device-snapshot.md) and
[pairing](../../docs/architecture/connectivity-pairing.md).
