# Console di sviluppo

Uso `scripts/device.ps1` come unico ingresso pubblico per firmware, simulatori,
generatori e verifiche, da PowerShell nella checkout. Menu, `help` e `list`
restano inerti: carico ESP-IDF o apro simulatori, check e seriale solo quando
scelgo un'operazione. Lascio help ed etichette della console in inglese.

## Comandi

```powershell
.\scripts\device.ps1 help
.\scripts\device.ps1 list
.\scripts\device.ps1 build -Jobs 8
.\scripts\device.ps1 build -BuildMode Release -Jobs 8
.\scripts\device.ps1 flash -Port COM10 -BuildMode Release -Jobs 8
.\scripts\device.ps1 simulator
.\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
.\scripts\device.ps1 simulator -Ccp
.\scripts\device.ps1 check -Check cody-native
.\scripts\device.ps1 generate -Generator cody-rig
.\scripts\device.ps1 size -SizeOutputFormat json2
.\scripts\device.ps1 clean-artifacts
```

Questi sono esempi d'uso, non autorizzazioni automatiche. Seguo
[AGENTS.md](../AGENTS.md) e le autorizzazioni esplicite della sessione per test,
build e operazioni sul dispositivo; prima di usare la seriale confermo la porta.

A ogni invocazione parto da `Debug` e `cody-companion`. Richiedo
`-BuildMode Release` su ciascun comando corrispondente; nel menu posso scegliere
il modo per quella sessione. `-BoardConfig` possiede il suo profilo e non si
combina con `-Profile`/`-Feature`. Conservo i controlli di configurazione e la
revisione del dependency lock: dettagli nei
[modi di build](../docs/architecture/firmware-build-modes.md).

Con `list` leggo gli identificatori correnti di check e generatori. Passo gli
argomenti aggiuntivi come valori letterali, mai come codice shell da valutare:

```powershell
.\scripts\device.ps1 check -Check cody-alpha -ToolArguments @('--previews')
.\scripts\device.ps1 check -Check changed-secrets -ToolArguments @('--base-ref=origin/main')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'battery', '--capture', 'battery.png')
```

La scansione segreti con `--base-ref` comprende anche i checkpoint locali e non
esegue fetch; senza quel parametro comprende soltanto file dirty/untracked.
Nel menu inserisco gli argomenti aggiuntivi come array JSON di stringhe.
Propago gli errori al chiamante della CLI; nel menu conservo l'esito fallito
prima di offrire un'altra scelta.

## Simulatore Cody e CCP

Con `simulator -Ccp` apro Cody Studio con il pannello CCP e un endpoint reale
CCP1 su `127.0.0.1:42117`. Tengo la discovery del simulatore esplicita attraverso
`include_simulator:true`; il default rimane il dispositivo fisico. Marco preview
e risultati `SIMULATED`. La GUI host permette rete sintetica, pairing e attivita'
CCP, senza eseguire ESP-IDF o riprodurre tutte le gesture dell'onboarding nativo.

Conservo lo stato protetto separato in `artifacts/cody-simulator-state/`, anche
durante la pulizia dei transitori. Per uso e limiti rimando alla
[guida del simulatore](../simulator/windows/README.md); per installare o abilitare
CCP uso la [guida del plugin](../plugins/cody-codex-plugin/README.md).

## Sintesi vocale opzionale

Lascio la sintesi vocale disabilitata nel firmware ordinario. Con
`-Feature tts-sd` seleziono inglese, italiano, mandarino e cantonese su microSD;
con `-Feature tts-flash` incorporo solo l'inglese. Sono feature dei profili
`cody-companion` e `full`, con configurazione, build e dipendenze isolate;
nel menu espongo la stessa scelta.

`tts-sd` abilita `CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN`: al termine delle fasi
obbligatorie accodo una volta la frase fissa "Ciao questa è una sintesi vocale
di test 5 ottobre 2026". `tts-flash` parte in silenzio. Posso disabilitare il
simbolo di test separatamente. La sintesi usa core 0; Cody/LVGL renderizzano su
core 1. Nessuna scena richiede automaticamente parlato. Il test al boot richiede
pacchetto SD verificato, alimentazione attiva e volume salvato diverso da zero;
salta la policy orologio/quiet hours.

Il generatore esplicito `tts-assets` richiede
`-ToolArguments @('--backend', 'sd')` oppure `@('--backend', 'flash')`.
Scrivo soltanto sotto `artifacts/`: non preparo una scheda fisica e non eseguo
flash. Le build ordinarie non richiamano il generatore. Tengo API, percorsi SD,
limiti di memoria e verifiche ancora aperte nella
[guida TTS](../docs/architecture/text-to-speech.md); applico le autorizzazioni
richieste per generazione, configurazione, build e uso dell'hardware.

## Seriale e log salvati

```powershell
.\scripts\device.ps1 capture -Port COM10 -DurationSeconds 45 -ResetBeforeCapture -OutputPath artifacts/serial/boot.log
.\scripts\device.ps1 sd-log -Port COM10 -StoredLog current
.\scripts\device.ps1 sd-log -Port COM10 -StoredLog previous
```

Con `-ResetBeforeCapture` richiedo esplicitamente un reset prima della cattura.
Il download dei log salvati non riavvia e non formatta: richiede firmware
aggiornato, dispositivo sveglio e SD montata. Chiudo prima gli altri monitor.
Il percorso predefinito e' `artifacts/serial/sd-log-<name>.log`; controllo i
blocchi prima di sostituire il file locale. `-WaitSeconds` limita ogni risposta
USB. L'ownership rimane nella [guida logging](../docs/architecture/offline-logging-usb.md).

## Strumenti host

Con `-PythonExecutable` scelgo Python per gli strumenti host; con `-PythonExe`
scelgo l'interprete ESP-IDF. Per l'host cerco prima
`artifacts/python/simulator/Scripts/python.exe`, poi PATH. Gli strumenti Cody
richiedono Pillow e PyYAML. Avviare un simulatore non installa automaticamente
l'ambiente. `package-simulator` prepara e impacchetta esplicitamente il
simulatore Luna ereditato; Cody viene eseguito dai sorgenti.

## Responsabilita' e output

| Percorso | Responsabilita' |
| --- | --- |
| `device.ps1` | Dispatcher pubblico |
| `commands/` | Firmware, menu, packaging e manutenzione banner |
| `common/` | Routing condiviso, console/help, policy artifact e contratti sorgente |
| `generators/` | Trasformazioni delle risorse autoriali |
| `checks/`, `tests/` | Runner espliciti e fixture native |

CMake puo' richiamare direttamente gli input interni di generatori/check;
i flussi manuali pubblici passano comunque dal dispatcher. La copia MenuManager
che mantengo non dipende a runtime da `E:\tools`. Non aggiungo alias pubblici
paralleli per menu o cattura.

Tengo gli output eliminabili dentro `artifacts/` di questa checkout. Anche
`-BuildDir`, `-LogPath`, `-OutputPath` e `-SizeOutputFile` espliciti devono stare
li'; risolvo i percorsi pubblici relativi rispetto alla checkout. I nomi semplici
di cattura Python finiscono in `artifacts/simulator/`.

Prima di build/flash/combo/recover pulisco log transitori, catture seriali,
report, preview, output dei check e temporanei. Conservo build firmware,
dependency lock, configurazioni e overlay delle schede, cache Python e
`artifacts/cody-simulator-state/`. Con `fullclean` pulisco esplicitamente soltanto
la build firmware selezionata. I writer tengono un lock esclusivo sugli artifact:
chiudo simulatori e monitor attivi prima di un altro writer. La pulizia controlla
i target e rifiuta i reparse point.

`assets/**/generated` versionati e `managed_components/` scaricati sono input,
non scarti di build. I default sono `artifacts/logs/<action>.log`,
`artifacts/serial/capture.log` e `artifacts/reports/size.<format>` rispettivamente
per log, catture e report dimensioni.
