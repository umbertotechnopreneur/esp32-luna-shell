# CCP1 local pairing protocol

CCP1 connects one physical Cody device to one local Cody's Codex Plugin (CCP)
process. It is a bounded UDP commissioning and liveness protocol, not the
future Codex message/session transport.

## Identity and ports

- Cody listens on UDP port `42117` while pairing.
- CCP listens on UDP port `42118` while Codex has the plugin running.
- A device ID is the 12 lowercase hexadecimal digits of the station MAC.
- A PC ID is 32 random lowercase hexadecimal digits retained by CCP.
- The displayed code contains six characters from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`.

Every datagram is printable ASCII, at most 512 bytes, and uses `|` separators.
Unknown, duplicated or missing fields are rejected. Senders use the canonical
field order shown below for reproducible proofs and diagnostics.

## Pairing

CCP broadcasts:

```text
CCP1|DISCOVER|request=<16 hex>
```

A Cody with an active pairing window replies:

```text
CCP1|OFFER|request=<16 hex>|device=<12 hex>|session=<8 hex>|salt=<32 hex>|tag=<64 hex>
```

The verifier is PBKDF2-HMAC-SHA256 over the displayed ASCII code and the
16-byte salt, with 50,000 iterations and a 32-byte output. `tag` is
HMAC-SHA256 using that verifier over:

```text
offer|<request>|<device>|<session>|<salt>
```

The plugin derives a verifier for every offer using the code entered by the
user. Exactly one offer must match. CCP then sends:

```text
CCP1|PAIR|device=<12 hex>|session=<8 hex>|pc=<32 hex>|port=42118|nonce=<32 hex>|proof=<64 hex>
```

`proof` is HMAC-SHA256 using the verifier over:

```text
pair|<device>|<session>|<pc>|42118|<nonce>
```

Both peers derive the link key as HMAC-SHA256 using the verifier over:

```text
link|<device>|<session>|<pc>|<nonce>
```

Cody persists the PC ID and link key as one versioned CRC-protected record and
confirms with:

```text
CCP1|PAIRED|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The confirmation proof uses the link key over:

```text
paired|<device>|<session>|<pc>|<nonce>
```

`PAIRED` proves Cody stored the relationship, but does not finish pairing. CCP
first protects the matching record with Windows DPAPI, then sends:

```text
CCP1|CONFIRM|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The proof uses the persisted link key over:

```text
confirm|<device>|<session>|<pc>|<nonce>
```

Cody keeps the pairing screen active until that proof succeeds, then replies:

```text
CCP1|ACTIVE|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The activation proof covers:

```text
active|<device>|<session>|<pc>|<nonce>
```

Only authenticated `ACTIVE` closes the plugin tool successfully, and only a
successfully sent `ACTIVE` promotes Cody to the paired screen. This two-way
commit prevents either side from presenting a one-sided retained link as done.
Cody retains only the non-secret session/PC/nonce activation tuple for a short
replay window, allowing a repeated valid `CONFIRM` to recover a lost `ACTIVE`
without retaining the display code, salt or verifier.

The displayed code, verifier and pairing salt are wiped when the attempt ends.
Neither side logs them.

## Authenticated rediscovery

After the temporary Idle screen, Cody broadcasts:

```text
CCP1|FIND|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The proof uses the stored link key over:

```text
find|<device>|<pc>|<nonce>
```

Only the CCP process with the matching PC ID and link key replies:

```text
CCP1|READY|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The reply proof covers:

```text
ready|<device>|<pc>|<nonce>
```

Repeated authenticated FIND/READY exchanges are liveness evidence only. They
do not prove that a Codex task, project, thread, or message transport exists.
On the PC, a newly persisted pairing has no liveness timestamp. The responder
marks it seen only after verifying FIND and successfully sending READY. The
ten-second PC presence deadline uses a monotonic clock; wall time is retained
only for the reported observation timestamp. A stopped responder is an actual
MCP tool error. The listener is acquired before any protected-state access so a
second local process cannot race first-use identity creation.

## Security boundary

The six-character code is never sent in a datagram. PBKDF2 makes captured
offers more expensive to brute-force, and every successful pairing rotates the
salt, nonce and link key. This compact commissioning protocol is not a formal
PAKE and must not be presented as equivalent to TLS, SPAKE2 or security
certification. A later production review can replace commissioning without
changing the stable device and PC identities.
