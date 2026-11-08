# Distance service

Optional distance acquisition and canonical snapshot publication. The real service
uses the separately gated `LUNASHELL_tof400f` component; the stub serves disabled builds.

Consumers read published validity, timestamps and failures instead of accessing
UART sensors themselves.
