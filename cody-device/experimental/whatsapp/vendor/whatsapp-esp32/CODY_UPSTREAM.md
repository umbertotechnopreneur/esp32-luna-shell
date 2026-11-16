# Cody integration of whatsapp-esp32

Source: https://github.com/oxidezap/whatsapp-rust-esp32 at commit
`3644829360ecf5666d645d35d86d89651ca9394d`; MIT license in `LICENSE`.

Cody removes binary startup and `.env` loading from the library integration.
Its storage owner fails on incomplete reset, orphaned Signal state, unreadable
or missing sync-key markers instead of automatically erasing records or guessing
which key is current. These changes and compatibility with ESP-IDF 6.0.2 have
not been compiled or validated on hardware.
