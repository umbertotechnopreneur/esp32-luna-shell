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

## Notifiche e decisioni autentiche (CCP 0.6)

Tengo separati i metadati automatici della chat e le richieste esplicite MCP.
Per una richiesta MCP genero un ticket casuale di 128 bit e una capability di
256 bit, consegnata solo alla chiamata creatrice. `context` e' un'etichetta
dichiarata dal chiamante, non l'identita' o il focus di una chat. Non invento
hook per spedire una domanda. Un vero `PermissionRequest` produce soltanto un
avviso `On your PC`: l'autorizzazione nativa rimane nel normale prompt Codex.

```text
CCP1|FIND|device=<12hex>|pc=<32hex>|nonce=<32hex>|activity=1|events=1|proof=<64hex>
HMAC input: find-events|<device>|<pc>|<nonce>

CCP1|EVENT|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|state=<state>|ttl=<seconds>|data=<hex or ->|proof=<64hex>
HMAC input: event|<device>|<pc>|<nonce>|<id>|<state>|<ttl>|<data>

CCP1|REPLY|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|choice=<choice-id>|proof=<64hex>
HMAC input: reply|<device>|<pc>|<nonce>|<id>|<choice>

CCP1|REPLYACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|choice=<choice-id>|status=<status>|proof=<64hex>
HMAC input: reply-ack|<device>|<pc>|<nonce>|<id>|<choice>|<status>

CCP1|EVENTACK|device=<12hex>|pc=<32hex>|nonce=<32hex>|id=<32hex>|state=<terminal-state>|proof=<64hex>
HMAC input: event-ack|<device>|<pc>|<nonce>|<id>|<state>
```

L'estensione `events=1` richiede anche `activity=1`; entrambe hanno valore
esatto `1`. Il dominio HMAC distingue questa richiesta dalle due forme FIND
precedenti. Mantengo il limite di 512 byte per tutti i pacchetti, salvo EVENT
che ammette al massimo 1280 byte. Non frammento i messaggi applicativi.

EVENT e' uno snapshot completo e immutabile del contenuto del ticket, firmato
con la chiave del link e il nonce FIND corrente. Ammetto una sola pubblicazione
EVENT per nonce e solamente dopo READY. `state` e' `pending`, `received`,
`delivered`, `cancelled`, `expired` oppure `none`. Il caso `none` usa ID tutto
zero, `ttl=0` e `data=-`; non costituisce una risposta. `ttl` e' il numero
intero di secondi residui arrotondato per eccesso (0..180), non un timestamp
UTC; `pending` e `received` richiedono almeno un secondo. Una ripetizione non
allunga la scadenza locale originale. Offline conservo la domanda e il suo
percorso: nessun timeout, riavvio o ritorno LAN sceglie un'opzione.

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
Il firmware che implementa EVENTACK e CCP 0.6.1 vanno aggiornati insieme;
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
e accodo una sola operazione per device. Attendo un FIND `events=1` fresco
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

## Security boundary

The six-character code is never sent in a datagram. PBKDF2 makes captured
offers more expensive to brute-force, and every successful pairing rotates the
salt, nonce and link key. This compact commissioning protocol is not a formal
PAKE and must not be presented as equivalent to TLS, SPAKE2 or security
certification. A later production review can replace commissioning without
changing the stable device and PC identities.
