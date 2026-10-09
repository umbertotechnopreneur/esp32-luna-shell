# CCP - Cody's Codex Plugin

Ho chiamato questo pezzo **CCP**: collego Cody a Codex dalla stessa LAN, con un
piccolo omaggio nel nome e senza chiedere alla nuvola di diventare un server.
Uso Python standard su Windows, MCP locale per Codex e CCP1 per il dispositivo.
Mi bastano Wi-Fi e IP sulla LAN: l'accesso a Internet non decide il pairing.

## Installo CCP una volta

1. Rendo `python` disponibile nell'ambiente dell'app desktop.
2. Dalla radice di questo repository registro il catalogo locale, se manca:
   `codex plugin marketplace add .`.
3. Nel catalogo **Cody local plugins** dell'app installo e abilito
   **CCP - Cody's Codex Plugin**, poi apro una chat che carichi il plugin aggiornato.
4. In una sessione Codex locale apro `/hooks`, leggo e autorizzo le definizioni
   di CCP. Installare il plugin e fidarmi dei suoi hook sono due operazioni diverse.
5. Invio un vero messaggio nella chat: da quel momento CCP può seguirne l'attività.

Uso il flusso di [installazione dei plugin locali](https://developers.openai.com/plugins/build/plugins)
e la [revisione degli hook](https://learn.chatgpt.com/docs/hooks) documentati da OpenAI.
Gli hook di comando del plugin richiedono orchestrazione locale: con orchestrazione
cloud non li considero disponibili, anche se alcuni strumenti girano sul PC.
Se la lista delle chat osservate resta vuota, controllo ambiente e trust; non
creo eventi finti e non modifico a mano i record di fiducia.

Codex avvia il servizio dal pacchetto installato, non dalla cartella del progetto.
L'entrypoint è `python -B <plugin-root>/mcp/ccp_server.py --stdio`.
Il plugin non richiede una chiave API OpenAI e non distribuisce credenziali Codex
sulla LAN. Per il dispositivo fisico consento a Python il traffico UDP privato
sulle porte 42117 e 42118.

## Collego Cody e mantengo il collegamento

Sul dispositivo apro **Setup → Connect Codex**. Alla prima associazione comunico
a Codex il codice visibile sullo screen 12; CCP fa discovery e trova l'offerta che
corrisponde crittograficamente a quel codice. Uno stato salvato non basta:
considero il pairing concluso dopo la conferma e la connessione viva dopo la
risposta autenticata del PC.

Se Cody è già associato scelgo **Keep pairing** per riprendere il collegamento.
La sostituzione richiede una conferma esplicita; un PC momentaneamente assente,
un cambio di IP o un normale aggiornamento firmware non richiedono un nuovo codice.
**Cancel** ritira la nuova operazione mantenendo il precedente record su Cody.
Al successivo scambio autenticato anche CCP può servire il vecchio collegamento.
Il candidato PC resta esplicitamente in attesa: una prova della vecchia chiave
non autorizza a cancellare una nuova associazione ancora da confermare.
Un candidato già salvato può riprendere la conferma dopo il riavvio; un errore di
memoria rimane visibile e non viene scambiato per primo avvio.

Tengo separati il dispositivo e il PC: le identità e le chiavi protette di CCP
stanno in `%LOCALAPPDATA%\CodyCodexPlugin\state.json`. Uso DPAPI dell'utente Windows.
Prima di inviare PAIR salvo il candidato in `pending-pairings.json`, nella stessa
directory; sostituisco il record attivo solo dopo ACTIVE o COMMITTED autenticati.
Il vecchio record resta disponibile durante il tentativo. Dopo Cancel o Forget,
se sul PC rimane un candidato, con un nuovo codice visibile autorizzo
`replace_pending: true` per sostituirlo.
Un semplice timeout non concede questa autorizzazione. Nello stato distinguo
`pairing_pending` da `retained_pairing`.
Dimenticare l'associazione su Cody non cancella automaticamente il record del PC.
Il dettaglio della transazione, incluso il limite tra due memorie indipendenti,
sta nel [protocollo CCP1](protocol/CCP1.md).

## La chat arriva da Codex

Faccio seguire a Cody l'ultima chat in cui invio un prompt reale.
Gli eventi di tool, completamento o avvio di altre chat non rubano il contesto.
Aprire una scheda senza scrivere non produce un segnale di focus documentato:
non leggo finestre, transcript o testo dei messaggi per indovinarlo.

Fino al primo prompt lo screen 15 mostra **Connected / Waiting for Codex**.
Trasmetto fase del lavoro, identità della chat e nome breve del progetto.
Due chat dello stesso progetto conservano identità diverse. Un evento `Stop`
significa risposta pronta; non lo presento come prova che il compito sia riuscito.
Una fase attiva senza aggiornamenti diventa **UNAVAILABLE** dopo 120 secondi.
Un riavvio del broker aspetta un nuovo prompt; una semplice riconnessione LAN
riporta lo snapshot corrente.

Un solo broker nascosto serve le chat del PC. I client MCP rinnovano le proprie
lease; chiudere una chat non spegne le altre. Dopo un errore di trasporto riprovo
con attese di 1, 3, 10 e al massimo 30 secondi, autenticando la stessa identità.
Conservo gli errori fino al recupero e non ripeto automaticamente tool o pairing
dal risultato ambiguo. Un'identità o una versione diversa richiede un intervento
esplicito. Dopo 60 secondi senza lease o hook il broker termina.

## Strumenti che espongo

| Strumento | Cosa osservo o cambio |
| --- | --- |
| `ccp_discover_devices` | Offerte LAN correnti. `include_simulator` è `false` di default. |
| `ccp_pair_device` | Associo il solo target che corrisponde al codice visibile; `replace_pending` richiede consenso esplicito. |
| `ccp_connection_status` | Identità, presenza autenticata e ultimo snapshot del broker. |
| `ccp_list_sessions` | Metadati delle chat osservate e campo `followed`; nessun selettore manuale. |

Distinguo sempre `LAN`, `SIMULATED` e `UNOBSERVED` nei risultati. Lo stato MCP
non dimostra quali pixel siano comparsi sul display fisico.

## Collego anche il simulatore

Dalla radice del repository apro `.\scripts\device.ps1 simulator -Ccp`.
Nel pannello **CCP** scelgo **Connect Codex** e leggo il codice visibile.
In Codex chiedo esplicitamente di collegare **Cody Studio SIMULATED**:
la discovery usa `include_simulator: true`, poi il pairing usa quel codice.

Il simulatore parla davvero CCP1 su `127.0.0.1:42117`, riceve gli snapshot del
broker e mostra **SIMULATED**. Conserva la propria identità DPAPI separata in
`artifacts/cody-simulator-state/device.json`; il cleanup dei build la preserva.
Nel pannello posso sostituire, annullare, dimenticare e interrompere la LAN simulata.
Non pubblico questi dati negli snapshot del firmware. Per controlli e limiti
della preview uso il [README del simulatore](../../simulator/windows/README.md).

## Verifiche ed evidenza

Preparo i comandi pubblici e li eseguo solo dopo l'autorizzazione prevista
da [AGENTS.md](../../AGENTS.md):

```powershell
.\scripts\device.ps1 check -Check cody-ccp
.\scripts\device.ps1 check -Check cody-ccp-simulator
```

Le suite usano fixture dichiaratamente sintetiche. La suite del simulatore include
uno scambio UDP su porte temporanee di loopback; non apre finestre o chat reali,
non invia broadcast LAN e non legge la memoria protetta dell'utente. Tengo l'esito delle verifiche in
[done.md](../../done.md) e il lavoro ancora da verificare in
[TODO.md](../../TODO.md), senza duplicare qui una cronologia destinata a invecchiare.
