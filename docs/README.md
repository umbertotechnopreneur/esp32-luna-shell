# Mappa della documentazione

Uso questa pagina come indice. Nelle guide tengo il comportamento corrente;
nei README delle cartelle spiego chi possiede il codice. Registro il lavoro
aperto in [TODO.md](../TODO.md), le evidenze in [done.md](../done.md) e le lezioni
in [lessons.md](../lessons.md). Evito una guida nuova per ogni correzione: di
addenda ne abbiamo gia' una discreta collezione.

## Cody e CCP

| Voglio capire... | Leggo... |
| --- | --- |
| Come e' composto Cody | [Architettura Cody](architecture/cody/README.md) |
| Primo avvio, Options, rete, pairing e recupero | [Onboarding e connessione](architecture/cody/onboarding.md) |
| Scene, orologi del runtime e Debug | [Runtime](architecture/cody/runtime.md) |
| Rig, rendering e prestazioni | [Rendering](architecture/cody/rendering.md) |
| Orologio e RTC | [Clock](architecture/cody/clock.md) |
| Installazione, hook, tool e broker CCP | [Guida CCP](../plugins/cody-codex-plugin/README.md) |
| Formato e autenticazione dei messaggi LAN | [Protocollo CCP1](../plugins/cody-codex-plugin/protocol/CCP1.md) |
| Preview Windows e pairing CCP simulato | [Cody Studio](../simulator/windows/README.md) |

Mantengo cinque PDF con responsabilita' diverse. Aggiorno le pagine principali
quando cambia il flow; conservo in fondo gli addenda precedenti e aggiungo sempre
la pagina datata **Errata and corrections**. Per orientarmi uso gli screen ID,
non il numero stampato della pagina.

| PDF | A cosa mi serve |
| --- | --- |
| [Navigation Atlas](Cody/cody-navigation-atlas-v1.pdf) | Percorsi, ritorni e decisioni di rete/pairing |
| [Planning Atlas](Cody/cody-planning-atlas-v1.pdf) | Inventario degli screen, layout e confini del prodotto |
| [Pairing Quickstart](Cody/cody-ccp-pairing-quickstart-a5-v1.pdf) | Installazione e collegamento dal PC, in formato A5 |
| [CCP Service Atlas](Cody/cody-ccp-service-atlas-v1.pdf) | Owner, protocollo, persistenza e limiti delle prove |
| [Codex Connection Atlas](Cody/cody-codex-connection-atlas-v1.pdf) | Esperienza dal codice iniziale al recupero del PC e della chat |

Tengo i vecchi [appunti sul pairing](planning/2026-10-03-pairing-atlas-addendum.md)
e [sull'aspetto](planning/2026-10-04-cody-look-atlas-addendum.md) come storico.
Per il comportamento attuale parto dalla guida e dalle pagine principali dei PDF.

## Piattaforma e Luna

| Area | Guida tecnica |
| --- | --- |
| Geometria | [Display circolare](architecture/circular-display-layout.md), [indicazioni UI](ui/round-display-guidelines.md) |
| Display e touch | [Owner del runtime](architecture/display-touch-runtime.md) |
| Avvio e log | [Startup](architecture/startup-and-logger.md), [log SD/USB](architecture/offline-logging-usb.md) |
| Dati hardware | [Snapshot](architecture/device-snapshot.md), [API hardware](architecture/hardware-api.md) |
| Alimentazione | [Politica low power](architecture/low-power-management.md), [deep sleep Release](architecture/release-deep-sleep.md) |
| Rete | [Provisioning](architecture/connectivity-pairing.md), [API HTTP](architecture/http-device-api.md) |
| Audio | [Wake-on-word](architecture/wake-on-word.md), [TTS opzionale](architecture/text-to-speech.md) |
| Configurazione | [Board](architecture/board-build-configuration.md), [Debug/Release](architecture/firmware-build-modes.md) |
| Rilascio | [Checklist open source](release/open-source-pre-release-checklist.md) |

## Hardware e console

- [Inventario hardware verificato](hardware/hardware-inventory.md)
- [Cablaggio ToF con doppia UART](hardware/tof400f-dual-uart.md)
- [Vincoli ESP-IDF 6](esp-idf-6-migration.md)
- [Console pubblica di sviluppo](../scripts/README.md)
- [Regole del repository](../AGENTS.md)
