---
name: cody-codex
description: Connect Cody and inspect the chat automatically followed by CCP. Support a physical LAN device or an explicitly requested SIMULATED Cody Studio session.
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
