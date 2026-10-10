# Simulatori Windows

Qui provo la UI circolare da 466 × 466 e i comportamenti descritti dagli asset.
Tengo ben visibili **SYNTHETIC** e **SIMULATED**: i valori generati sul PC non
entrano negli snapshot del firmware. FPS host, immagini Pillow e audio del PC
restano osservazioni host; il comportamento elettrico di Cody si verifica su Cody.

## Apro Cody Studio

Uso sempre la console pubblica dalla radice del repository:

```powershell
.\scripts\device.ps1 simulator
.\scripts\device.ps1 simulator -Screen greeting
.\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
.\scripts\device.ps1 simulator -Screen boot-vibeware -AtMs 1500 -OutputPath artifacts/simulator/boot.png
.\scripts\device.ps1 boot-screens
```

Mi servono Python, Pillow e PyYAML. La console prepara il driver audio host prima
dell'apertura interattiva; una cattura statica non avvia l'audio.
L'anteprima parte senza rig sovrapposto. Nei pannelli **Face**, **Shape**, **Inputs**
e **Inspect** cambio espressioni, contorno, stimoli sintetici e diagnostica.
Ogni scheda mantiene il proprio scorrimento; nelle finestre strette l'ispettore
si dispone sotto la preview, quando lo spazio verticale lo permette.

Ridimensiono solo la presentazione: geometria, rotazione e puntatore restano
nelle coordinate logiche del display. Uso Per Monitor v2 prima di Tk e la scala
DPI iniziale del monitor. Il cambio tra monitor con DPI diversi richiede ancora
una transizione Tk dedicata. Lascio il limite a 30 Hz, oppure 25 Hz nello studio
jelly; nell'ispettore posso scegliere 15, 25, 30 o 60 Hz. Il footer distingue
gli FPS osservati dal limite richiesto. Aggiorno le letture testuali a 10 Hz e
lo stato a 5 Hz; i clock del modello seguono il tempo trascorso.

Il floppy VibeWare usa 42 frame e la timeline di 3,5 secondi del GIF approvato,
con composizione nera, scala pixel doppia e rotazione condivisa.
`-AtMs` sceglie il fotogramma di una cattura; `boot-screens` comprende
l'introduzione. Il link Manifesto si apre solo con un clic esplicito.

## Collego il simulatore a CCP

```powershell
.\scripts\device.ps1 simulator -Ccp
```

Con `-Ccp` apro anche il pannello **CCP** e un endpoint UDP reale, solo su
`127.0.0.1:42117`. Nel pannello scelgo **Connect Codex** e leggo il codice.
In Codex chiedo espressamente di collegare **Cody Studio SIMULATED**; la discovery
aggiunge `include_simulator: true`, poi il pairing usa il codice visibile.
Per installazione e trust seguo il [README canonico di CCP](../../plugins/cody-codex-plugin/README.md).

Parlo CCP1 con le stesse primitive del plugin: offerte, PAIR/PAIRED,
CONFIRM/ACTIVE e FIND/READY/ACTIVITY autentici. Dopo PAIR salvo un candidato
distinto dall'associazione attiva. Lo promuovo con CONFIRM o con un READY
autenticato che risponde al nonce corrente; quest'ultimo percorso permette di
riprendere dopo un riavvio senza un nuovo codice.
Rispondo a ogni READY valido con COMMITTED, soltanto dopo il commit attivo:
anche il PC può così completare il proprio candidato se ACTIVE o un precedente
COMMITTED si sono persi. Il protocollo conserva il vecchio record su entrambi
i lati fino alla prova del nuovo commit; non prometto una transazione atomica
tra PC e dispositivo. Dopo Cancel il vecchio link può riprendere, ma il candidato
PC resta esplicito: per sostituirlo uso un nuovo codice e `replace_pending: true`.

La mia identità simulata inizia con `02` e vive, insieme alle associazioni
protette da DPAPI, in `artifacts/cody-simulator-state/device.json`.
Il cleanup dei build conserva questa directory. Un secondo endpoint non può
prendere la stessa porta; un errore di lettura resta un errore, senza inventare
un nuovo dispositivo. Il codice compare solo nella UI, mai nei log o nello
snapshot pubblico.

Dal pannello posso:

- confermare **Replace pairing** mantenendo il vecchio record fino alla nuova conferma;
- usare **Cancel code** per scartare la finestra o il candidato corrente;
- confermare **Forget pairing** per cancellare solo le associazioni del simulatore;
- togliere **SIMULATED LAN available** e ripristinarla senza perdere l'identità;
- disattivare **Show CCP on the display** per tornare al rig mentre il link resta attivo.

Posso chiedere esplicitamente a CCP il riavvio o il factory reset di questo
target **SIMULATED**, con le distinte frasi e il codice descritti nel
[README del plugin](../../plugins/cody-codex-plugin/README.md#riavvio-o-ricomincio-dal-setup).
Il riavvio conserva lo store; il factory reset cancella soltanto impostazioni
e associazioni del simulatore e rende la LAN simulata non configurata anche
dopo la riapertura. Per ripartire riattivo **SIMULATED LAN available** e apro
un nuovo pairing. Il vecchio record PC resta separato; non cancello nulla sul
dispositivo fisico, nel firmware o sulla scheda SD.

La preview mostra le scene/font esistenti e lo stato ricevuto dal broker.
Il nome progetto occupa al massimo due righe da 21 caratteri; attendo pacchetti
freschi prima di mostrare attività. Dopo otto secondi senza aggiornamenti tratto
l'attività come **UNAVAILABLE**. Il cambio chat segue il vero prompt inviato a
Codex; non ho aggiunto un selettore locale o un produttore di hook finti.

Con CCP 0.6 ricevo anche EVENT autentici. Una domanda segue 26 → 27 → 31:
apro, scelgo una card, controllo la descrizione e premo **Send**. La scelta
resta locale e reversibile fino a Send; Back conserva il percorso. Il solo
ACK UDP lascia **Waiting for Codex**: **Reply sent** richiede il secondo poll
di conferma del destinatario, entro il TTL di 180 secondi. Posso togliere la
LAN dal pannello e vedere la stessa domanda ancora presente, con invio
disabilitato fino a un nuovo snapshot autentico.

Per le PR mostro solo dati. **View failures** apre fino a sei nomi interi,
tre per pagina; scorro verticalmente e torno indietro con Back. Le righe
non sono pulsanti, e non compaiono merge, retry, log o stacktrace. Le nuove
notifiche sostituiscono le vecchie senza interrompere una domanda. Gli
strumenti di invio richiedono `include_simulator: true` anche dopo il pairing;
continuo a mostrare **SIMULATED**, senza spacciare la preview per firmware.

Questa modalità verifica il percorso del protocollo e la sua presentazione host.
Il commissioning si controlla dal pannello desktop: non eseguo qui il firmware
ESP-IDF né riproduco ogni gesto e schermata dell'onboarding nativo.
Per evitare catture ambigue uso `-Ccp` solo nella modalità interattiva `rig`,
senza studi di materiale. Cody fisico resta un'identità indipendente.

## Espressioni, simboli e materiale

Nel pannello Face scelgo un simbolo manga per la presentazione `rig`.
I preset semantici `sleep` e `sleeping` attivano Sleep; chiudere gli occhi con
uno slider non equivale ad addormentarsi. Una modifica manuale del volto toglie
l'identità del preset. Greeting, boot e Micro non mostrano il simbolo.
Per provare le carezze automatiche disattivo il rig diagnostico e scelgo None.

Leggo ID, priorità, tempi, gesti e umore da `assets/cody/personality.yaml`.
Il canale attende 300 ms di richiesta stabile, entra in 400 ms, rimane almeno
2.500 ms e scompare in 600 ms. Sleep semantico salta l'attesa iniziale e può
rilasciare subito il simbolo concorrente; un simbolo Sleep scelto manualmente
mantiene la normale attesa. Pause congela il tempo attivo.
Non prometto che il pannello fisico resti acceso fino alla fine della dissolvenza.

L'ancora segue Nose con una finestra di 360 ms; la scala usa 400 ms.
Uso 17 intervalli fissi da 25 ms, ritardo radiale massimo di quattro pixel e
deformazione massima di due pixel. La storia geometrica non accoda simboli.
Palette, copertura e opacità arrivano dagli asset RGB565 generati.
Riservo Back e la regione di stato: se il simbolo non entra, riporto
`BLOCKED_BY_LAYOUT`, senza ridurlo o spostarlo a sorpresa.
Il contratto completo sta in [personality icons](../../docs/architecture/cody/personality-icons.md).

Per l'aspetto della nuvola tengo distinti tre studi:

| Opzione | Cosa provo |
| --- | --- |
| `-CloudJellyStudy` | Approccio perla a scanline, interattivo o PNG/GIF; il volto segue il corpo. |
| `-CloudMaterialStudy` | Studio host del volume, con cattura statica. |
| `-CloudPngMeshStudy` | Texture e mesh triangolare; `-MeshOverlay` mostra la topologia. |

Scelgo un solo materiale per volta. Preferisco PNG per giudicare i colori;
il GIF segue una timeline sintetica fissa, non un replay di sensori.

```powershell
.\scripts\device.ps1 simulator -CloudJellyStudy
.\scripts\device.ps1 simulator -Screen greeting -CloudJellyStudy -AtMs 1500 -OutputPath artifacts/simulator/greeting.png
.\scripts\device.ps1 simulator -CloudPngMeshStudy -MeshOverlay -OutputPath artifacts/simulator/cloud-mesh.gif
```

## Preview Luna e Leg

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'battery-low', '--screen', 'battery')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'settings', '--capture', 'artifacts/simulator/settings.png')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--board-config')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'leg-motion')
.\scripts\device.ps1 package-simulator
```

Con Luna provo pagine diagnostiche, archi di stato/comando, alimentazione e azioni
semantiche. `--board-config` sceglie la shell minima dal board YAML e dal documento
shell preparato; non lo mescolo con scenari, pagine e radio del dispositivo
completo. Per Leg tengo accelerometro, gyro, vista combinata, movimento e
impostazioni, sempre sintetici.

Il pacchetto EXE rimane Luna in `artifacts/simulator/windows/dist/`.
Il packaging prepara esplicitamente l'ambiente host; aprire una preview non
installa dipendenze di nascosto.

| Controllo Luna | Azione sintetica |
| --- | --- |
| Mouse | Touch |
| B | BOOT breve / fullscreen |
| P | Standby PWR |
| R | Reset software BOOT |
| Enter / C | Conferma |
| Esc | Back |
| F2 | Guida viewport |

In standby ignoro touch e BOOT breve; riattivo con PWR sintetico.
Il viewport parte da 320 × 320 a y=73; nascondendo l'arco basso arriva a
320 × 393, e senza entrambi gli archi/fullscreen a 320 × 466, y=0.
Con `--idle-state` posso catturare active, command-hidden, chrome-hidden,
fullscreen, dimmed, off e standby.

## Fixture e verifiche

Il driver `cody_host_graphics.dll` comprende anche i moduli di logica condivisi
con il firmware. Python conserva finestra, trasporto, autenticazione e input
sintetici; non sostituisce una libreria mancante o non aggiornata con una seconda
implementazione. Il caricamento controlla manifest, hash dei sorgenti e ABI.
Dopo modifiche a questi moduli preparo il driver, quando l'esecuzione e'
autorizzata, attraverso la console pubblica:

```powershell
.\scripts\device.ps1 generate -Generator cody-simulator-graphics
```

| Percorso | Codice di produzione eseguito sul PC |
| --- | --- |
| CCP | Decoder EVENT e transazioni: ID, scadenza originale, scelta, risposta, chiusura e rifiuto della riapertura. |
| Domande | Piano di presentazione, percorsi, testi, palette, icone, preparazione e raster delle azioni, tempi di comparsa e ammissione del touch. |
| Standby | Politica dei servizi, ordine delle risorse, errori, rollback e recupero; le operazioni delle risorse rimangono sintetiche. |
| Snapshot e HTTP | Decoder RTC, aging con timestamp originali, validita', redazione e serializzatore JSON del firmware. |

Le azioni `standby:profile` e `standby:fault` configurano rispettivamente i
quattro requisiti booleani `deep_sleep_enabled`, `retained_connection`,
`voice_listening`, `touch_wake_available` e un guasto con `resource`, `target`,
`error`, piu' `count`/`skip` opzionali. `press:pwr` esegue la transizione C++ e
pubblica anche il risultato di un recupero incompleto. `rtc-registers` riceve
`registers`, una lista di sette byte, e passa i valori al decoder RTC reale.
Un errore di decodifica resta visibile, senza sostituire il campione storico.

I gruppi non modellati restano indisponibili. Il Task-WDT ESP-IDF non esiste
sul PC: `/api/v1/health` riporta liveness non disponibile con
`ESP_ERR_NOT_SUPPORTED` e `ok=false`. I dati restano **SYNTHETIC**; il JSON
aggiunge `simulator.runtime=production-cpp`. La scelta deep sleep e' una
decisione della politica, non una prova di sonno elettrico, consumi o risveglio.

In `scenarios/` uso `LUNASHELL.simulator.scenario.v1`: patch tipizzate e
eventi ordinati nel tempo virtuale. Rifiuto chiavi duplicate, tipi errati e stati
incoerenti. Format e calibrazione mantengono le proprie precondizioni.

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments @('--list-scenarios')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'healthy', '--at-ms', '2500', '--dump-snapshot')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'healthy', '--action', 'start:wireless', '--advance-ms', '1000', '--dump-api', '/api/v1/status')
```

Con `--scenario-file` scelgo una fixture esplicita; `--output` scrive sotto
`artifacts/`. Avanzo il replay solo quando richiesto e mantengo generazioni
immutabili. L'elenco degli scenari viene dal catalogo, così non lo duplico qui.

Il server interno `fake_device_server.py` usa loopback, normalmente porta 18080,
e header sintetici/no-cache. Health è pubblico; gli altri GET richiedono Bearer
esatto, con limite dopo venti errori in sessanta secondi. Il token pubblico di
fixture è `LUNASHELL-SIMULATOR-ONLY-NOT-A-SECRET`.
Il controllo separato `/__simulator/` gestisce load/advance/action; per i contratti
uso [HTTP device API](../../docs/architecture/http-device-api.md).

Preparo queste verifiche tramite la console, poi chiedo l'autorizzazione prevista
da [AGENTS.md](../../AGENTS.md) prima di ogni esecuzione:

```powershell
.\scripts\device.ps1 check -Check cody-ccp
.\scripts\device.ps1 check -Check cody-ccp-simulator
.\scripts\device.ps1 check -Check cody-runtime
.\scripts\device.ps1 check -Check cody-preview-raster
.\scripts\device.ps1 check -Check simulator
```

Le due suite CCP non aprono finestre e non inviano hook a Codex. Quella del
simulatore comprende UDP reale su porte temporanee di loopback e stato sintetico
isolato, senza broadcast LAN o accesso alle associazioni personali.
La suite raster confronta i pixel con il compositore per-pixel e un oracolo di
copertura indipendente su maschere piccole. La suite Luna copre fixture, azioni,
redazione e trasporto loopback. Registro risultati e limiti in
[done.md](../../done.md) e [TODO.md](../../TODO.md).
Per interpreti, percorsi e packaging rimando alla [console](../../scripts/README.md);
per il dispositivo ai contratti [runtime](../../docs/architecture/cody/runtime.md)
e [rendering](../../docs/architecture/cody/rendering.md).

`cody-runtime` include le definizioni di regressione dei moduli C++ condivisi;
`simulator` comprende aging, RTC, errori di pubblicazione e JSON nativo. La
presenza di queste definizioni non attesta la loro esecuzione. Le suite che
caricano la libreria richiedono il driver aggiornato prima del proprio avvio.
