# ESP32-S3-Touch-AMOLED-1.43C - scheda di raccolta tecnica

Raccolta preparata il 15 ottobre 2026 da fonti ufficiali Waveshare ed Espressif. Il materiale e le immagini restano soggetti alle rispettive licenze/copyright; questa cartella e' una copia di consultazione locale.

## Che prodotto e'

Scheda di sviluppo Waveshare per prototipi e validazione di applicazioni AI e agenti conversazionali vocali. E' una scheda ESP32-S3 molto compatta con display AMOLED rotondo touch, audio, doppio microfono, alimentazione/batteria e Wi-Fi/BLE integrati. Il produttore e' Waveshare, azienda cinese fondata nel 2006 che realizza hardware e pubblica risorse tecniche e parte del software in open source.

## Prezzo e batteria

- Prezzo mostrato dalla pagina prodotto al momento della raccolta: **US$30.99 - US$31.99**. E' il prezzo esposto dal negozio, non una conferma di prezzo finale, tasse, spedizione o disponibilita'.
- **ESP32-S3-Touch-AMOLED-1.43C**: comprende una batteria al litio da **3.7 V**, installabile nel case.
- **ESP32-S3-Touch-AMOLED-1.43C-EN**: versione standard, **senza batteria**.
- La documentazione conferma il connettore MX1.25 a 2 poli per batteria Li-ion 3.7 V, con carica e scarica. Non dichiara una capacita' della batteria inclusa nella pagina consultata: verificare l'etichetta dell'unita' ordinata prima di dimensionare autonomia e carica.

## Dati principali

| Voce | Specifica |
| --- | --- |
| MCU/SoC | ESP32-S3-PICO-1-N8R8, Xtensa LX7 dual-core a fino a 240 MHz |
| Memoria | 512 KB SRAM, 384 KB ROM, 8 MB PSRAM e 8 MB Flash |
| Wireless | Wi-Fi 2.4 GHz 802.11 b/g/n e Bluetooth 5 LE, antenna onboard |
| Display | AMOLED rotondo 1.43 in, 466 x 466 px, 16.7 M colori, 600 cd/m2, contrasto 10000:1 |
| Display bus | QSPI; driver CO5300 |
| Touch | Capacitivo; controller CST820 su I2C |
| Audio | Doppio microfono, codec ES8311, elaborazione/riduzione eco ES7210, amplificatore per speaker |
| Alimentazione/I/O | USB-C, connettore batteria MX1.25 2-pin, pad I2C, UART e USB; speaker MX1.25 |
| Comandi | BOOT, PWR e LED programmabile su GPIO5 |

## Chip e circuiti da considerare per una BSP/bring-up

| Componente | Ruolo | Evidenza disponibile |
| --- | --- | --- |
| ESP32-S3-PICO-1-N8R8 | MCU, Wi-Fi/BLE, Flash/PSRAM impilate | pagina, wiki, schema |
| CO5300 | driver del pannello AMOLED su QSPI | pagina/wiki e datasheet locale |
| CST820 | controller touch capacitivo su I2C | pagina/wiki |
| ES8311 | codec audio a basso consumo | pagina/wiki, schema e datasheet locale |
| ES7210 | acquisizione/elaborazione microfoni con echo cancellation | pagina/wiki, schema e datasheet locale |
| NS4150B | amplificatore di potenza speaker | identificato nello schema |
| ETA6098 | gestione/alimentazione (sigla nello schema) | schema |
| TPS63020DSJR | convertitore buck-boost (sigla nello schema) | schema |

Il display e il touch sono su un'interfaccia dedicata della scheda: per pinout, reti, livelli e collegamenti completi usare `hardware-docs/schematic.pdf`, non questo riepilogo. Lo schema riporta inoltre USB-C, protezione TVS, connettori, passivi e test point.

## ESP-IDF, non Arduino

La wiki dichiara supporto ESP-IDF e descrive demo per ADC/batteria, Wi-Fi AP, Wi-Fi STA, audio, LVGL 8, LVGL 9 e factory program. Raccomanda VS Code con estensione ESP-IDF e usa ESP-IDF 5.5.2 come esempio, ma chiede di scegliere la versione compatibile con il pacchetto demo.

L'archivio `software/waveshare-source-main.zip` e' stato scaricato per completezza: il repository ufficiale contiene librerie, esempi, firmware, schema e file meccanici, inclusa una parte Arduino che non serve a questo lavoro. Non e' stato usato alcun codice Arduino. Gli esempi ESP-IDF sono presenti nell'archivio nel percorso `02_Example/espidf_v5.5.3/` e sono stati estratti separatamente in `software/esp-idf-v5.5.3/`: ADC/batteria, Wi-Fi AP, Wi-Fi STA, audio, LVGL 8 e LVGL 9. Questo e' il punto di partenza BSP disponibile dal produttore. La pagina wiki cita anche un `07_FactoryProgram`, che non e' presente nello snapshot GitHub scaricato.

## Contenuto scaricato

- `images-product/` - 21 immagini ufficiali della pagina prodotto: 6 foto del prodotto, le grafiche funzionali, package content, overview/"What's on board" e dimensioni.
- `images-wiki/` - 3 tavole della wiki: resource overview ("What's on board"), interface introduction e dimensioni.
- `hardware-docs/` - schema PDF a 3 pagine, modelli 3D ZIP, datasheet ESP32-S3, TRM ESP32-S3, e datasheet/guida CO5300, ES8311 ed ES7210.
- `software/esp-idf-v5.5.3/` - 6 progetti ESP-IDF pronti da consultare, estratti dal repository ufficiale; non richiedono Arduino.
- `software/waveshare-source-main.zip` - snapshot completo dell'archivio ufficiale Waveshare su GitHub, incluso per tracciabilita'.

La pagina prodotto referenzia anche `ESP32-S3-Touch-AMOLED-1.43C-details-16.jpg`, ma al momento del download l'URL ufficiale restituisce 404; non e' stato sostituito con materiale non ufficiale.

## Fonti online

- Pagina prodotto: https://www.waveshare.com/esp32-s3-touch-amoled-1.43c.htm
- Wiki principale: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.43C
- Risorse e PDF: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.43C/Resources-And-Documents
- Guida ESP-IDF: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.43C/ESP-IDF
- Repository ufficiale: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43C

