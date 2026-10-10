# CCP1 local pairing protocol

CCP1 connects one physical Cody device to one local Cody's Codex Plugin (CCP)
runtime. It provides bounded UDP commissioning, liveness, selected chat
activity and semantic question/answer exchange. It does not transport full chat
transcripts or audio.

## Identity and ports

- Cody listens on UDP port `42117` while pairing.
- CCP listens on UDP port `42118` while Codex has the plugin running.
- A device ID is the 12 lowercase hexadecimal digits of the station MAC.
- A PC ID is 32 random lowercase hexadecimal digits retained by CCP.
- The displayed code contains six characters from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`.

Per il simulatore uso un'identita' separata di 12 cifre esadecimali e la indico
sempre come SIMULATED. La discovery deve includerlo esplicitamente; non pubblico
quell'identita' come un MAC misurato su un dispositivo fisico.

Every datagram is printable ASCII and uses `|` separators. Ordinary packets
admit 512 bytes; ACTIVITY and TECH admit 1024; EVENT admits 1280.
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

## Authenticated rediscovery (CCP 0.6.3)

After commissioning, a retained boot or reconnection, Cody broadcasts a new
128-bit client nonce every two seconds. The authenticated FIND capabilities
below retain their existing HMAC domains. FIND alone establishes neither
current presence nor permission to return activity, questions or technical data.

```text
CCP1|FIND|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|proof=<64 hex>
HMAC input: find|<device>|<pc>|<nonce>
```

The matching broker verifies the retained key and returns a fresh, signed,
128-bit server challenge. Its `address` and `port` are the device endpoint
observed by the receiving socket, not fields chosen by the requester.

```text
CCP1|READY|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|challenge=<32 hex>|address=<IPv4>|port=42117|proof=<64 hex>
HMAC input: ready-v2|<device>|<pc>|<nonce>|<challenge>|<address>|<port>
```

Cody verifies the HMAC, current client nonce, fixed broker source port and its
own endpoint: `address` must match current canonical Wi-Fi IPv4 and `port` must
be 42117. The simulator requires its explicit loopback endpoint. A relay cannot
ask Cody to sign a challenge addressed to the relay's socket. No consumer reads
radio hardware or guesses an unavailable address.

READY can finish a retained candidate transaction after reboot. Only after the
active device key is durably written and read back does Cody acknowledge it:

```text
CCP1|COMMITTED|device=<12 hex>|pc=<32 hex>|nonce=<32 hex>|challenge=<32 hex>|address=<IPv4>|port=42117|proof=<64 hex>
HMAC input: committed-v2|<device>|<pc>|<nonce>|<challenge>|<address>|<port>
```

The broker retains at most 128 endpoint-scoped challenges for two monotonic
seconds. A duplicate FIND for the same endpoint and nonce does not renew that
challenge's deadline. COMMITTED must match its original socket endpoint,
nonce, fresh challenge and proof. The challenge is consumed before persistence
or outgoing effects; duplicate, expired and previously captured acknowledgements
cannot renew liveness or disclose another snapshot.

Only this completed exchange marks the device seen, admits the event peer and
returns ACTIVITY, TECH or EVENT. An old captured FIND can obtain a fresh
challenge but cannot obtain current metadata without proving the retained key
at the signed device endpoint. A failed or lost exchange waits for Cody's next
fresh FIND. The ten-second PC presence lease uses monotonic time; UTC timestamps
are retained only for status presentation.

The broker promotes a matching pending candidate after the fresh COMMITTED.
Proof with the previous active key preserves that association without retiring
an unrelated pending candidate. `pairing_pending` and `retained_pairing` remain
separate. The exclusive local listener is acquired before protected identity
access, and a stopped responder remains an actual MCP tool error.

CCP 0.6.3, firmware and the explicit simulator must be updated together.
Earlier READY/COMMITTED layouts and HMAC domains are rejected; there is no
compatibility fallback. The saved pairing record format remains unchanged.

## Authenticated chat activity and purpose

Current firmware negotiates `activity=3`, with or without the separate events
capability. Each capability is bound to its own HMAC domain; stripping or
changing a capability never reuses a presence proof. Version three adds the
separate TECH projection below; its ACTIVITY payload keeps version-two fields.

```text
CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=3|proof=<64hex>
HMAC input: find-activity-v3|<device>|<pc>|<nonce>

CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=3|events=2|current=<32hex>|state=<device-state>|proof=<64hex>
HMAC input: find-events-activity-v3|<device>|<pc>|<nonce>|<current>|<state>

CCP1|ACTIVITY|device=<12hex>|pc=<32hex>|nonce=<32hex>|chat=<64hex>|revision=<16hex>|phase=<token>|project=<hex or ->|description=<hex or ->|branch=<hex or ->|proof=<64hex>
HMAC input: activity-v2|<device>|<pc>|<nonce>|<chat>|<revision>|<phase>|<project>|<description>|<branch>
```

The PC sends this complete activity snapshot only after the fresh COMMITTED. The link
key authenticates every field, in exactly the documented order. `chat` is the
SHA-256 of the followed opaque Codex session ID; all zeroes denote no followed
prompt. `revision` is a volatile diagnostic counter, not a persisted ordering
guarantee. The current random FIND nonce admits at most one ACTIVITY snapshot;
old polls, duplicate replies and altered metadata are rejected.

ACTIVITY and TECH use separate 1024-byte extension envelopes. EVENT retains its
1280-byte envelope; every other datagram retains its original 512-byte bound.
There is no fragmentation. Printable ASCII text is hex encoded: `project` and
`branch` each admit at most 64 characters, `description` at most 192. `-` means
no description or unavailable branch. A no-chat snapshot requires `phase=none`,
zero chat, `project=-`, `description=-` and `branch=-`. A description is admitted
only for actual `working`, `editing` or `running` phases.

The hook ingestion owner obtains the actual branch from the nearest checkout's
`.git/HEAD`, including an explicit worktree `gitdir` pointer. Detached HEAD is
reported as `DETACHED`; an absent checkout or failed/unrepresentable read makes
the branch unavailable and preserves the real diagnostic. No Git command,
transcript, raw tool input or guessed branch is used. Project metadata uses the
actual workspace leaf within its 64-character ASCII contract; unrepresentable
labels explicitly report `UNAVAILABLE`. Measured overflow scrolling belongs to
the native display; authenticated metadata retains the accepted full label.

Version-one peers continue to negotiate `activity=1`, using `find-activity` or
`find-events-v2`. Their ACTIVITY fields remain device, pc, nonce, chat, revision,
phase, project and proof, authenticated with `activity`. Their 24-byte project
slot receives a separately logged display projection when a newer owner holds
a longer name. Version-one responses remain below 512 bytes. Version-two peers
use `find-activity-v2` or `find-events-activity-v2` and receive ACTIVITY without
TECH. Current firmware requires the version-three contract it requests; it
does not silently downgrade.

Phases are `none`, `active`, `working`, `editing`, `running`, `ready`, `paused`,
`interrupted`, `ended` and `unavailable`. A genuine `apply_patch` tool, including
native namespace qualification, maps to editing. `Bash`, `exec_command`,
`write_stdin` and `shell_command` map to running. Other tools, including opaque
code orchestration, remain generic working; arguments are never mined for an
invented command or purpose. Latest-state polling can miss brief tool bursts.

The canonical DeviceSnapshotService receives activity validity, health, original
observation times and real errors. UI consumers never read host sockets. Missing
PC presence after eight seconds uses the connection-lost view; expired activity
remains unavailable. A retained label identifies context while disconnected.

Hooks retain only event/session/turn/tool identities, cwd, bounded checkout
metadata, reported model and genuine compaction trigger through the
DPAPI-authenticated loopback owner. No prompts, tool
arguments, responses or transcript paths are retained. Only the latest genuinely
emitted UserPromptSubmit follows a chat; background events cannot switch it.
SessionStart registers metadata only. Broker restart waits for a new real prompt;
LAN recovery alone retains context. Active phases expire after 120 seconds
without a genuine event; MCP leases and description reports never renew them.
Rejected turn correlations log the event and expected/observed turn hashes,
without weakening old-turn protection or persisting raw hook contents.

`ccp_report_progress(session_id, turn_id, description)` adds a short actual
purpose only to the currently followed active turn. Identities come from
`ccp_list_sessions`. It cannot select a chat, change a hook phase or claim task
completion. Reports reject inactive/stale turns and unsupported text. Purpose
expires independently after 120 seconds and clears on a new prompt, Stop,
PermissionRequest, Interrupt or SessionEnd. The skill asks for actual English
work descriptions, never commands, logs, percentages or invented estimates.

Firmware and Cody Studio use shared native presentation helpers. The main title
shows Working, Editing code, Running command, Waiting for you or Reply ready.
Ordinary tool subtypes require 2.5 seconds of stable observation while generic
Working covers short bursts; completion, stale data and attention bypass that
dwell. The observed work clock starts before subtype admission, ignores repaint
and purpose changes, pauses during genuine interaction views and disappears on
completion/unavailability. Its timer appears only after ten observed seconds;
it is neither a CPU measurement nor a progress estimate.

The fixed project row holds for at least five seconds, fades out for 200 ms, remains black
for 100 ms, then fades in `branch: <actual branch>` over 200 ms. Branch holds for
at least three seconds before the reverse transition. Project is white; branch uses
WhiteSmoke. Actual attention keeps the project visible. Native font metrics
measure long identity captions and scroll their bounded native viewport. Long
identity dwell extends until its full scroll and end hold complete. A purpose fitting
the 320-pixel viewport stays still; overflow scrolls at 20 pixels per second
with 1.2-second start and 1.8-second end holds. This animation never triggers
hook activity, typewriter counts, touch admission or repeated audio.

## Authenticated technical metadata

Version-three FIND yields READY; fresh COMMITTED then yields ACTIVITY and TECH. Activity and technical
metadata are acquired under one ActivityStore lock, preserving one chat and
revision even when a hook arrives during UDP transmission. EVENT and other
independent extensions retain their own authenticated contracts.

```text
CCP1|TECH|device=<12hex>|pc=<32hex>|nonce=<32hex>|chat=<64hex>|turn=<64hex>|revision=<16hex>|model=<hex or ->|effort=-|modelage=<ms or ->|compaction=<token>|compactseq=<16hex>|compactage=<ms or ->|activityage=<ms or ->|turnelapsed=<ms or ->|used=-|capacity=-|cumulative=-|input=-|output=-|threshold=-|usageage=-|proof=<64hex>
HMAC input: technical-v1|<device>|<pc>|<nonce>|<chat>|<turn>|<revision>|<model>|<effort>|<modelage>|<compaction>|<compactseq>|<compactage>|<activityage>|<turnelapsed>|<used>|<capacity>|<cumulative>|<input>|<output>|<threshold>|<usageage>
```

Field order is mandatory. TECH accepts at most 1024 bytes without fragmentation.
All identifiers and hex text use lowercase hex. `turn` hashes the genuine
current turn ID; `model` retains a full 1..128-byte ASCII slug, with character
codes 33..126, hex encoded. Unrepresentable values are rejected with their real
cause rather than truncated. Milliseconds are canonical unsigned decimal
0..4294967295 with no leading zeroes; `-` means unavailable. A missing model
requires missing modelage and vice versa.

The receiver admits one TECH per current FIND challenge only after READY and
the matching ACTIVITY have been accepted. Chat and revision must exactly match
that ACTIVITY. TECH and ACTIVITY have independent consumed-nonce flags. TECH
before ACTIVITY, repeated TECH, old challenges, mismatched peers and invalid
proofs are rejected; the next FIND supplies a new coherent snapshot. No TECH ACK
or staged merge with an older activity is used. On pairing replacement, the
technical projection is cleared. Disconnect invalidates transport freshness;
historical metadata may remain visible only with explicit stale/unavailable
status. Transport freshness expires after eight seconds.

A zero chat requires zero turn, missing model and source ages, missing duration,
and `compaction=none|compactseq=0000000000000000|compactage=-`. A known idle chat
may have zero turn only while model, duration and compaction are unavailable;
its last genuine activity observation may still have an age. SessionStart after
an ended chat preserves the known chat without inventing a new turn.

| Field | Actual source and meaning |
| --- | --- |
| model, modelage | Common hook model metadata, its source emission age; reported model, not proof of separately executed/rerouted selection |
| effort | UNAVAILABLE: no documented reasoning-effort field in the hook payload |
| compaction, compactseq, compactage | Genuine PreCompact/PostCompact lifecycle within the exact current chat/turn; age of the latest observed lifecycle transition |
| activityage | Age of the latest admitted hook emission; a broker heartbeat never refreshes it |
| turnelapsed | Observed UserPromptSubmit-to-now duration, frozen at genuine Stop, Interrupt or SessionEnd |
| used, capacity | UNAVAILABLE: no connected desktop-owned usage/capacity stream |
| cumulative, input, output | UNAVAILABLE: no connected desktop-owned token counters |
| threshold, usageage | UNAVAILABLE: no observed automatic threshold or token sample |

The currently unsupported fields must be `-`; decoders reject numeric values
until an actual source and its semantics are implemented. No model-global
capacity, last-request input count, cumulative counter or guessed threshold is
substituted for current context occupancy. No percentage or free-token bar is
derived from missing data. Source ages use the PC monotonic clock and genuine
hook emission timestamp, distinct from receiver-local transport timestamps.
Reported model and active compaction observations expire independently after
120 seconds. Same-turn steering preserves the observed turn start and active
tools; a new actual turn clears the previous model and compaction scope.

Compaction tokens are `none`, `active`, `refreshed`, `orphaned`, `interrupted`
and `stale`. `none` requires zero sequence and missing compactage; other states
require a nonzero sequence and nonzero turn. PreCompact accepts only genuine
`auto`/`manual` trigger metadata, increments the lifecycle sequence and enters
active. A matching PostCompact changes active to refreshed. Ordinary tool hooks
never clear it. Duplicate boundaries and old-turn completions are rejected.
PostCompact without an observed start reports orphaned, never success. Stop
without PostCompact reports lost updates; interruption or session end reports
interrupted. An expired active observation projects stale, never completion.
Only active compactage denotes observed compaction start age; other ages are
transition ages, not an invented operation duration. Even a genuine refreshed
state supplies no new token sample and cannot revive old token numerators.

Official Codex documents the common hook model field and genuine compact
boundaries in [Hooks](https://learn.chatgpt.com/docs/hooks), and
`thread/tokenUsage/updated` plus `model/rerouted` in
[App Server](https://learn.chatgpt.com/docs/app-server). The plugin has no
connected notification channel for the currently owned desktop chat. No second
App Server, chat resume/start or transcript parsing is introduced to simulate
that channel. Changed plugin hooks require native review before execution;
enabling the plugin alone does not establish trusted live hook delivery.

## Notifiche e decisioni autentiche (CCP 0.6)

Tengo separati i metadati automatici della chat e le richieste esplicite MCP.
Per una richiesta MCP genero un ticket casuale di 128 bit e una capability di
256 bit, consegnata solo alla chiamata creatrice. `context` e' un'etichetta
dichiarata dal chiamante, non l'identita' o il focus di una chat. Non invento
hook per spedire una domanda. Un vero `PermissionRequest` produce soltanto un
avviso `On your PC`: l'autorizzazione nativa rimane nel normale prompt Codex.

```text
CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=1|events=2|current=<32hex>|state=<device-state>|proof=<64hex>
HMAC input: find-events-v2|<device>|<pc>|<nonce>|<current>|<state>

CCP1|EVENT|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|state=<state>|ttl=<seconds>|data=<hex or ->|proof=<64hex>
HMAC input: event|<device>|<pc>|<nonce>|<id>|<state>|<ttl>|<data>

CCP1|REPLY|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|choice=<choice-id>|proof=<64hex>
HMAC input: reply|<device>|<pc>|<nonce>|<id>|<choice>

CCP1|REPLYACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|choice=<choice-id>|status=<status>|proof=<64hex>
HMAC input: reply-ack|<device>|<pc>|<nonce>|<id>|<choice>|<status>

CCP1|EVENTACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|state=<terminal-state>|proof=<64hex>
HMAC input: event-ack|<device>|<pc>|<nonce>|<id>|<state>
```

The `events=2` capability accepts activity version 1, 2 or 3, with its matching
HMAC domain. `current` is the observed question ID, or all zeroes when absent;
`state` is `none` only for zero ID, otherwise pending, sending, received,
delivered, cancelled, expired or rejected. Notifications are not open questions.
EVENT admits at most 1280 bytes; ACTIVITY version 2 and TECH admit 1024; other packets
retain 512. Application messages are never fragmented.

EVENT e' uno snapshot completo e immutabile del contenuto del ticket, firmato
con la chiave del link e il nonce FIND corrente. Ammetto una sola pubblicazione
EVENT per nonce e solamente dopo READY. `state` e' `pending`, `received`,
`delivered`, `cancelled`, `expired`, `unavailable` oppure `none`. Il caso `none` usa ID tutto
zero, `ttl=0` e `data=-`; non costituisce una risposta. `ttl` e' il numero
intero di secondi residui arrotondato per eccesso (0..180), non un timestamp
UTC; `pending` e `received` richiedono almeno un secondo. Una ripetizione non
allunga la scadenza locale originale. Offline conservo la domanda e il suo
percorso: nessun timeout, riavvio o ritorno LAN sceglie un'opzione.

Da CCP 0.6.2, se il FIND dichiara una domanda aperta che il broker non ha mai
pubblicato per l'associazione attiva (ad esempio dopo il suo riavvio), rispondo
prima con EVENT `unavailable`, lo stesso ID, `ttl=0`, `data=-`. Non pubblico
ancora una nuova domanda gia' accodata. Cody accetta il ritiro soltanto per
la propria domanda e conserva la scelta locale come consegna non confermata;
pubblica `rejected` con `ESP_ERR_NOT_FOUND`. Il successivo FIND firmato con
stato terminale, oppure assenza dopo Done, conferma il ritiro prima della
nuova pubblicazione. Se il frame o la pubblicazione canonica falliscono,
ripeto il ritiro; non serve EVENTACK per un ticket che il nuovo broker ignora.
Questo flusso non ricostruisce risposte perse e non prolunga le scadenze.

Decodifico `data` come byte esadecimali minuscoli. Il payload v1 contiene,
nell'ordine:

| Campo | Formato e limite |
| --- | --- |
| version | u8, valore 1 |
| kind | u8: 1 notice, 2 question, 3 approval_on_pc, 4 pr_pending, 5 checks_running, 6 checks_failed, 7 checks_passed |
| context | u8 lunghezza + 0..24 byte ASCII stampabili |
| title | u8 lunghezza + 1..24 byte ASCII stampabili |
| body | u8 lunghezza + 1..63 byte ASCII stampabili |
| choice_count | u8: 2 o 3 solo per question, 0 negli altri casi |
| choices | Per scelta: id, label, detail, tone nel formato sotto |
| selected_id | u8 lunghezza + 0..12 byte; vuoto finche' nessuna risposta |
| failure_count | u8, massimo 6; diverso da zero solo per checks_failed |
| failure_names | Per nome: u8 lunghezza + 1..48 byte ASCII stampabili |
| progress_valid | u8, esattamente 0 o 1 |
| passed | u16 big endian; zero se progress_valid=0 |
| total | u16 big endian; zero se progress_valid=0 |

Ogni scelta contiene `id` (u8 lunghezza + 1..12 byte, pattern
`[a-z][a-z0-9_]{0,11}`), `label` (u8 + 1..12 ASCII), `detail` (u8 + 1..63
ASCII) e `tone` u8: 0 neutral, 1 positive, 2 negative, 3 info. ID ed etichette
sono univoci; gli ID non dipendono dalla posizione. I testi obbligatori non
possono contenere soltanto spazi. Il decoder rifiuta campi eccedenti,
byte residui, combinazioni incoerenti e contenuto fuori budget: niente tagli
silenziosi. Per PR richiedo head esatto; pubblico progresso solo da un elenco
completo, corrente e riferito allo stesso head. Non deduco checks passati da
Stop. I nomi dei fallimenti sono quelli forniti da Codex: se non entrano,
rifiuto la notifica con rimando al computer. Non esistono azioni PR nel wire.
Con progresso valido richiedo `0 <= passed <= total`, `total > 0`; per
checks_failed i nomi non possono superare `total - passed`, per checks_passed
tutti i check devono essere passati.

REPLY riguarda soltanto question. Una risposta valida passa a `received`:
CCP l'ha registrata, ma non dichiaro ancora screen 31. Il primo poll MCP del
destinatario restituisce risposta e `answer_id` casuale; un poll successivo
con `acknowledge_answer_id` uguale conferma che il destinatario l'ha ricevuta
e passa a `delivered`. Solo REPLYACK `delivered`, o EVENT con quello stato,
autorizza `Reply sent`. Gli altri status REPLYACK sono `received`, `expired`,
`cancelled`, `conflict`, `unknown`. Ripetere la stessa scelta e' idempotente;
una scelta diversa non sovrascrive la prima. Non azzero lo stato per un ACK
perso: il successivo FIND o REPLY ne riporta lo stato autenticato.

Da CCP 0.6.1 distinguo la conferma del destinatario dalla chiusura osservata
sul dispositivo. Una domanda gia' pubblicata e chiusa dal broker precede la
successiva finche' Cody non applica lo stato terminale e invia EVENTACK.
L'ACK ammette solo `delivered`, `cancelled`, `expired` o `rejected`, con chiave
attiva, endpoint osservato e nonce FIND corrente. Non contiene una scelta,
non approva operazioni e non modifica il risultato del destinatario.
Una scadenza locale puo' precedere il frame `delivered`: conservo la reale
chiusura locale senza inventare `Reply sent` e senza riscrivere la risposta MCP.

| Passaggio | Proprietario | Effetto |
| --- | --- | --- |
| pending -> sending | Cody, dopo Send esplicito | Scelta confermata localmente; consegna ancora ignota |
| pending -> received | Broker, dopo REPLY autenticato | Risposta registrata; il destinatario deve confermarla |
| received -> delivered | Agent, poll con answer_id esatto | Destinatario confermato; Cody deve ancora osservare la chiusura |
| terminal -> EVENTACK | Cody, dopo pubblicazione canonica | Consente al broker di pubblicare la domanda successiva |

Ritrasmetto lo snapshot terminale anche oltre gli otto secondi della normale
presentazione, finche' ricevo l'ACK. Se l'ACK si perde, il FIND successivo
ripete la chiusura e Cody la riconferma, anche dopo Done. Una domanda mai
pubblicata non richiede una chiusura sul dispositivo. Le scadenze iniziano
all'accodamento: l'attesa della chiusura precedente non prolunga il TTL.
Il firmware che implementa EVENTACK, FIND events=2 e CCP 0.6.2 vanno aggiornati insieme;
non rilascio la successiva domanda per un semplice timeout di presentazione.

Tengo una sola domanda aperta per dispositivo, senza soppiantarla con attivita',
batteria o notifiche. La selezione sullo screen 27 rimane reversibile finche'
non confermo; Back torna al 26 conservandola. Poll e cancel richiedono ticket
e capability della chiamata creatrice; la capability non viaggia sulla LAN.
L'attesa del poll e' limitata a 15 secondi e non trattiene il lock delle altre
operazioni MCP. La cache volatile di risultati/scadenze e' limitata; il riavvio
del broker non rigioca richieste o risposte. Cody Studio usa lo stesso wire,
sempre dichiarato SIMULATED e soltanto con opt-in esplicito.

Una nuova notifica informativa ritira quelle precedenti per lo stesso device:
una vecchia PR fallita non torna quando scade la notifica successiva. La domanda
aperta mantiene la precedenza. Lego ogni ticket al digest della chiave attiva;
un cambio pairing lo ritira anche se la prima operazione successiva e' REPLY.
Le notifiche native terminano con il loro contesto hook reale. La scadenza
copre anche `received`: senza conferma del destinatario entro il TTL non
pubblico `delivered` e non espongo la risposta come decisione utilizzabile.

## Riavvio e ripristino espliciti (CCP 0.6)

Tengo distinti `ccp_reset_device` e `ccp_factory_reset_device`. Il primo
riavvia conservando tutte le impostazioni; il secondo cancella le impostazioni
del dispositivo, inclusi Wi-Fi e pairing, e riparte dal primo setup. Firmware,
asset e SD restano intatti. Entrambi richiedono target gia' paired e presente,
`confirmation_code` come stringa esatta `043533477` e intenzione corrispondente
nel messaggio utente corrente. La skill riconosce rispettivamente
`resetta il device codice 043533477` e `formatta cody 043533477`; non ricava
il codice da questo documento o da messaggi precedenti. Il codice evita
equivoci, non e' una password; l'autenticazione resta il link CCP.

```text
CCP1|RESET|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|code=043533477|proof=<64hex>
HMAC input: reset|<device>|<pc>|<nonce>|<id>|043533477
CCP1|RESETACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|status=accepted|proof=<64hex>
HMAC input: reset-ack|<device>|<pc>|<nonce>|<id>|accepted

CCP1|FACTORYRESET|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|code=043533477|proof=<64hex>
HMAC input: factory-reset|<device>|<pc>|<nonce>|<id>|043533477
CCP1|FACTORYRESETACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|status=accepted|proof=<64hex>
HMAC input: factory-reset-ack|<device>|<pc>|<nonce>|<id>|accepted
```

I quattro pacchetti restano entro 512 byte. Genero un ID casuale di 128 bit
e accodo una sola operazione per device. Attendo un FIND `events=2` fresco
autenticato con la chiave attiva; dopo READY ed EVENT invio il comando una sola
volta, all'endpoint osservato e con quel nonce. Il device risponde dalla porta
42117 alla 42118, ammette un ID una volta e non sposta la scadenza quando riceve
una copia identica. Tipo di comando, ID, nonce, chiave ed endpoint devono
corrispondere anche nell'ACK: non scambio riavvio e formattazione.

Aspetto al massimo cinque secondi dall'accodamento. Senza una trasmissione
ritorno `not_sent`; dopo un invio senza ACK valido ritorno `ambiguous`.
`accepted` prova soltanto l'ammissione. Non dichiaro verificati boot o
cancellazione e non ritrasmetto automaticamente. Dopo l'ACK il device attende
almeno 500 ms prima del riavvio; per il factory reset cancella le impostazioni
attraverso il relativo owner e riavvia solo dopo il successo. Un errore storage
rimane un errore e non viene nascosto dal riavvio. Il record PC puo' restare
salvato ma senza presenza: non lo elimino su un esito incerto e non avvio
pairing automatici. Il simulatore applica le stesse regole solo al proprio
store, con `include_simulator: true` esplicito e identita' sempre SIMULATED.

## Clock preferences and quiet hours

After an authenticated READY, physical firmware may advertise `CLOCKCAP`.
Each field order below is canonical; proofs use the active pairing key and
the exact ASCII values in the shown HMAC order. These packets remain bounded
to 512 bytes.

```text
CCP1|CLOCKCAP|device=<12hex>|pc=<32hex>|nonce=<32hex>|clock=1|initialized=<0|1>|proof=<64hex>
HMAC input: clock-cap|<device>|<pc>|<nonce>|1|<initialized>

CCP1|CLOCK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|op=<read|init|set>|timezone=<value>|enabled=<value>|start=<value>|end=<value>|proof=<64hex>
HMAC input: clock|<device>|<pc>|<nonce>|<id>|<op>|<timezone>|<enabled>|<start>|<end>

CCP1|CLOCKACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|op=<read|init|set>|status=<applied|rejected>|error=<decimal>|initialized=<value>|timezone=<value>|enabled=<value>|start=<value>|end=<value>|proof=<64hex>
HMAC input: clock-ack|<device>|<pc>|<nonce>|<id>|<op>|<status>|<error>|<initialized>|<timezone>|<enabled>|<start>|<end>
```

Timezone identities are `0` UTC, `1` Rome and `2` Ho Chi Minh City. Other
identities are rejected with a real error; an unsupported host timezone is
reported to the user and is never silently substituted. The RTC remains UTC.
Quiet hours use an explicit `enabled` switch (`0` or `1`) and local minutes
`start`/`end` in `0..1439`, forming a half-open window that may cross midnight.
Equal endpoints are rejected when quiet hours are enabled. Numeric fields use
canonical decimal without leading zeroes; `-1` means preserve the existing
setting, never infer a value.

`read` requires all four setting fields to be `-1`. `init` requires a supported
timezone and `enabled=-1|start=-1|end=-1`: automatic first connection adopts only
the host timezone and preserves all quiet-hours preferences. `set` requires at
least one explicit setting. The firmware retains a separate strict durable
`clock_adopted` marker across reconnects and restarts. An initialized device
answers `init` with its existing configuration without changing it. Explicit
`set` seals automatic adoption before changing preferences, so a partial failure
cannot make a later reconnect replace the user's configuration.

Quiet hours default to disabled only when the clock preference key is genuinely
absent. Existing enabled settings survive firmware updates. Disabling quiet
hours on an existing device requires an explicit `set` with `enabled=0`.
Manual Sounds, speaker volume and standby remain independent audio controls.

Only the exact active READY peer, fixed port and challenge are admitted. The
device consumes each CLOCK nonce once and rejects the most recent request ID
again. The host sends a mutation once and does not replay it after a lost ACK;
the result remains ambiguous until a separate authenticated read. ACK validation
retains the request's original nonce and endpoint, even if a newer poll arrives.

`applied` requires `error=0` and confirmed settings. `rejected` carries the original
validation, storage or publication error. When durable and runtime settings
cannot be confirmed together, all five ACK result fields (`initialized`,
`timezone`, `enabled`, `start`, `end`) are `-1`; unavailable values are never
replaced by defaults. A partial write may leave actual confirmed settings in a
rejected ACK, so inspect both its status and values. An ACK confirms the settings
operation, not physical audio output or clock synchronization. NVS operations
run through the settings executor outside the CCP state mutex.

## Resource budgets and security boundary

Ingress budgets apply before decoding, DPAPI unprotect or HMAC work. Source
identities come from the socket; an IP budget never substitutes for authentication.
Global budgets remain in force when spoofed sources churn the bounded peer table.

| Owner | Initial source limits |
| --- | --- |
| Firmware receive slice | At most 8 packets or 5 ms, followed by state/event maintenance |
| Firmware UDP ingress | 32 packets/s, burst 64 globally; 16 packets/s, burst 32 per IPv4; 16 fixed source slots |
| PC UDP ingress | 64 packets/s, burst 128 globally; 16 packets/s, burst 32 per IP; 64 source slots |
| Discovery | 16 offers globally, 4 per source, 128 received packets per bounded discovery pass |
| Pairing verification | At most 16 PBKDF2 candidates and a 2-second local verification budget |
| Endpoint challenges | 128 entries, 2-second monotonic lifetime, consumed once |
| Local control HTTP | Loopback only, bearer authentication, 32 concurrent workers, 3-second socket deadline, 64 KiB body |
| Rejection diagnostics | At most one summary per 5 seconds, with suppressed rejection count |
| Resident broker log | 2 MiB per file; active broker.log plus 2 rotated backups |

The exclusive loopback control listener is acquired before the resident owner
opens these rotating files. Losing startup processes cannot rotate the running
owner's log. The separate launcher `runtime.log` still retains startup failures
in append-only form; it no longer receives resident UDP rejection traffic.
Bounding that bootstrap history remains separate hardening work.

Ambiguous discovery identities or exhausted offer/packet budgets discard the
whole result with an explicit failure. Pairing work overflow fails before any
new candidate is persisted. Firmware receive budgeting prevents a permanently
non-empty socket queue from postponing maintenance indefinitely. Individual
blocking storage or crypto calls are not preempted by the receive time slice.
These initial values bound local resources; tuning and physical timing still
need separately authorized runtime verification. They cannot guarantee service
availability on a hostile or jammed network.

The six-character code is never transmitted, but its 30-bit search space and
public OFFER tag permit offline candidate checking. PBKDF2 increases the work
per guess; it does not remove the offline oracle. A captured full pairing
transcript also contains the public fields needed to derive the resulting link
key after the code is recovered. Expiry and online rate limits do not invalidate
such a captured transcript. No attack-cost benchmark or security certification
has been performed.

CCP authenticates ASCII/hex payloads with HMAC; it does not encrypt them. A LAN
observer able to capture unicast traffic can read project names, purpose,
questions and technical metadata. Replay hardening does not provide
confidentiality or protection against a compromised local computer. Windows
DPAPI protects persisted keys for its current-user boundary, not against code
already executing as that user.

An evaluated standard PAKE or high-entropy QR secret, followed by authenticated
encryption with explicit session/replay ownership, remains deferred. Do not
introduce custom PAKE or present this commissioning protocol as equivalent to
TLS, SPAKE2 or a production security audit. Stable device and PC identities can
be retained when that coordinated future protocol is designed.
