# ESP32-S3-Touch-AMOLED-1.75C - raccolta tecnica completa

Raccolta preparata il 15 ottobre 2026 da fonti ufficiali Waveshare, Espressif e dal repository ufficiale Waveshare. Immagini, documentazione e software restano soggetti alle rispettive licenze; questa e' una copia locale per consultazione e sviluppo.

## Che prodotto e'

Scheda Waveshare in formato badge/orologio con case CNC in lega di alluminio. Integra un ESP32-S3, display AMOLED touch rotondo, audio con due microfoni, IMU a 6 assi, PMIC e spazio interno per batteria. E' adatta a prototipi UI, wearable, interazione vocale e applicazioni embedded con ESP-IDF.

## Prezzo e batteria

- Prezzo esposto sulla pagina prodotto alla raccolta: **US$39.99 - US$41.99**. Non e' una conferma di prezzo finale, tasse, spedizione o disponibilita'.
- SKU **33691 / ESP32-S3-Touch-AMOLED-1.75C**: batteria Li-ion **3.7 V** inclusa.
- SKU **33692 / ESP32-S3-Touch-AMOLED-1.75C-EN**: batteria non inclusa.
- Connettore batteria: MX1.25 a 2 poli, con carica e scarica. La capacita' della batteria inclusa non e' dichiarata nella pagina consultata: verificare l'etichetta della batteria fisica prima di stimare l'autonomia.

## Specifiche principali

| Voce | Specifica |
| --- | --- |
| MCU | ESP32-S3R8, Xtensa LX7 dual-core fino a 240 MHz |
| Memoria | 512 KB SRAM, 384 KB ROM, 8 MB PSRAM e 32 MB NOR Flash esterna |
| Wireless | Wi-Fi 2.4 GHz 802.11 b/g/n e Bluetooth 5 LE con antenna onboard |
| Display | AMOLED touch 1.75 in, 466 x 466 px, 16.7 M colori, 700 cd/m2, contrasto 100000:1 |
| Display/touch | QSPI con driver CO5300; touch CST9217 su I2C |
| Audio | Doppio microfono, ES7210, codec ES8311 e speaker onboard/pad speaker |
| Movimento | QMI8658 IMU 6 assi: accelerometro 3 assi e giroscopio 3 assi |
| Alimentazione | AXP2101: PMIC, carica e gestione batteria, piu' uscite di alimentazione |
| I/O | USB-C per flashing/log, tasti PWR e BOOT, connettore batteria MX1.25 |

## Chip e aspetti BSP

| Componente | Ruolo | Base documentale |
| --- | --- | --- |
| ESP32-S3R8 | MCU, Wi-Fi/BLE, PSRAM | pagina, wiki e schema |
| NOR Flash 32 MB | Flash esterna | pagina/wiki; il part number non e' dichiarato nel riepilogo ufficiale |
| CO5300 | driver AMOLED QSPI | pagina/wiki e datasheet locale |
| CST9217 | controller touch I2C | pagina/wiki |
| QMI8658 | IMU 6 assi | pagina/wiki, schema e datasheet locale |
| AXP2101 | PMIC, gestione batteria/ricarica e rail di alimentazione | pagina/wiki, schema e datasheet locale |
| ES7210 | ADC audio per microfoni | wiki e schema |
| ES8311 | codec audio | wiki, schema e datasheet locale |
| NS4150 | amplificatore speaker | identificato nello schema |

Per pinout, reti, alimentazioni, USB-C, connettori, passivi e test point, usare lo schema completo in `hardware-docs/schematic.pdf`. Questa tabella non sostituisce lo schema.

## BSP ESP-IDF e firmware (senza Arduino)

Il BSP e gli esempi ufficiali sono estratti in `software/esp-idf-bsp/`. Contengono cinque progetti:

- `01_AXP2101` - gestione alimentazione e telemetria batteria.
- `02_lvgl_demo_v9` - UI/display con LVGL 9.
- `03_esp-brookesia` - applicazione UI ESP-Brookesia.
- `04_Immersive_block` - grafica LVGL guidata dall'IMU.
- `05_Spec_Analyzer` - analizzatore di spettro dai microfoni.

I manifest del BSP usano il componente gestito `waveshare/esp32_s3_touch_amoled_1_75c` versione `^3.0.0`; gli esempi dichiarano dipendenze ESP-IDF >= 5.5 e LVGL 9.5.0. Il repository ufficiale dichiara supporto per ESP-IDF 5.5.5 e 6.0.2. Non e' stato usato ne' estratto codice Arduino.

In `firmware/` sono disponibili dieci pacchetti ufficiali `*-combined.zip`, pronti da flashare: cinque demo per ESP-IDF 5.5.5 e le stesse cinque per ESP-IDF 6.0.2. Contengono script `flash_combined.bat`/`flash_combined.sh` secondo la documentazione del repository. E' incluso anche `FactoryOnly-260114.bin`, firmware factory di ripristino; flashare soltanto dopo aver verificato porta, offset e procedura di release del produttore.

## Contenuto locale

- `gallery.html` - galleria consultabile con tutte le immagini locali.
- `images-product/` - 23 immagini ufficiali: 7 foto del prodotto, funzioni, case, display, audio, AI, overview, "What's on board", package content e dimensioni.
- `images-wiki/` - 2 tavole ufficiali wiki: overview/interfacce e dimensioni.
- `hardware-docs/` - schema PDF a 3 pagine, datasheet ESP32-S3/TRM, QMI8658, AXP2101, ES8311, CO5300 ed ES7210.
- `software/esp-idf-bsp/` - 5 progetti sorgente ESP-IDF ufficiali estratti.
- `software/waveshare-source-main.zip` - snapshot completo del repository ufficiale, Apache-2.0.
- `firmware/` - 10 pacchetti flashabili ESP-IDF e un binario factory ufficiale.

La pagina prodotto referenzia anche `ESP32-S3-Touch-AMOLED-1.75C-details-16.jpg`, ma l'URL ufficiale restituisce 404 durante il download; non e' stato sostituito con materiale di terze parti.

## Link ufficiali

- Pagina prodotto: https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm
- Wiki: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C
- Risorse, schema e datasheet: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C/Resources-And-Documents
- Guida ESP-IDF: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C/ESP-IDF
- Repository, BSP e sorgenti: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C
- Release/firmware ufficiali: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/releases

## Progetti community indicati dalla wiki (non scaricati)

Questi riferimenti non sono forniti o mantenuti da Waveshare; sono lasciati come link, non inseriti nel BSP ufficiale.

- Muse Gadget SDK: https://github.com/facebookincubator/muse-gadget-sdk
- Claude Desktop Buddy port: https://github.com/vthinkxie/claude-desktop-buddy-esp32
- MOTO GPS Waveshare Edition: https://github.com/mx3353672833-debug/moto-gps-waveshare
- WaveStopwatch: https://github.com/VolosR/WaveStopwatch
- Codex Island: https://github.com/WildBeast114514/codex-island-waveshare
- Falling sand demo: https://github.com/nurdism/esp_sand_sim

