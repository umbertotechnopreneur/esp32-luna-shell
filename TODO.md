# TODO

## ESP-Brookesia integration

- [ ] **Blocco corrente:** ESP-Brookesia non espone componenti applicativi generici per label, button, progress bar, text box o log page. I wrapper `LvObject`/`LvContainer` restano astrazioni sottili sopra LVGL e non soddisfano l'obiettivo di rimuovere la UI custom a basso livello.
- [ ] Decidere se contribuire questi componenti al framework ESP-Brookesia oppure attendere/integrare una versione che li fornisca nativamente.
- [ ] Portare la UI QSee a componenti e stylesheet ESP-Brookesia, mantenendo LVGL solo come backend sottostante.
- [ ] Registrare una stylesheet QSee per il display circolare 466 x 466 e la safe area 320 x 320.
- [ ] Riutilizzare il componente `StatusBar` di ESP-Brookesia come barra principale dell’app.
- [ ] Estendere lo status bar Brookesia con indicatori informativi per Bluetooth e GPS: non sono presenti nel componente standard attuale.
- [ ] Aggiungere al modello di status bar Brookesia CPU per core, memoria libera e spazio SD: non sono campi supportati dal componente standard attuale.
- [ ] Verificare se le barre di progresso e il viewer dei log hanno componenti Brookesia riutilizzabili; in caso contrario creare componenti app-specifici compatibili con il tema Brookesia.
- [ ] Sostituire la navigazione custom corrente con le route/schermate Brookesia, inclusa la pagina full-screen dei log e il pulsante Back.

## Dati ancora da esporre

- [ ] Pubblicare nello snapshot lo spazio libero della SD, oltre alla capacità totale già disponibile.
- [ ] Pubblicare una misura di utilizzo CPU per ciascun core, aggiornata a intervalli regolari.
- [ ] Definire l’aggiornamento non bloccante dei dati hardware ogni secondo, evitando campionamenti I2C nel thread UI.
