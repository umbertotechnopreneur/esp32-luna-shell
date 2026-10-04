# TOF400F implementation

UART transport/resource ownership and the separately testable Modbus framing/CRC
protocol. Public driver/protocol declarations remain in `../include/`.

The existing fixtures remain in `../tests/`. This folder does not change feature
admission, sensor behavior or measured distance publication.
