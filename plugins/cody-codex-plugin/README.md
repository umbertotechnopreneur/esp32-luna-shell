# CCP - Cody's Codex Plugin

Ho chiamato questo pezzo **CCP**: collego Cody a Codex dalla stessa LAN, con un
piccolo omaggio nel nome e senza chiedere alla nuvola di diventare un server.
Uso Python standard su Windows, MCP locale per Codex e CCP1 per il dispositivo.
Mi bastano Wi-Fi e IP sulla LAN: l'accesso a Internet non decide il pairing.

CCP 0.6.2 richiede il firmware con EVENTACK e FIND `events=2`: prima di mostrare una seconda
domanda attendo la chiusura autenticata della prima sul dispositivo. La conferma
dell'agent e quella di Cody restano distinte; i dettagli sono nel
[contratto CCP1](protocol/CCP1.md). Questo checkpoint aggiorna i sorgenti;
l'installazione del pacchetto e il caricamento del firmware sono operazioni separate.
Il FIND dichiara anche la domanda corrente autenticata: dopo il riavvio del
broker ritiro quella non piu' disponibile prima di inviare una nuova domanda.

`mcp/ccp_windows.py` possiede DPAPI, percorsi utente, esclusivita' Winsock e
avvio nascosto. Trasporto e broker mantengono il formato delle chiavi salvate.
Il runtime residente resta Windows: Linux/macOS richiedono un proprietario
esplicito delle credenziali e dell'avvio, senza sostituti in chiaro.

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

Before the first prompt, screen 15 shows **Connected / Ready to team up**,
with **Start a chat in Codex** as the next step. This confirms the PC link,
without claiming a followed chat or active work.
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
| `ccp_ask_question` | Invio una domanda autosufficiente con due o tre scelte e conservo ticket e capability privati. |
| `ccp_publish_notification` | Invio un fatto noto o uno snapshot PR in sola lettura, con metadati espliciti dello stesso head. |
| `ccp_poll_request` | Leggo la risposta e confermo la ricezione con un secondo poll e `acknowledge_answer_id`. |
| `ccp_cancel_request` | Ritiro il mio ticket senza scegliere un'opzione. |
| `ccp_reset_device` | Riavvio il target paired su richiesta esplicita con codice, conservando le impostazioni. |
| `ccp_factory_reset_device` | Cancello le impostazioni e riparto dal setup solo con la distinta intenzione di formattare e il codice. |

Distinguo sempre `LAN`, `SIMULATED` e `UNOBSERVED` nei risultati. Lo stato MCP
non dimostra quali pixel siano comparsi sul display fisico.

## Una domanda fa il giro completo

Con CCP 0.6 chiedo a Codex di inviarmi una domanda su Cody. Non mi servono hook
per questa chiamata MCP esplicita: il destinatario è il ticket privato della
richiesta, non una chat dedotta dal nome del progetto. `context` resta una mia
etichetta dichiarata. Sullo screen 26 apro la domanda, sul 27 scelgo una card,
leggo la descrizione e premo **Send**. Posso tornare indietro e cambiare scelta
prima di inviare; una perdita di LAN conserva domanda e percorso.

Il primo ACK UDP significa soltanto che CCP ha ricevuto la scelta. Codex legge
la risposta con il ticket e la capability, poi conferma `answer_id` in un
secondo poll. Solo allora Cody mostra lo screen 31 **Reply sent**. Il TTL
predefinito di 180 secondi comprende anche questo ultimo passo: scadenza,
cancellazione e assenza di rete non inventano una risposta. Il poll può
attendere al massimo 15 secondi per chiamata; non riattiva da solo una chat
sospesa. Dopo un riavvio del broker creo una nuova richiesta esplicita.

Tengo una sola domanda aperta per dispositivo. Le notifiche successive
sostituiscono le precedenti, senza scavalcare la domanda; così una vecchia PR
rossa non ricompare dopo uno snapshot verde. Per i check uso nomi interi e
conteggi solo da un elenco completo, corrente e dello stesso head. Se supero
sei fallimenti o 48 caratteri per nome, torno a Codex: niente nomi mozzati.
Gli screen 21–24 sono di sola lettura; lo screen 25 rimane ritirato.

Un vero `PermissionRequest` può mostrare **Approval needed / On your PC**.
Lascio l'approvazione nativa nel normale prompt Codex e non spedisco comandi,
argomenti privati, log o stacktrace al display. I dettagli wire stanno nel
[contratto CCP1](protocol/CCP1.md); capacità e limite di 1280 byte riguardano
soltanto il nuovo EVENT, mentre il pairing conserva il limite di 512 byte.

## Riavvio o ricomincio dal setup

Scrivo **resetta il device codice 043533477** quando voglio solo riavviare:
`ccp_reset_device` conserva Wi-Fi, pairing e preferenze. Scrivo invece
**formatta cody 043533477** quando voglio cancellare tutte le impostazioni e
ripartire dal primo setup: `ccp_factory_reset_device` elimina anche Wi-Fi e
pairing, preservando firmware, asset e SD. Sono due comandi diversi, mica due
nomi per la stessa sorpresa.

Intenzione e codice devono comparire nel messaggio utente corrente. La stringa
ha nove cifre, zero iniziale incluso; evita equivoci e non aggiunge una password
al protocollo. Uso sempre il collegamento CCP già autenticato. `accepted`
significa che Cody ha ammesso il comando, non che il successivo avvio o il wipe
siano stati verificati. CCP invia una volta e aspetta al massimo cinque secondi:
senza invio restituisce `not_sent`, dopo un invio senza conferma `ambiguous`.
Non ripeto automaticamente un esito incerto. Dopo il factory reset il vecchio
record PC può restare memorizzato, senza presenza; la nuova configurazione e
il nuovo pairing richiedono passaggi espliciti.

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
