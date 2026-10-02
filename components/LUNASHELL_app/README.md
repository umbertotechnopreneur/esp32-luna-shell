# Luna app source map

The full app targets Brookesia System Core and its declarative GUI runtime.
These source boundaries keep screen lifecycle and event handling easy to find:

| File | Responsibility |
| --- | --- |
| LUNASHELL_app.cpp | App lifecycle, route transitions and feature behavior |
| LUNASHELL_app_documents.cpp | Screen/document ownership, load/unload, view bindings and app metadata |
| LUNASHELL_app_document_actions.cpp | Named route action catalogs, setting handlers and subscription ownership |
| LUNASHELL_navigation_service.cpp | Back/Forward history, independent from document rendering |
| LUNASHELL_adaptive_command_bar.cpp | Command geometry, visual bindings and action hit testing |

Document callbacks capture both the document ID and its loading generation.
One subscription boundary checks that the app is active and the captured
document still owns the screen before dispatching the event. Failed connection
clears every subscription from the current loading attempt; the real error
returns to the document orchestrator. Keep those checks together when adding
a control.

Ordinary route actions have unique string IDs. Setting tables explicitly pair
an action ID with its named handler; timing controls use AdaptiveChromeTiming.
Table positions are traversal details and do not identify actions.

CMakeLists.txt includes the new action module in the full app source list.
The UI-minimal app keeps its own existing source list and does not use this
document lifecycle.

This organization change has static source review only. Builds and regression
checks remain deferred until explicitly requested; hardware behavior is
unverified.
