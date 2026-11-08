# Runtime services

Settings persistence, canonical clock observations/routine policy, shared sleep
coordination and liveness supervision. These services retain their existing
owners and public interfaces in `include/`.

Clock acquisition publishes through the private snapshot boundary; consumers do
not read the RTC directly.
