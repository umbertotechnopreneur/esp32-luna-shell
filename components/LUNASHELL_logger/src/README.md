# Logger implementation

Canonical logger storage/console lifecycle and USB serial transport.
`LUNASHELL_logger_usb.h` is private support shared by these implementation files.

Public logging/readback contracts remain in `../include/`. Logging does not admit
implicit card formatting or replace canonical device error state.
