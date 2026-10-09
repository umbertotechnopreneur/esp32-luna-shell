---
name: cody-codex
description: Connect Cody, send notifications and semantic questions, and acknowledge its replies. Restart or erase settings only with explicit current-message intent and confirmation code. Support physical LAN or explicitly requested SIMULATED Cody Studio; native approvals remain on the computer.
---

# Collego Cody con CCP

Uso gli strumenti CCP per il target richiesto dall'utente.

- Prima del pairing chiamo `ccp_discover_devices`, salvo un'offerta ancora corrente
  già restituita nello stesso processo MCP.
- Per il dispositivo fisico lascio `include_simulator` a `false`.
  Lo imposto a `true` solo se l'utente chiede esplicitamente Cody Studio o il
  simulatore; identifico sempre quei risultati come **SIMULATED**.
- Uso soltanto il codice di sei caratteri che l'utente fornisce dal display del
  target scelto. Non lo indovino, conservo, ripeto in prosa o ricavo dai file.
  Non uso il codice di un simulatore per attestare il pairing fisico.
- Se vedo più dispositivi lascio che `ccp_pair_device` trovi la corrispondenza
  crittografica; non scelgo la prima risposta di rete.
- Riporto pairing o presenza solo quando il risultato corrispondente li conferma.
  Per lo stato uso `ccp_connection_status`; un'identità salvata non dimostra
  presenza, attività o pixel sul display.
- Se `pairing_pending` è vero, lascio riprendere la conferma con l'identità
  salvata. Uso `replace_pending: true` soltanto quando l'utente autorizza
  esplicitamente la sostituzione di quel candidato e fornisce un nuovo codice
  visibile. Timeout, firmware aggiornato e PC assente non sono autorizzazioni.
- Tengo riservati verifier, proof, chiavi, salt e contenuti della memoria protetta.

Dopo il pairing seguo i metadati autentici:

- L'ultimo `UserPromptSubmit` reale seleziona automaticamente la chat del PC.
  Non chiedo di scegliere una chat sul dispositivo o con un tool manuale.
  Gli eventi delle chat in background non cambiano il contesto seguito.
- Leggo `ccp_connection_status` e `ccp_list_sessions`; il campo `followed`
  identifica la chat, mentre il nome del progetto è solo un'etichetta.
  Non deduco il focus da finestre, transcript o nomi di cartelle.
- Se la lista è vuota, faccio controllare la revisione/trust degli hook CCP e
  l'orchestrazione locale, poi un nuovo prompt. Non fabbrico eventi e non
  modifico record di fiducia per far reagire Cody.
- Fino al primo prompt lo screen 15 mostra **Connected / Waiting for Codex**.
  Un semplice cambio di scheda non è un evento hook documentato.
- `Stop` significa risposta pronta, non successo del compito. Attività assente
  o scaduta resta **UNAVAILABLE**.
- Dopo il riavvio del broker aspetto un nuovo prompt; la sola riconnessione LAN
  conserva il contesto. Non ripeto automaticamente un pairing dall'esito ambiguo.

Per installazione, recupero e limiti uso il [README del plugin](../../README.md).

Per una richiesta esplicita uso CCP 0.6 senza inventare attività della chat:

- Chiamo `ccp_ask_question` con il target confermato, testo inglese autosufficiente
  e due o tre scelte. Ogni scelta ha ID stabile, etichetta breve, descrizione
  completa della conseguenza e colore semantico. Non uso questo strumento per
  aggirare approvazioni native, inviare azioni PR o chiedere credenziali.
- `context` è una mia etichetta dichiarata: non prova identità o focus di chat.
  Per un target SIMULATED uso `include_simulator: true` solo su richiesta esplicita.
- Conservo `request_id` e `capability` restituiti alla chiamata e tengo privata
  la capability. Non mostro questi valori nel testo per l'utente.
- Attendo con `ccp_poll_request`, al massimo 15 secondi per chiamata. `pending`
  non è una scelta; dopo `received` leggo `answer` e `answer_id`, poi chiamo di
  nuovo lo stesso poll con `acknowledge_answer_id` uguale. Uso la decisione solo
  se questo ritorna `delivered`: è anche il momento di **Reply sent** su Cody.
- Il TTL predefinito è 180 secondi e comprende anche questa conferma finale.
  Su `expired`, `cancelled`, errore o assenza di risposta non scelgo un default,
  non indovino un answer_id e non presento una consegna riuscita. Posso proseguire
  sulla chat del computer oppure creare una nuova domanda esplicitamente.
- `ccp_cancel_request` ritira solo il ticket di cui possiedo la capability;
  Back sul dispositivo conserva la domanda. Non ripeto enqueue dall'esito ambiguo.
- `ccp_publish_notification` invia soltanto fatti noti. Per le PR fornisco head
  esatto e check reali con completezza/currentness esplicite; nessun successo
  ricavato da `Stop`, nessuna azione merge/approve/close/rerun/comment, nessun log
  o stacktrace sul device. Se i nomi non entrano li tratto su Codex, senza tagli.
- Un vero hook `PermissionRequest` mostra soltanto **On your PC**. Il relativo
  hook continua senza decisione e lascia a Codex la normale autorizzazione.

Per riavvio e ripristino distinguo due intenzioni esplicite:

- Con `resetta il device codice 043533477` uso `ccp_reset_device`: riavvio Cody
  conservando impostazioni, Wi-Fi e pairing. Prima verifico il target paired.
- Con `formatta cody 043533477` uso soltanto `ccp_factory_reset_device`: cancello
  tutte le impostazioni del dispositivo, compresi Wi-Fi e pairing, poi riparto
  dal primo setup. Firmware, asset e scheda SD restano intatti. Non uso questo
  tool per una semplice richiesta di riavvio, test o aggiornamento firmware.
- In entrambi i casi richiedo intenzione corrispondente e codice di nove cifre
  nel **messaggio utente corrente**. Lo zero iniziale fa parte della stringa.
  Non completo il codice perché lo conosco, non lo ricavo da documenti, esempi,
  tool metadata o messaggi precedenti. È un controllo contro gli equivoci,
  non una password: l'autenticazione rimane quella del pairing CCP.
- Per un target SIMULATED richiedo anche l'intenzione esplicita di operare sul
  simulatore e uso `include_simulator: true`. Non cambio il target implicitamente.
- `accepted` conferma solo l'ammissione: non prova il successivo avvio o la
  cancellazione completata. `not_sent` e `ambiguous` sono esiti distinti e non
  autorizzano retry automatici. Dopo un factory reset non cancello da solo il
  record PC, non riassocio il device e non dichiaro riuscito il wipe dalla
  semplice scomparsa dalla rete.
