# Cody: primo avvio, opzioni e collegamento

Qui tengo il contratto corrente del percorso di Cody. Uso gli screen ID degli
atlanti come riferimenti stabili; lascio cronologia e risultati in
[done.md](../../../done.md), verifiche aperte in [TODO.md](../../../TODO.md) e
lezioni in [lessons.md](../../../lessons.md). Il primo schema aveva mescolato
commissioning, riconnessione e attivita' della chat: ho separato quei tre fatti.

Per installazione e hook rimando alla [guida CCP](../../../plugins/cody-codex-plugin/README.md).
Per pacchetti, prove e tempi del protocollo uso
[CCP1](../../../plugins/cody-codex-plugin/protocol/CCP1.md), senza mantenerne una
seconda copia qui. I PDF aggiornano anche le pagine principali e conservano gli
addenda e le errata precedenti in fondo.

## Decido il percorso dai dati salvati

Dopo branding e checkpoint locali leggo una sola osservazione canonica. Non uso
`cody_firstboot` come scorciatoia per dichiarare rete o PC pronti.

| Osservazione reale | Percorso |
| --- | --- |
| Profilo Wi-Fi veramente assente | 04 / Welcome e provisioning 05-09 |
| Credenziali presenti, associazione o IP assenti | 42 / No connection: Network setup oppure Later |
| Wi-Fi e IP validi, pairing Codex assente | 40 / Connect Codex?: Yes oppure No |
| Pairing conservato e valido | 14 / Looking for PC, poi 15 solo dopo READY autenticato |
| Lettura, tipo, CRC o storage falliti | Mantengo l'errore; non li trasformo in un profilo assente |

Mi basta la **LAN con Wi-Fi e IP funzionanti**. Un'eventuale perdita di Internet
puo' influire sui servizi online di Codex, ma non e' una ragione per rifare il
pairing locale. Con No torno a Idle senza avviare CCP o salvare un rifiuto. Al
boot successivo posso riproporre la scelta se manca ancora l'associazione.

La normale configurazione usa `CONFIG_LUNASHELL_CODY_FORCE_FIRST_BOOT=n`.
Mantengo `y` come replay di sviluppo esplicito. Lo standby software conserva il
percorso; un vero reset/deep sleep ricostruisce lo stato dalle osservazioni e
dai dati persistenti, senza fingere che la RAM precedente esista ancora.

## Percorso e ritorni

| Screen e azione | Risultato |
| --- | --- |
| 04, introduzione completata | 05 / Let's pair |
| 05, Show QR | 06 / QR di installazione; nessuna azione radio |
| 06, Pair Cody ammesso | 07 / Starting Bluetooth |
| 07, ReadyToScan del tentativo corrente | QR di sessione dentro 07 / Scan to pair |
| 07, credenziali o connessione in corso | Ritiro il QR e mostro il progresso reale |
| 07, Cancel | Ritiro il QR, attendo la chiusura confermata, torno a 06 |
| 08, Retry | Riprendo il passaggio corretto dopo cleanup riuscito |
| Online e Wi-Fi/IP validi | Scrivo e rileggo `cody_firstboot=false` |
| Handoff completato in Debug | 39 / dati reali copiati; un nuovo tap continua |
| Handoff completato in Release | Entro direttamente in 09 |
| 09, Continue | 10 / Make it yours |
| 10, Next | 36 / Settings |
| 36, Continue | 11 / Skin |
| 11, scelta esplicita e Done | Torno al chiamante; nel setup iniziale valuto il link Codex salvato |
| 40, Yes senza pairing | 12 / preparazione e visualizzazione del codice |
| 12, handshake a due lati completato | 13 / You're all set |
| 13, Let's go | Cerco subito il PC su 14 o riutilizzo una presenza valida su 15 |
| 14, READY autenticato | 15 / Connected |
| 15, nuovo prompt reale ricevuto da CCP | Attivita' della chat seguita automaticamente |

`06-bis` rimane un'identita' nei dati d'autore; nel runtime attuale il QR di
sessione e' una presentazione dello screen 07. Ho rimosso la pausa artificiale
di dieci secondi su 16 dopo Let's go: la conferma deve portare al collegamento,
senza una passeggiata extra in Idle.

`CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX` resta un gate esplicito, normalmente `y`.
Con `n` il primo setup salta il collegamento Codex. Le scelte di rete continuano
ad appartenere al loro owner.

Tengo le viste di provisioning entro questi slot logici, con rendering e touch
nella stessa rotazione:

| Vista | Geometria e ammissione |
| --- | --- |
| 05 | Muovo Cody da `(233,225,130)` a `(233,105,65)` in 1.000 ms, attendo 300 ms, poi avvio il testo; durante movimento e attesa non ammetto azioni |
| 06 | Fondo bianco, Cody `(233,94,58)`, QR installazione 240 x 240 in `(113,140)` |
| 07 | Cody `(233,80,46)`, titolo y=131, QR sessione 204 x 204 in `(131,182)` soltanto quando ReadyToScan |
| 05/06 | Capsule Show QR / Pair Cody in `(123,390,220,44)`; nessun target laterale invisibile |

Su 09 ricontrollo Wi-Fi/IP in tempo reale. Se la rete cade sostituisco la
conferma con No connection o UNAVAILABLE; dopo wake non attivo Continue al
posto dell'utente. Se il pairing di rete e' gia' Online e fallisce solo la
scrittura di `cody_firstboot`, Retry riprova quella scrittura senza avviare
un altro provisioning BLE.

## Apro Setup anche quando il PC manca

Tengo fermo il dito sul personaggio visibile per **1,5 secondi** e apro 41 / Setup.
Movimento oltre 12 pixel, gap reale tra campioni oltre 250 ms, rotazione o una
transizione di alimentazione annullano il candidato. Dopo il risveglio conto dal
primo campione fresco con personaggio visibile; il tempo al buio non vale.
Il contatto che apre il menu deve terminare prima che una scelta sia ammessa.

Conservo l'ingresso dalle viste stabili, da Looking for PC e dagli errori CCP
locali recuperabili. Un'operazione attiva di provisioning o cleanup mantiene
il proprio owner fino a chiusura: il gesto non scavalca quel vincolo.

Metto **Connect Codex** e **Network setup** sulla stessa riga e **Options** sotto.
Network setup riapre 04-09. Options entra nel percorso locale 10/36/11.
Back torna al percorso associato senza far ripartire inutilmente una discovery
gia' in corso o una connessione autenticata ancora valida.

Con un pairing conservato, Connect Codex mostra **Already paired**, anche se la
LAN e' momentaneamente assente:

- **Keep pairing** conserva il record e riusa Connected/Discovering quando lo
  stato e' valido; altrimenti riprende la ricerca del PC salvato.
- **Pair again** e' consenso esplicito a una sostituzione; ammetto il nuovo
  codice quando la rete richiesta e' disponibile.
- **Forget**, centrato sotto la coppia, apre una seconda conferma dedicata.

Con un candidato incompleto uso il titolo **Pairing paused**. Con un errore
storage, incluso lettura o CRC, uso **Pairing error** e abilito solo la cancellazione
esplicita: un record corrotto non equivale a un dispositivo mai associato.

Aprire la conferma, tornare indietro o premere Retry non autorizza a cambiare PC.
Il backend distingue l'azione di primo pairing dall'azione di sostituzione,
controlla l'identita' del tentativo e rifiuta conferme diventate obsolete.

## Options e Skin condividono il chiamante

Da Setup, Options apre Make it yours. Mantengo il contesto di navigazione in
modo che Done ritorni a 41, Back da 10 ritorni a Setup e Back da 36 ritorni a 10.
La visita alle preferenze non avvia network setup o commissioning Codex.
Nel primo avvio, Done dal percorso Skin continua invece verso la decisione
sul link salvato.

Su 10 rendo operative quattro righe compatte, con valore sempre visibile:

| Preferenza | Scelte e vincoli |
| --- | --- |
| Shake to wake | On / Off, persistente |
| Auto-rotate | On / Off, persistente |
| Sounds | On / Off, gate audio master persistente |
| Orientation | 0 / 180 solo con Auto-rotate Off; mostro Auto altrimenti |

Con Sounds Off fermo la riproduzione; lascio microfono e wake word attivi.

**Next** continua a Settings 36 e **Continue** apre Skin 11. Durante una richiesta
di salvataggio tengo distinta l'ammissione dal risultato. Confermo il valore solo
dopo scrittura e rilettura riuscite; l'errore rimane visibile e non invento un
successo. Con Retry ripropongo lo stesso valore richiesto. Le letture persistenti
invalide mantengono la propria causa, senza
correzione automatica o sostituzione con default.

Sul selettore 11 uso l'anteprima `(233,172,44)`, titolo a y=78 e viewport
`(117,226,232,146)`. Le righe sono 220 x 38 con passo 44, capacita' otto e scroll
senza inerzia. Gli aspetti disponibili sono Pearl Mist, Aurora Veil, Cosmic
Stardust, Enchanted Mosaic e Dreamwave. Salvo gli ID stabili, non il nome o
l'ordine della lista. Un valore `cody_skin` invalido resta un errore.

Copia della richiesta, persistenza e risultato passano dall'action worker e
dall'esecutore settings con stack interno. Durante Saving mantengo la vecchia
anteprima, blocco una seconda scrittura e rendo visibile l'esito reale. Il
renderer applica lo skin solo dopo il salvataggio riuscito, tra frame conclusi.
`DeviceSnapshot.character_look_save` espone ID, request ID, Saving/Saved/Failed,
errore e timestamp originali. Ritirare una vista non riapre il suo percorso
quando arriva un risultato in ritardo.

Durante il primo setup chiedo una scelta esplicita anche se Pearl Mist e'
gia' mostrato. La selezione della stessa riga vale come conferma; una nuova
selezione abilita Done solo dopo il salvataggio riuscito.

## Codice visibile e tempi distinti

Su 12 mostro **Preparing secure code / Please wait / About 30 seconds** finche'
il codice non esiste. La stima deriva dalla precedente osservazione, non e'
progresso misurato. Quando e' pronto, mostro Enter code in Codex e tutti e sei
i caratteri con il font del titolo: centro y=338, bounds 109 x 40. Il codice
non entra nei log, nei file, nello snapshot pubblico, nell'HTTP o nei datagrammi.

Uso l'owner CCP a priorita' 1 su CPU0. La derivazione mantiene gli stessi
50.000 passi PBKDF2-HMAC-SHA256 con operazioni PSA; cedo un tick tra slice di
circa dieci millisecondi, senza trattenere lock di servizio o snapshot. Registro
solo fasi, tempi e cause di errore, senza materiale di pairing.

Distinguo la finestra backend di cinque minuti dal timeout di standby dello
screen. `standby_override_seconds=180` viene riancorato una volta quando il
codice pronto e' copiato nella presentazione. Tengo quindi tre minuti dal
codice disponibile; il touch rinnova l'inattivita' e uscire ripristina la policy
ordinaria. Questi tempi non richiedono una nuova preferenza persistente.

Dal codice pronto mostro **Cancel** nella capsula compatta `(123,390,220,44)`.
Durante la derivazione attendo la conclusione dell'operazione. Cancel chiude
il codice e scarta l'eventuale candidato, conservando il link attivo precedente.
Aspetto l'esito del worker e tengo visibile un errore reale di cancellazione.

## Confermo, recupero o dimentico il pairing

Ho separato il record attivo `ccp_pairing` dal candidato `ccp_pending` su Cody,
conservando lo schema v1 dell'attivo. Anche sul PC tengo separati `state.json`
e `pending-pairings.json`, entrambi protetti con DPAPI. Salvo il candidato PC
prima del primo PAIR: posso cosi' recuperare anche se tutte le risposte PAIRED
vanno perse. PAIR salva il candidato Cody senza sostituire il precedente attivo;
PAIRED ne prova il salvataggio. CONFIRM prova la persistenza del candidato PC:
promuovo allora quello Cody, verifico la rilettura e rimuovo il pending.
Solo dopo l'invio riuscito di ACTIVE mostro All set; il tool PC conclude con
successo quando riceve ACTIVE autenticato. Una persistenza o cancellazione
fallita resta un errore, senza All set anticipato.

Dopo un reboot con candidato incompleto cerco il PC usando quel candidato.
Una risposta READY fresca e autenticata prova che il PC possiede il link e
permette di completare la promozione. Aggiungo a CCP1 l'ACK COMMITTED: dopo ogni
READY valido, con attivo scritto e riletto, confermo il commit al PC. La prova
usa identita', sorgente e nonce del FIND recente (dieci secondi). Il PC promuove
su ACTIVE oppure COMMITTED validi; FIND da solo non basta. Ripetendo COMMITTED
anche per un attivo recupero l'ACK perso al giro seguente. Se manca la prova,
rimango in attesa o nell'errore reale; non invento la conferma e non impongo
subito un altro codice.

Non prometto un rollback atomico distribuito. Cancel conserva il vecchio attivo
sul Cody e permette di usarlo ancora, ma lascio il candidato PC irrisolto.
Un COMMITTED con la vecchia chiave non lo cancella: potrebbe essere un pacchetto
vecchio riprodotto. Anche dopo Cancel, Forget o una prima associazione interrotta
richiedo un nuovo codice visibile e `replace_pending:true` per sostituire quel
candidato esplicitamente. Espongo `pairing_pending` e `retained_pairing` come
stati distinti. Il dettaglio dei messaggi resta nel [protocollo CCP1](../../../plugins/cody-codex-plugin/protocol/CCP1.md).

**Forget pairing** presenta **Go back / Forget**, con **Only Codex is reset**.
Cancello sul Cody soltanto `ccp_pairing` e `ccp_pending`. Conservo Wi-Fi, skin,
preferenze e log; il PC conserva il proprio record protetto, senza presenza
autenticata del dispositivo. Questa azione non invia una revoca remota e non
e' un factory reset.

Solo dopo l'esito noto della cancellazione mostro **Unpaired / Pairing removed**.
**Done** torna a Setup. Uso la stessa scelta esplicita per rimuovere un record
CCP danneggiato; non faccio repair automatici o cancellazioni globali di NVS.

## Recupero il PC senza rifare commissioning

Il record conservato identifica il PC anche se cambiano IP o disponibilita'
della rete. Quando la radio e' in standby o manca l'IP, aspetto uno stato
canonico utilizzabile. Un invio fallito mantiene errore e associazione, con
ritentativi limitati nel tempo; non ricado in un nuovo codice.

Invio FIND normalmente ogni due secondi. Solo READY autenticato stabilisce
presenza; dopo otto secondi senza una risposta valida riprendo discovery.
Il PC usa una soglia propria di dieci secondi per il suo stato pubblico.
Una presenza scaduta non diventa attuale solo perche' il record esiste ancora.

Un aggiornamento applicativo normale conserva NVS. Cancellazione esplicita,
record danneggiato o altra identita' del PC sono casi distinti. La transazione
puo' aver gia' scritto un candidato sul dispositivo prima della conferma PC:
quel journal, da solo, non autorizza All set o un successo di pairing.

## L'ultimo prompt reale sceglie la chat

Su 15 uso **Connected / Waiting for Codex** finche' manca un contesto autentico.
La didascalia e' di 17 caratteri e 188 pixel nel body da 232: rispetto i limiti
del renderer invece di nascondere un errore di misura.

CCP segue l'ultimo `UserPromptSubmit` autorizzato, con identita' esatta di chat
e turno. Le altre chat possono aggiornare i propri metadati ma tool,
completamenti e interruzioni sullo sfondo non cambiano il contesto seguito.
Un click su una scheda non e' un hook di focus documentato. Dopo il riavvio
del broker attendo il prossimo prompt reale; durante una semplice riconnessione
LAN recupero lo snapshot corrente.

Il nome progetto e' un'etichetta ASCII limitata, non l'identita' della chat.
Cody consuma lo snapshot autenticato per 17-20, 18U e 32. I pacchetti scadono
dopo otto secondi; il lavoro attivo scade dopo 120 secondi senza un nuovo hook.
Stop significa risposta pronta, non task riuscito. SessionEnd mostra 20B per
cinque secondi attivi e poi lascia Cody in Idle.

Installazione, trust degli hook e orchestrazione locale sono condizioni
operative separate dal pairing. Tengo le istruzioni nella guida CCP; non
fabbrico eventi o record di fiducia per simulare una chat attiva.

## Recupero della lease e simulatore Windows

CCP rinnova normalmente la lease MCP ogni dieci secondi. Dopo un guasto di
trasporto riprova con attese di 1, 3, 10 e poi al massimo 30 secondi. Mantengo
l'errore nei tool fino a un rinnovo autenticato riuscito, leggendo la credenziale
esistente e conservando la stessa identita' e lo stesso lease ID. Errori di
identita', versione, autenticazione o risposta invalida restano terminali.
Questo recupero non ripete pairing, tool o hook per conto dell'utente. Se devo
riavviare il broker, attendo un nuovo prompt reale per ricostruire l'attivita'.

Con Cody Studio posso usare un endpoint CCP1 reale su `127.0.0.1:42117`, separato
dal dispositivo. Lo avvio esplicitamente con l'opzione `-Ccp` della console;
la discovery lo include solo con `include_simulator:true`. Identita' e record
DPAPI restano in `artifacts/cody-simulator-state/device.json`, preservati dalla
pulizia ordinaria e distinti dai record fisici.

Il pannello offre Connect, Replace con conferma, Cancel, Forget con conferma
e una disponibilita' LAN sintetica. Ricevo veri pacchetti di attivita' CCP, ma
mostro sempre **SIMULATED**. E' una GUI host con lo stesso protocollo, non
esecuzione ESP-IDF o replica completa dei gesti e dell'onboarding nativo.
Tengo comandi e limiti nella [guida Cody Studio](../../../simulator/windows/README.md).
Una prova di questo endpoint non si trasforma in una prova di Wi-Fi, alimentazione,
touch o NVS sul Cody fisico.

## Owner del provisioning e cleanup

Leggo `DeviceSnapshot.radios.pairing` oppure `readConnectivityObservation`.
Invio START/RETRY/CANCEL attraverso `LUNASHELL_pairing_request` con l'attempt ID
osservato. `ESP_OK` ammette la richiesta; aspetto lo stato canonico per dichiarare
Online o Cancelled. Il display non inizializza le radio e non chiama lo SDK Wi-Fi.

Prima del pairing esplicito parcheggio il DMA del display e passo la lease del
controller al suo owner. Conservo il budget interno di 34.816 byte come quattro
blocchi da 8 KiB e uno da 2 KiB, acquisiti insieme. Rilascio il budget subito prima
dell'inizializzazione controller e lo recupero solo dopo chiusura certa. Gli
errori di cleanup e di prenotazione DMA restano distinti; non faccio passare
una chiusura incerta per riuscita. Il dettaglio vive nella
[guida provisioning](../connectivity-pairing.md).

Copia del QR, payload e moduli appartengono al tentativo corrente. Solo
ReadyToScan autorizza `LUNASHELL_provisioning_copy_qr`; una sessione scaduta o
sostituita produce `ESP_ERR_INVALID_STATE` e richiede una nuova osservazione.
Uso moduli interi, quiet zone bianca di quattro moduli e storage limitato alla
versione 10: massimo 57 x 57 moduli, 407 byte. Non salvo payload segreti o QR.

Gli stati Idle, Starting, ReadyToScan, Connecting, Completing, Online,
Cancelling, Cancelled, Failed e TimedOut mantengono validita', freschezza e
causa reale. Un errore degradato ma valido rimane presentabile. Prima di una
nuova sessione attendo cleanup riuscito; Retry con una sessione ancora attiva
richiede Cancel e resta sulla vista d'errore fino alla chiusura confermata.

## Inattivita', touch e pulsanti

Uso timeout locali di ritorno di 30 secondi per 05/06 e 90 per 07. Il clock del
percorso avanza da sveglio anche se metto in pausa l'animazione; standby e wake
non lo consumano. Lo rinnovo con touch reale o progresso canonico del tentativo,
non con animazione, teletype, IMU o snapshot invariati. Mantengo separati il
deadline backend da cinque minuti e gli override di standby dello screen.

Dopo timeout ritiro generazione e QR della vista, mostro Welcome e porto a
termine il cleanup. Un tap fresco su Welcome riapre Setup; il gesto da 1,5
secondi e' disponibile anche sulle pagine 05/06 stabili. Un risultato tardivo
non puo' ripristinare la vecchia vista o completare un handoff gia' ritirato.

Applico la decisione UI del 9 ottobre: due card affiancate, tre come coppia piu'
azione centrata sotto, una come capsula larga con icona a sinistra. Uso gradienti
discreti e colori semantici. I pulsanti compaiono interi dopo titolo e riga
principale; abilito contemporaneamente il touch. Le loro label non partecipano
a teletype o suono dei caratteri. Per geometria, antialiasing e Back laterale
seguo il [contratto circolare](../circular-display-layout.md).

Dopo startup tratto `ESP_ERR_INVALID_RESPONSE` del touch come guasto opzionale
dell'input: tengo errore e dati precedenti invalidi, scarto i report pendenti,
annullo il gesto e aspetto un rilascio acquisito reale. Non sintetizzo un release
e non resetto implicitamente il controller. Gli altri errori critici mantengono
la loro policy. Questa scelta impedisce al singolo report malformato di fingere
un touch valido o di fermare automaticamente tutto il rendering.

## PWR, BOOT e HTTP

Mantengo la policy della piattaforma. PWR singolo alterna standby/wake, salvo
la cattura screenshot attiva in Debug; il rilascio dopo pressione lunga richiede
standby. In Debug il doppio PWR entro 500 ms o la pressione continua per tre
secondi riavvia. BOOT breve ripristina prima un pannello dim/sleep, oppure
alterna fullscreen/status; BOOT per tre secondi riavvia solo in Debug.
Questi riavvii conservano NVS e log, senza eseguire un factory reset.

Tengo HTTP separato da CCP: ascolta sull'IP station reale, porta 8080, solo
quando attivato esplicitamente. Pairing non lo abilita. Health e' pubblico;
gli endpoint read-only protetti richiedono Bearer e non espongono QR o segreti
CCP. Un valore salvato invalido resta errore. Il contratto completo rimane
nell'[API HTTP](../http-device-api.md).

## Cosa ho osservato e cosa resta da provare

Conservo i dettagli dei checkpoint e delle catture in `done.md` e nei log locali.
La cattura autorizzata di `3458237e` ha completato la derivazione in 27,790323
secondi senza watchdog osservato; l'utente ha poi confermato il codice e,
separatamente, All set/Connected. La correzione dello stack power ha raggiunto
Connected con 5.820 byte liberi minimi su 8.192, ma la seriale e' caduta dopo
circa 16 secondi: quell'intervallo non prova il ciclo standby/risveglio completo.

Per questa revisione registro separatamente build/installazione, prove host,
revisione visiva dei PDF, gesture e salvataggi fisici, Cancel/reset e interruzioni
fra persistenze, perdita/ritorno della LAN, hook reali in Codex e consegna sul
display. I casi sintetici del simulatore non diventano misure del firmware.
Tengo gli esiti effettivi in [done.md](../../../done.md) e autorizzazioni e prove
ancora aperte in [TODO.md](../../../TODO.md): un documento aggiornato non spunta
da solo tutte quelle caselle.

## Provenienza degli atlanti

Ho riunito qui i due appunti del 4 ottobre 2026, prima separati in
`docs/planning/2026-10-04-ccp-service-atlas.md` e
`docs/planning/2026-10-04-ccp-pairing-quickstart.md`. Sono memoria del lavoro di
allora: i contratti correnti sono sopra, le correzioni datate in fondo ai PDF.
La cartella originale era
`C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`;
i mirror nel repository erano identici in quel checkpoint. Non considero questo
un controllo della sincronizzazione attuale.

### Service atlas del 4 ottobre 2026

Su richiesta di Umberto Giacobbi ho preparato dieci pagine A4 landscape in inglese,
con diagrammi vettoriali deterministici, cloud frontale, una scena ImageGen
riutilizzata senza modifiche dal connection atlas e spazio per note. La mappa
originale era: 1 discovery/pairing/presenza; 2 Codex MCP, Python CCP e CCP1 UDP;
3 DISCOVER/OFFER e finestre; 4 codice sul display, un solo match e ambiguita';
5 PAIR/PAIRED/CONFIRM/ACTIVE e retry; 6 una identita' fra 100 PC, illustrazione
senza load test; 7 vecchio percorso 13→16→14→15 e FIND/READY; 8 cadenza di due
secondi, scadenza device di otto secondi e stato PC separato; 9 sicurezza, limite
offline-guessing, DPAPI, CRC NVS e codice; 10 prerequisiti e limiti delle prove.
Il percorso con attesa sullo screen 16 e' stato poi corretto.

Avevo letto `CCP1.md`, README, `mcp.json`, `mcp/ccp_server.py` e
`LUNASHELL_codex_link.cpp`, senza eseguirli e senza dichiarare trasporto di task,
chat o messaggi verificato. Ho applicato a tutti e cinque i PDF l'autore
`Umberto Giacobbi` e il contatto `hello@umbertogiacobbi.biz`, visibili e nei
metadati. I quattro documenti precedenti conservavano contenuti, copyright,
numerazione e conteggi: planning 26, navigation 16, connection 14 e quickstart 2.
Ho aggiunto soltanto una riga di attribuzione di 6,4 pt nella fascia libera
dei loro 58 footer; il nuovo service atlas aveva un footer piu' ampio.

Conservo questi riferimenti sotto `atlas/source/` nella cartella originale:

- `build_ccp_service_atlas.py` e `ccp-service-page-manifest.json`: sorgente e indice.
- `apply_pdf_attribution.py`: passaggio idempotente con backup espliciti.
- `review_ccp_service_documents.py` e `ccp-service-document-review.json`:
  render, dimensioni e confronto dei contenuti precedenti.
- `cody-pdf-attribution-review.json`: hash, mirror, metadati e percorsi dei backup;
  gli hash storici non vengono riscritti come se fossero attuali.

Le evidenze locali erano in `artifacts/document-review/ccp-service-atlas/`.
Ho renderizzato le dieci nuove pagine a 1500 px sul lato lungo, corretto le
spaziature freccia/testo e rivisto i render. Sulle 58 pagine precedenti, il
confronto prima/dopo a 96 DPI mostrava il corpo sopra il footer identico pixel
per pixel; ho ispezionato tutti i footer in quattro contact sheet. Testo e
dimensioni erano conservati, autore e contatto presenti ovunque. Erano prove
documentali: nessun build, test firmware, simulatore, installazione/esecuzione
plugin, modifica firewall/rete, controllo fisico o stampa.

### Quickstart A5 del 4 ottobre 2026

Sempre su richiesta di Umberto, nel fuso Asia/Saigon, ho creato il pieghevole
inglese di due pagine A5 portrait, 419,528 × 595,276 pt. Indicavo stampa al 100%,
fronte/retro sul lato lungo; le preferenze PDF richiedevano NoScaling e
DuplexLongEdge, ma la stampa fisica non era stata verificata. Ho usato navy/cyan,
Segoe UI e JetBrains Mono incorporati, due immagini ImageGen del connection
atlas senza modificarle, una riga per note per lato e un codice chiaramente
illustrativo. Il fronte copriva prerequisiti, ZIP, registrazione marketplace,
installazione/abilitazione e firewall privato; il retro codice, conferma,
Let's go, la vecchia attesa Idle di dieci secondi e recupero del PC associato.

Il pacchetto `Cody-CCP.zip` era una proposta, non un archivio distribuito o
installato. La struttura prevista era `CCP/.agents/plugins/marketplace.json`
piu' `CCP/plugins/cody-codex-plugin/`, con `plugin.json`, `mcp.json`,
`.codex-plugin/plugin.json`, `.mcp.json`, `mcp/`, `skills/`, `protocol/` e README.
Il marketplace proposto usava `name: cody-ccp`, `interface.displayName: Cody CCP`,
entry `cody-codex-plugin`, source `./plugins/cody-codex-plugin`, installation
`AVAILABLE`, authentication `ON_INSTALL` e category `Productivity`; quei wrapper
non erano stati aggiunti al plugin. Distinguevo `codex plugin marketplace add .`
dall'installazione e dall'abilitazione dopo riavvio del client. I prerequisiti
erano Codex CLI, client desktop con supporto ai plugin locali, Windows e Python
raggiungibile come `python`.

Il riferimento storico verificato il 4 ottobre era
[Package your plugin](https://developers.openai.com/plugins/build/plugins),
collegato nel PDF: nessuna azione inventata Install ZIP, nessuna garanzia sul
supporto di ogni client/account. Per l'installazione corrente rimando alla
[guida del plugin](../../../plugins/cody-codex-plugin/README.md).

Nella cartella originale conservo `source/build_ccp_pairing_quickstart_a5.py`,
`source/ccp-pairing-quickstart-a5-review.json` e le immagini
`codex-connection-assets/cody-codex-hero.png` e
`codex-connection-assets/display-code-to-codex.png`. I render locali erano in
`artifacts/document-review/ccp-quickstart-a5/`: entrambe le pagine renderizzate
con Poppler a 1600 px sul lato lungo, poi ispezionate per clipping, allineamento,
leggibilita' e ordine del flow. Non avevo ricostruito gli altri atlanti ne'
eseguito firmware, simulatore, installazione plugin, rete o prove sul dispositivo.
