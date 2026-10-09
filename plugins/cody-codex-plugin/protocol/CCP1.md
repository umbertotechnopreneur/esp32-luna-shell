# CCP1 local pairing protocol

CCP1 connects one physical Cody device to one local Cody's Codex Plugin (CCP)
runtime. It provides bounded UDP commissioning, liveness and the explicit
chat-activity extension below. It does not transport user messages or replies.

## Identity and ports

- Cody listens on UDP port `42117` while pairing.
- CCP listens on UDP port `42118` while Codex has the plugin running.
- A device ID is the 12 lowercase hexadecimal digits of the station MAC.
- A PC ID is 32 random lowercase hexadecimal digits retained by CCP.
- The displayed code contains six characters from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`.

Per il simulatore uso un'identita' separata di 12 cifre esadecimali e la indico
sempre come SIMULATED. La discovery deve includerlo esplicitamente; non pubblico
quell'identita' come un MAC misurato su un dispositivo fisico.

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

Derivo un verifier per ogni offerta usando il codice ricevuto dall'utente e
richiedo una sola corrispondenza. Prima del primo PAIR genero il nonce, derivo
la chiave del link come descritto sotto e salvo il candidato PC in
`pending-pairings.json`, protetto con DPAPI e separato dall'attivo `state.json`.
Posso cosi' recuperare anche se tutte le risposte PAIRED vanno perse. Poi invio:

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

Salvo sul Cody il candidato autenticato in `ccp_pending`, separato dal record
attivo `ccp_pairing`. Mantengo lo schema v1 dell'attivo e i messaggi di pairing;
aggiungo la conferma post-commit `COMMITTED` descritta sotto.
Il vecchio link resta attivo finche' ricevo la prova che il PC ha salvato il
candidato. Dopo la scrittura riuscita del pending rispondo con:

```text
CCP1|PAIRED|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The confirmation proof uses the link key over:

```text
paired|<device>|<session>|<pc>|<nonce>
```

Con `PAIRED` confermo che Cody ha conservato il candidato, non che il nuovo link
e' gia' attivo. Il candidato sul PC e' gia' persistito, senza sostituire il
vecchio attivo; dopo PAIRED valido posso inviare:

```text
CCP1|CONFIRM|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The proof uses the persisted link key over:

```text
confirm|<device>|<session>|<pc>|<nonce>
```

Tengo la schermata di pairing fino alla prova valida del PC. Con `CONFIRM`
autenticato promuovo il candidato nel record attivo e rimuovo il pending;
solo dopo queste operazioni riuscite rispondo:

```text
CCP1|ACTIVE|device=<12 hex>|session=<8 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
```

The activation proof covers:

```text
active|<device>|<session>|<pc>|<nonce>
```

Nel commissioning chiudo con successo il tool e promuovo il candidato PC
soltanto dopo `ACTIVE` autenticato; sul Cody mostro All set dopo l'invio
riuscito di `ACTIVE`.
Conservo per una breve finestra la tupla non segreta di attivazione, cosi' un
`CONFIRM` ripetuto e valido puo' recuperare una risposta finale persa senza
trattenere il codice o il materiale temporaneo di derivazione.

Se un riavvio interrompe la transazione, recupero `ccp_pending` e cerco il PC
con quel candidato. Un `READY` fresco, autenticato e legato alla richiesta
corrente dimostra che il PC conserva il link: posso promuoverlo e chiudere il
journal anche senza ripresentare un codice. Invio poi `COMMITTED`, cosi' anche
il PC puo' promuovere il suo candidato dopo una prova del commit sul dispositivo.
Non invento una risposta positiva a un vecchio tool. Senza la prova del PC
mantengo attesa o errore reali.

Rifiuto un candidato diverso mentre il journal ne conserva gia' uno, fino a
Cancel oppure a una nuova sostituzione esplicita. Cancel chiude il tentativo e
rimuove il pending sul Cody, preservando l'attivo. Sul dispositivo, una lettura,
scrittura, validazione o cancellazione storage fallita resta repair-required; la sola
scelta di Retry non la trasforma in successo.

Con Forget confermato elimino sul dispositivo soltanto `ccp_pairing` e
`ccp_pending`. Mantengo Wi-Fi, skin, preferenze e log. Il record DPAPI sul PC
rimane; questo comando non invia una revoca remota. Uso questa azione esplicita
anche per rimuovere un record danneggiato, senza migrazioni o reset automatici.

Non prometto un rollback atomico distribuito. Se Cancel torna a un vecchio link,
conservo il vecchio collegamento operativo e lascio il candidato PC irrisolto.
Un `COMMITTED` con la vecchia chiave non lo cancella: puo' essere un pacchetto
precedente riprodotto. Anche dopo Cancel, Forget o una prima associazione
interrotta richiedo un nuovo codice visibile e `replace_pending:true`
esplicitamente autorizzato per sostituire il candidato PC. Non lo elimino
automaticamente per un pacchetto vecchio o per il solo trascorrere del tempo.

The displayed code, verifier and pairing salt are wiped when the attempt ends.
Neither side logs them.

## Authenticated rediscovery

Dopo Let's go, al boot con record salvato o durante il recupero, avvio subito
la ricerca autenticata, senza una pausa Idle intermedia. Cody invia:

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

Con un link attivo, gli scambi FIND/READY ripetuti provano la presenza. Nel
recupero di un candidato pending, una risposta fresca e valida prova anche che
il PC conserva quel candidato e permette la promozione descritta sopra.
In entrambi i casi aspetto eventi separati per task, progetto, chat o messaggi.
On the PC, a newly persisted pairing has no liveness timestamp. The responder
marks it seen only after verifying FIND and successfully sending READY. The
ten-second PC presence deadline uses a monotonic clock; wall time is retained
only for the reported observation timestamp. A stopped responder is an actual
MCP tool error. The listener is acquired before any protected-state access so a
second local process cannot race first-use identity creation.

### Conferma post-commit sul dispositivo

Dopo ogni READY valido invio questo ACK solo quando il record attivo e'
durabilmente scritto e riletto. Lo ripeto anche per un link gia' attivo, cosi'
recupero un ACK perso al successivo giro di presenza:

```text
CCP1|COMMITTED|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
HMAC input: committed|<device>|<pc>|<nonce>
```

Uso la chiave del link attivo e il nonce FIND corrente. Sul PC confronto prova,
identita', sorgente e nonce con un FIND osservato negli ultimi dieci secondi.
Se corrisponde al candidato, lo promuovo; se prova il ritorno al vecchio attivo,
mantengo operativo quel link e lascio il candidato irrisolto. Non ricavo da
una prova con la vecchia chiave un'autorizzazione a cancellare il candidato;
serve la sostituzione esplicita descritta sopra. Un FIND da solo non autorizza
alcuna promozione.
Lascio `pairing_pending` e `retained_pairing` distinti nello stato pubblico.

## Authenticated chat activity (CCP 0.3)

A capable Cody requests activity using a distinct HMAC domain, so stripping the
capability cannot turn the request into an authenticated presence-only request:

```text
CCP1|FIND|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|activity=1|proof=<64 hex>
HMAC input: find-activity|<device>|<pc>|<nonce>
```

CCP sends ordinary READY, followed by a complete latest-state snapshot:

```text
CCP1|ACTIVITY|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|chat=<64 hex>|revision=<16 hex>|phase=<token>|project=<hex or ->|proof=<64 hex>
HMAC input: activity|<device>|<pc>|<nonce>|<chat>|<revision>|<phase>|<project>
```

The link key authenticates every field. `chat` is the SHA-256 of the followed
opaque Codex session ID; all zeroes denote waiting for a prompt. `revision` is a volatile
unsigned counter for diagnostics, not a persisted ordering guarantee across PC
restarts. Freshness comes from the device's current random FIND nonce, accepted
at most once for ACTIVITY. Old polls, duplicated replies and altered metadata are
rejected. The full datagram remains below 512 bytes. Text is hex-encoded printable
ASCII, at most 24 bytes; `-` is allowed only with `phase=none` and zero chat ID.

Phases: `none`, `active`, `working`, `editing`, `running`, `ready`, `paused`,
`interrupted`, `ended`, `unavailable`. These map to screens 15/16, 17, 18, 19, 20A/20B, or
18U unavailable. An eight-second missing PC lease uses screen 32 after a chat
has been followed. A retained label identifies context only while disconnected.

The owner publishes activity into DeviceSnapshotService, separately marking
freshness/health/error. No UI consumer reads host data or sockets. Unavailable
activity never becomes a numeric estimate or a synthetic working state.

Codex hooks submit only event/session/turn/tool identities and cwd through the
DPAPI-authenticated 127.0.0.1:42119 control endpoint. No prompts, tool arguments,
responses or transcript paths are retained. Since CCP 0.4, only the latest emitted
UserPromptSubmit changes the followed chat; other chats' background events cannot
steal it. All Cody devices paired to this PC mirror that context. SessionStart
registers metadata only. The workspace leaf provides a bounded ASCII display
label, never identity or a claimed chat title. No device or MCP selection step is
required. Sessions and followed context are volatile; restart waits for the next
genuine prompt, while LAN reconnect retains the context. Active hook
states expire after 120 seconds without a new event. MCP leases keep the broker
alive but do not renew activity. There is no model-callable activity setter.

## Security boundary

The six-character code is never sent in a datagram. PBKDF2 makes captured
offers more expensive to brute-force, and every successful pairing rotates the
salt, nonce and link key. This compact commissioning protocol is not a formal
PAKE and must not be presented as equivalent to TLS, SPAKE2 or security
certification. A later production review can replace commissioning without
changing the stable device and PC identities.
