# LUNASHELL_logger

Canonical console, bounded RAM and removable-storage logging, with the existing
USB serial transport and diagnostic command handling.

Implementation and private USB support live in [src](src/README.md). The public API
stays in `include/`; component identity and dependencies remain at root.

`LUNASHELL_logger_set_power_off` seals new logger/protocol USB transmissions,
waits up to 2200 ms for an admitted writer and, with a connected USB host, waits
up to 250 ms for driver TX completion. Its structured result distinguishes
ownership retirement from delivery. An absent host returns `ESP_ERR_NOT_FOUND`;
a non-consuming host can return `ESP_ERR_TIMEOUT`. The power orchestrator may
explicitly accept these USB-only losses after ownership has retired. They are
never reported as a successful drain. SD retirement remains a separate mandatory
storage-owner operation. RAM capture and counted USB drops continue while sealed.
Every aborted shutdown must unseal USB. Direct stdio/VFS output is outside this
logger seal; `fflush(stdout)` alone does not drain logger driver traffic.

`LUNASHELL_logger_finish_boot_capture_wait` seals boot capture and waits for the
writer's completion after releasing its internal backlog. Cody awaits this before
its ready frame to make that memory available for DMA. Archival errors remain
optional startup failures; the actual renderer still reports admission failures.
The live logging buffers remain internal and cache-safe. Repeated finish requests
retain the first operation's result, and a timeout leaves ownership with the writer.
