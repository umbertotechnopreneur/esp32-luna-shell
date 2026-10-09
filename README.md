# ESP32 Luna Shell / Cody Companion

Qui sviluppo Cody e Luna sulla Waveshare ESP32-S3 Touch AMOLED 1.75: display
circolare 466 x 466, touch, 16 MB di flash e 8 MB di PSRAM. Uso **ESP-IDF 6.0.2**
e tengo separati dispositivo fisico e interfaccia scelta.

- **Cody Companion**, profilo predefinito: personaggio e UI nativi 2D, senza LVGL.
- **Luna Shell**: UI diagnostica con ESP-Brookesia 0.8 System Core e documenti
  dichiarativi. Carico le dipendenze in base al profilo.

Tengo il testo del dispositivo in inglese ASCII. Nei documenti racconto invece
in italiano cosa ho deciso, cosa ho implementato e cosa devo ancora verificare.

## Da dove parto

Uso la [mappa della documentazione](docs/README.md) come indice unico delle guide
tecniche e dei cinque atlanti Cody. Per il percorso dal primo avvio al PC leggo
[onboarding e connessione](docs/architecture/cody/onboarding.md); per installare
CCP e autorizzare i suoi hook uso la [guida del plugin](plugins/cody-codex-plugin/README.md).

CCP segue automaticamente la chat dell'ultimo prompt reale. Cody conserva il PC
associato e distingue pairing salvato, presenza sulla LAN e attivita' della chat.
Ho eliminato la scelta manuale della chat e il nuovo codice a ogni riconnessione:
un po' meno burocrazia, finalmente.

Conservo un solo registro per ogni scopo:

| Mi serve... | Lo tengo qui |
| --- | --- |
| Regole del repository e autorizzazioni | [AGENTS.md](AGENTS.md) |
| Lavoro attivo e prove mancanti | [TODO.md](TODO.md) |
| Lavoro rinviato | [planned-todo.md](planned-todo.md) |
| Implementazioni concluse ed evidenze | [done.md](done.md) |
| Errori da non ripetere | [lessons.md](lessons.md) |

## Branch e checkpoint

Uso questi tag come punti fermi dei sorgenti; il tag da solo non certifica una
build o una prova sul dispositivo.

| Tag | Commit di partenza | Branch della variante |
| --- | --- | --- |
| `base-display-touch` | `3283a194` | `codex/base-display-touch` |
| `luna-shell` | `369e5026` | `codex/luna-shell` |
| `cody` | `9d2fe8c5` | `codex/cody`, anche `main` al momento del tag |

Il lavoro di onboarding parte da `cody` su `codex/cody-onboarding`. Mantengo i tag
fermi, faccio checkpoint locali coerenti e pubblico con l'autorizzazione al push.
Le varianti storiche restano riferimenti dei sorgenti, senza aggiungere altri SDK.

## Come ci lavoro

Da PowerShell uso **solo** `scripts/device.ps1` per firmware, simulatori,
generatori e verifiche. Menu, `help` e `list` non avviano build e non aprono la
seriale.

```powershell
.\scripts\device.ps1 help
.\scripts\device.ps1 list
.\scripts\device.ps1
```

Tengo i comandi operativi nella [guida della console](scripts/README.md), senza
ricopiarli in ogni documento. Il default e' Debug / `cody-companion`; scelgo
Release esplicitamente e limito le compilazioni a otto job. Per prove e operazioni
hardware seguo [AGENTS.md](AGENTS.md), verificando prima la porta effettiva.

## Dove vive il codice

| Area | Responsabilita' |
| --- | --- |
| `main/` | Avvio e composizione del profilo |
| [CODY_companion](components/CODY_companion/README.md) | Runtime, scene, modello e diagnostica del personaggio |
| [CODY2D](components/CODY2D/README.md) | Primitive native 2D, testo e controlli |
| [LUNASHELL_platform](components/LUNASHELL_platform/README.md) | Snapshot, display, alimentazione, audio, rete e storage |
| [LUNASHELL_app](components/LUNASHELL_app/README.md) | Documenti, navigazione e shell Luna |
| [LUNASHELL_boot](components/LUNASHELL_boot/README.md) | UI e audio di avvio |
| [LUNASHELL_logger](components/LUNASHELL_logger/README.md) | Log persistenti e download USB |
| [LUNASHELL_tof400f](components/LUNASHELL_tof400f/README.md) | Trasporto del sensore di distanza |
| `plugins/cody-codex-plugin/` | CCP: broker locale, MCP, hook e protocollo LAN |
| `assets/`, `config/`, `gui/` | Risorse, configurazione e input generati del firmware |
| `scripts/` | Comandi, helper, generatori e verifiche esplicite |

Organizzo l'implementazione in `src/` e le API pubbliche in `include/`; lascio i
vendor alle loro strutture. Metto build, log, catture e file temporanei in
`artifacts/`. La pulizia conserva firmware selezionato, configurazione e cache;
le risorse versionate restano sorgenti.

Per i dati uso lo snapshot canonico con validita', errore e timestamp originali.
Per geometria e touch seguo il [contratto del display circolare](docs/architecture/circular-display-layout.md).
Conservo distinti risultato host e comportamento fisico: una compilazione riuscita
non mi dice ancora come Cody si risveglia sulla scrivania.
