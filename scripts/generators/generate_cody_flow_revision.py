"""Revise the five Cody flow PDFs in place while preserving their dated history."""

import argparse
from dataclasses import dataclass
from enum import Enum
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path

from pypdf import PdfReader, PdfWriter
from reportlab.lib.colors import HexColor
from reportlab.pdfgen import canvas

from generate_cody_setup_revision import paragraph, register_fonts


ROOT = Path(__file__).resolve().parents[2]
REVISION = "CODY-RETAINED-FLOW-20261010"
DATE = "10 ottobre 2026"
INK = HexColor("#142737")
MUTED = HexColor("#536776")
TEAL = HexColor("#008E87")
RULE = HexColor("#D6E1E8")
PALE = HexColor("#F1F6F9")
WHITE = HexColor("#FFFFFF")


class DocumentId(str, Enum):
    Navigation = "navigation"
    Planning = "planning"
    Quickstart = "ccp-pairing-quickstart-a5"
    Service = "ccp-service"
    Connection = "codex-connection"


class Layout(str, Enum):
    Sections = "sections"
    Screens = "screens"
    Actions = "actions"


@dataclass(frozen=True)
class PageSpec:
    title: str
    subtitle: str
    sections: tuple
    layout: Layout = Layout.Sections
    note: str = ""


@dataclass(frozen=True)
class DocumentSpec:
    filename: str
    label: str
    original_pages: int
    main_pages: int


DOCUMENTS = {
    DocumentId.Navigation: DocumentSpec("cody-navigation-atlas-v1.pdf", "NAVIGATION ATLAS", 38, 12),
    DocumentId.Planning: DocumentSpec("cody-planning-atlas-v1.pdf", "PLANNING ATLAS", 38, 22),
    DocumentId.Quickstart: DocumentSpec("cody-ccp-pairing-quickstart-a5-v1.pdf", "CCP QUICK START", 13, 2),
    DocumentId.Service: DocumentSpec("cody-ccp-service-atlas-v1.pdf", "CCP SERVICE ATLAS", 20, 10),
    DocumentId.Connection: DocumentSpec("cody-codex-connection-atlas-v1.pdf", "CODEX CONNECTION ATLAS", 24, 14),
}

BOOT = (
    ("01-03 / Avvio locale", "Apro con VibeWare e poi mostro i checkpoint di Cody. Conservo gli errori "
     "reali dei servizi. Con Ready confermo che lo stadio locale e' pronto; per PC e chat aspetto "
     "le rispettive prove di connessione."),
    ("04-09 / Nessuna rete salvata", "Avvio il provisioning dal telefono solo se il profilo Wi-Fi "
     "manca davvero. Con credenziali salvate ma senza IP locale apro 42 / No connection. "
     "Tratto una lettura fallita come errore, senza inventare un primo avvio."),
    ("40 / LAN pronta, pairing assente", "Mi bastano associazione Wi-Fi e IP locale valido. "
     "Connect Codex? offre Yes e No. Con No torno a 16, senza avviare CCP o salvare un rifiuto. "
     "Non aggiungo un controllo separato dell'accesso a Internet."),
    ("14-15 / Pairing gia' salvato", "Riutilizzo l'identita' del PC conservata e validata. Cerco "
     "automaticamente; mostro Connected solo dopo READY autenticato. Un'interruzione Wi-Fi, "
     "lo standby o un normale aggiornamento firmware non mi chiedono un altro codice."),
)

PAIRING = (
    ("41 / Apro Setup", "Tengo il dito fermo sul personaggio visibile per 1,5 secondi. Metto "
     "Connect Codex e Network setup affiancati, Options sotto al centro. Prima di scegliere "
     "rilascio il dito. Mantengo questo ingresso anche mentre il PC e' offline."),
    ("Variante 40 / Already paired", "Controllo il record prima della rete. Mostro Keep pairing "
     "e Pair again affiancati, Forget sotto. Con un candidato incompleto uso Pairing paused; "
     "con errore storage mostro Pairing error e abilito solo la cancellazione esplicita."),
    ("Keep pairing / Riprendo", "Riutilizzo una presenza autenticata ancora valida; altrimenti "
     "cerco il PC salvato. Anche Retry riprende questa associazione e puo' attendere il ritorno "
     "della LAN. Una riconnessione non autorizza la sostituzione del PC."),
    ("Pair again / Sostituisco consapevolmente", "Genero il codice dopo conferma esplicita e "
     "con LAN utilizzabile. Salvo prima un candidato separato. Lo promuovo solo dopo la "
     "prova che il PC ha salvato il link. Cancel scarta il candidato e conserva l'attivo."),
)

TRANSACTION = (
    ("Due candidati, attivi conservati", "Sul PC salvo il candidato DPAPI prima di "
     "PAIR; sul Cody uso ccp_pending. Tengo separati i vecchi attivi. CONFIRM oppure "
     "READY fresco provano il PC e consentono commit e rilettura sul Cody."),
    ("Chiudo con una prova del commit", "Il PC promuove dopo ACTIVE o COMMITTED "
     "autenticati, mai per il solo FIND. Dopo ogni READY valido Cody ripete COMMITTED "
     "con il nonce corrente: recupero riavvii e ACK persi senza un altro codice."),
    ("Cancel / Chiudo il tentativo", "Dal codice pronto scarto il candidato Cody. "
     "Conservo il vecchio link operativo; sul PC il candidato resta pending. "
     "Per sostituirlo richiedo nuovo codice e replace_pending:true esplicito. "
     "Una prova con la vecchia chiave non autorizza la cancellazione."),
    ("Forget / Solo il pairing Codex", "Confermo Forget pairing con Go back / Forget "
     "e Only Codex is reset. Cancello solo i record CCP di Cody; il PC conserva il suo. "
     "Dopo l'esito reale mostro Unpaired / Pairing removed, poi Done -> Setup."),
)

CONTEXT = (
    ("15 / Connected, Waiting for Codex", "Con READY autenticato so che CCP e' raggiungibile. "
     "Per la chat aspetto invece un evento reale. Waiting for Codex descrive l'assenza di quel "
     "contesto; ripetere il pairing non lo farebbe comparire per magia."),
    ("Seguo l'ultimo prompt reale", "Affido a UserPromptSubmit autorizzato la scelta della chat "
     "seguita da CCP. Cody legge lo snapshot autenticato. Ho eliminato il selettore di chat "
     "dal dispositivo e il passaggio manuale di scelta dentro Codex."),
    ("Autorizzo gli hook, poi li eseguo localmente", "Dopo installazione e attivazione rivedo "
     "gli hook di CCP in Codex; /hooks apre la revisione nella CLI locale. Gli hook command "
     "richiedono orchestrazione locale. La sola installazione non li autorizza."),
    ("Tengo le altre chat sullo sfondo", "Tool, completamenti e interruzioni di altre chat "
     "non cambiano il contesto seguito. Un nuovo prompt si'. Il click su una scheda non e' "
     "un hook documentato. Dopo il riavvio del broker aspetto un nuovo prompt reale."),
)

RECOVERY = (
    ("Internet giu', LAN funzionante", "Continuo a usare il collegamento Wi-Fi/IP locale tra "
     "Cody e CCP. Tengo separata la disponibilita' dei servizi online di Codex: il PC presente "
     "sulla LAN non mi garantisce una risposta del modello."),
    ("Wi-Fi o IP locale assenti", "Conservo credenziali e PC associato. All'avvio mostro 42 "
     "con Network setup o Later. Durante il recupero attendo la LAN e riprovo con l'identita' "
     "salvata. Gli errori reali del trasporto restano visibili nello stato."),
    ("PC o CCP assenti", "Continuo la ricerca su 14; uso 32 per la perdita dopo un'attivita' "
     "gia' seguita. Retry riprende la ricerca senza nuovo codice. Posso comunque tenere "
     "premuto Cody per 1,5 secondi e aprire Setup."),
    ("35A / 35B e aggiornamento firmware", "Con lo standby ordinario conservo percorso e "
     "pairing. Al risveglio recupero la presenza autenticata. Un flash applicativo normale "
     "preserva NVS; cancellazione, dati danneggiati o altra identita' PC sono casi distinti."),
)

PERSONALIZATION = (
    ("10 / Make it yours", "Da Setup apro Options. Imposto Shake to wake, Auto-rotate e "
     "Sounds su On oppure Off con righe compatte che mostrano i valori. La quarta "
     "riga e' Orientation. Sounds Off lascia microfono e wake word attivi. "
     "Next continua verso Settings, senza avviare alcun pairing."),
    ("36 -> 11 / Skin", "Uso lo stesso selettore con anteprima animata e identificatori stabili "
     "salvati. Ho rinominato lo screen 11 Skin: scelgo l'aspetto di Cody, oltre il vecchio "
     "concetto di solo colore. Se il salvataggio fallisce, mantengo l'errore reale."),
    ("Torno a chi mi ha aperto", "Entrando da Setup, Done torna a 41 e Back da 10 riporta a "
     "Setup. La personalizzazione non avvia pairing di rete o Codex. Nel primo avvio "
     "proseguo invece verso la decisione sul collegamento salvato."),
    ("Orientation e salvataggio reale", "Scelgo 0 oppure 180 solo con Auto-rotate Off; "
     "altrimenti mostro Auto. Confermo ogni valore solo dopo scrittura e rilettura "
     "riuscite. Mantengo l'errore visibile; Retry ripropone lo stesso valore richiesto."),
)

ACTIVITY = (
    ("17 / Project active", "Context from Codex", "Mostro il contesto dell'ultima chat con un prompt. "
     "Il nome progetto e' un'etichetta ASCII; l'identita' della chat resta distinta. Non acquisisco ancora il branch."),
    ("18 / Working", "Working in Codex", "Entro in Working dopo un prompt reale. Gli hook dei tool "
     "forniscono Editing o Running. Con un nuovo prompt posso seguire automaticamente un'altra chat."),
    ("18U / Unavailable", "No recent activity", "Dopo 120 secondi senza un nuovo hook considero "
     "scaduta l'attivita' in corso. Il solo heartbeat del PC non mi dice se la chat sta ancora lavorando."),
)

REPLIES = (
    ("19 / Response ready", "Read it in Codex", "Interpreto Stop come risposta pronta da leggere. "
     "Il risultato del lavoro, dei test o del deploy resta quello dichiarato e verificato nella chat."),
    ("20A / Paused", "Continue in Codex", "Con PermissionRequest mostro l'attesa di approvazione; "
     "con Interrupt il turno interrotto. Un completamento sullo sfondo non lo riavvia."),
    ("20B -> 16 / Ended", "Waiting for Codex", "Mostro SessionEnd per cinque secondi attivi, poi "
     "lascio riposare Cody. Il prossimo prompt reale fornisce di nuovo il contesto senza altro pairing."),
)


# pdf: Destination vector canvas.
# dimensions: Existing source page dimensions in points.
# label: Document identity shown above the title.
# title: Bounded heading for the authored page.
# subtitle: Scope and stable screen IDs.
# number: Physical page number, not a screen identity.
# Raises ValueError if text leaves the printable page.
def page_frame(pdf, dimensions, label, title, subtitle, number):
    width, height = dimensions
    compact = width < 500
    margin = 26 if compact else 34
    paragraph(pdf, "CODY / " + label + " / FLOW ATTUALE", margin, 22,
              width - 2 * margin, height, 7.5, True, TEAL)
    title_end = paragraph(pdf, title, margin, 48, width - 2 * margin, height,
                          23 if compact else 27, True)
    subtitle_end = paragraph(pdf, subtitle, margin, title_end + 12,
                            width - 2 * margin, height, 9 if compact else 10, False, MUTED)
    content_top = max(142, subtitle_end + 22)
    pdf.setStrokeColor(RULE)
    pdf.line(margin, height - content_top + 11, width - margin, height - content_top + 11)
    pdf.line(margin, 46, width - margin, 46)
    pdf.setFillColor(MUTED)
    pdf.setFont("SetupBody", 6.5 if compact else 7)
    pdf.drawString(margin, 31, "Umberto Giacobbi | hello@umbertogiacobbi.biz | riservato")
    pdf.drawString(margin, 19, REVISION + " | Riferimenti per screen ID")
    pdf.drawRightString(width - margin, 19, str(number))
    return content_top


# pdf: Destination canvas.
# dimensions: Source geometry, A4 landscape or A5 portrait.
# spec: Up to four authored sections with bounded text.
# top: First available content y measured from the page top.
# Raises ValueError if a section exceeds its allocated row.
def draw_sections(pdf, dimensions, spec, top):
    width, height = dimensions
    compact = width < 500
    if not 1 <= len(spec.sections) <= 4:
        raise ValueError("A sections page requires one to four sections")
    margin = 26 if compact else 34
    columns = 1 if compact else 2
    rows = (len(spec.sections) + columns - 1) // columns
    column_width = (width - 2 * margin - (columns - 1) * 26) / columns
    bottom = height - (78 if spec.note else 62)
    row_height = (bottom - top) / rows
    for index, (title, copy) in enumerate(spec.sections):
        x = margin + (index % columns) * (column_width + 26)
        y = top + (index // columns) * row_height
        end = paragraph(pdf, title, x, y, column_width, height, 11 if compact else 13, True)
        end = paragraph(pdf, copy, x, end + 7, column_width, height, 9 if compact else 10.5)
        if end > y + row_height - 12:
            raise ValueError("Section exceeds its row: " + title)
    if spec.note:
        paragraph(pdf, spec.note, margin, height - 77, width - 2 * margin, height, 8, False, MUTED)


# pdf: Destination canvas.
# center_x: Screen center measured from the left.
# center_y: Screen center in PDF bottom-up coordinates.
# caption: Illustrative status label, never a device observation.
def draw_screen(pdf, center_x, center_y, caption):
    pdf.setFillColor(HexColor("#000000"))
    pdf.circle(center_x, center_y, 64, stroke=0, fill=1)
    pdf.setFillColor(WHITE)
    for dx, dy, radius in ((-20, 14, 14), (-3, 24, 20), (19, 14, 16)):
        pdf.circle(center_x + dx, center_y + dy, radius, stroke=0, fill=1)
    pdf.setFillColor(INK)
    for dx in (-10, 10):
        pdf.circle(center_x + dx, center_y + 22, 2.5, stroke=0, fill=1)
    pdf.setFillColor(WHITE)
    pdf.setFont("SetupBold", 8)
    pdf.drawCentredString(center_x, center_y - 24, caption)
    pdf.setFillColor(HexColor("#7ACCC4"))
    pdf.setFont("SetupBody", 6.5)
    pdf.drawCentredString(center_x, center_y - 39, "Stato illustrativo")


# pdf: Destination canvas.
# dimensions: Existing A4 landscape page geometry.
# spec: Three screen descriptions and a bounded evidence note.
# top: First available content y measured from the page top.
# Raises ValueError for wrong card count or overflowing descriptions.
def draw_screens(pdf, dimensions, spec, top):
    if len(spec.sections) != 3 or dimensions[0] < 500:
        raise ValueError("Screen illustrations require three landscape cards")
    width, height = dimensions
    card_width = (width - 100) / 3
    for index, (identity, caption, copy) in enumerate(spec.sections):
        x = 34 + index * (card_width + 16)
        pdf.setFillColor(PALE)
        pdf.roundRect(x, height - top - 303, card_width, 303, 12, stroke=0, fill=1)
        paragraph(pdf, identity, x + 14, top + 12, card_width - 28, height, 11, True, TEAL)
        draw_screen(pdf, x + card_width / 2, height - top - 112, caption)
        end = paragraph(pdf, copy, x + 14, top + 194, card_width - 28, height, 10)
        if end > top + 290:
            raise ValueError("Screen description exceeds its card: " + identity)
    paragraph(pdf, spec.note, 34, top + 322, width - 68, height, 10, False, MUTED)


# pdf: Destination canvas.
# dimensions: Existing A4 landscape page geometry.
# top: First available content y measured from the page top.
# Raises OSError for missing approved visual references.
# Raises ValueError for text overflowing the page.
def draw_action_language(pdf, dimensions, top):
    width, height = dimensions
    references = ROOT / "docs/Cody/ui-proposals/20261009"
    for index, filename in enumerate(("selected-multi-action.png", "selected-single-action.png")):
        x = 34 + index * 236
        pdf.drawImage(str(references / filename), x, height - top - 230,
                      width=222, height=222, preserveAspectRatio=True, mask="auto")
    x = 525
    text_width = width - x - 34
    y = paragraph(pdf, "Due / tre / uno", x, top + 8, text_width, height, 13, True)
    y = paragraph(pdf, "Con due azioni uso card affiancate. La terza va sotto, al centro. "
                  "Per una sola azione scelgo una capsula larga con icona a sinistra.", x, y + 8,
                  text_width, height, 10.5) + 16
    y = paragraph(pdf, "Il colore ha un significato", x, y, text_width, height, 13, True)
    paragraph(pdf, "Uso teal per conferma/Codex, blu per rete, grafite per scelte neutre, "
              "No e Later, ambra per Retry, Cancel o sostituzione. Aggiungo icone e gradienti discreti.",
              x, y + 8, text_width, height, 10.5)
    paragraph(pdf, "DECISIONE UI IMPORTANTE / approvata il 9 ottobre 2026", 34, top + 251,
              width - 68, height, 12, True, TEAL)
    paragraph(pdf, "Faccio comparire i pulsanti completi dopo titolo e riga principale; abilito il "
              "touch solo quando sono visibili. Il teletype resta sul testo narrativo. Conservo "
              "Back laterale, quiet zone dei QR e corde del cerchio. Queste immagini sono i "
              "riferimenti approvati; l'aspetto fisico del firmware richiede una prova separata.",
              34, top + 278, width - 68, height, 10.5)


# specification: Authored page recipe with literal document text.
# dimensions: Source page dimensions retained unchanged.
# label: Canonical document identity.
# number: Physical page number for the footer.
# Raises OSError or ValueError for missing assets or failed authoring.
def authored_page(specification, dimensions, label, number):
    stream = BytesIO()
    pdf = canvas.Canvas(stream, pagesize=dimensions, pageCompression=1)
    top = page_frame(pdf, dimensions, label, specification.title, specification.subtitle, number)
    if specification.layout == Layout.Sections:
        draw_sections(pdf, dimensions, specification, top)
    elif specification.layout == Layout.Screens:
        draw_screens(pdf, dimensions, specification, top)
    elif specification.layout == Layout.Actions:
        draw_action_language(pdf, dimensions, top)
    else:
        raise ValueError("Unknown page layout: " + str(specification.layout))
    pdf.showPage()
    pdf.save()
    stream.seek(0)
    return PdfReader(stream).pages[0]


# identity: Stable requested document identity.
# Returns physical-page replacements; screen IDs remain in the authored text.
# Raises ValueError for an identity outside the explicitly requested document set.
def main_revisions(identity):
    common_cover = (
        ("Associo una volta; recupero il PC salvato", "Tengo separati provisioning di rete e pairing Codex. "
         "Conservo l'associazione durante standby, interruzioni temporanee e normali aggiornamenti. "
         "Per sostituirla chiedo una scelta esplicita."),
        ("Cody mostra; CCP porta la chat", "Seguo automaticamente la chat dell'ultimo prompt reale. "
         "Connected conferma CCP sulla LAN; Waiting for Codex mi dice che manca ancora un contesto "
         "reale. Ho tolto il passaggio Pick a chat."),
        ("Setup resta a portata di dito", "Con una pressione ferma di 1,5 secondi apro Setup. "
         "Connect Codex gestisce il PC, Network setup la rete, Options le preferenze. Con un "
         "record salvato mostro Already paired: Keep pairing, Pair again oppure Forget."),
        ("Come leggo questa revisione", "Ho aggiornato le pagine principali e conservato in "
         "fondo addenda ed errata datati. Uso gli screen ID come riferimento. Distinguo sempre "
         "codice scritto, build, installazione e comportamento osservato sul dispositivo."),
    )
    if identity == DocumentId.Navigation:
        return {
            1: PageSpec("Cody / Atlante della navigazione", "Dal primo avvio al PC salvato, fino all'attivita' reale della chat.", common_cover),
            2: PageSpec("All'avvio leggo le scelte salvate", "Screen 01-09, 14-16, 40 e 42 / per CCP mi basta la LAN.", BOOT),
            3: PageSpec("La rete ha un percorso dedicato", "Screen 04-09 e 42 / il provisioning Wi-Fi resta distinto dal pairing Codex.", (
                ("04 -> 05 -> 06 -> 07", "Presento Cody con Welcome. Show QR apre l'installazione "
                 "dell'app; Pair Cody entra in 07, che mostra il QR reale quando e' pronto. "
                 "Conservo 06-bis nei dati d'autore, senza aggiungere uno step runtime."),
                ("07 -> 09 / Aspetto una LAN reale", "Attendo provisioning, associazione Wi-Fi e IP "
                 "DHCP. Un SSID salvato, un link BLE o una richiesta ammessa non bastano. Con 09 "
                 "confermo la rete locale senza una verifica aggiuntiva di Internet."),
                ("08 / Errore e Retry esplicito", "Mantengo l'errore originale. Cancel e Back "
                 "attendono la chiusura dell'operazione attiva. Con Retry riprendo il passaggio "
                 "di rete pertinente; non cambio l'associazione al PC."),
                ("Inattivita' o rete salvata offline", "Dopo un timeout posso tornare a Welcome "
                 "e riaprire Setup. Se ho credenziali ma manca l'IP, 42 offre Network setup o "
                 "Later. L'assenza temporanea della rete non cancella le credenziali."),
            )),
            4: PageSpec("Setup protegge il pairing esistente", "Screen 41, variante di conferma 40 e screen 12.", PAIRING),
            5: PageSpec("Mi collego e seguo il prompt", "Screen 13-20 e 32 / Let's go avvia subito la ricerca del PC salvato.", CONTEXT),
            6: PageSpec("L'attivita' quotidiana arriva da Codex", "Screen 17, 18 e 18U / leggo metadati autenticati del ciclo di lavoro.", ACTIVITY, Layout.Screens,
                        "Uso 19 dopo Stop, 20A per pausa/interruzione, 20B dopo SessionEnd. I pacchetti "
                        "di attivita' scadono dopo otto secondi; verifico separatamente hook reali e display."),
            10: PageSpec("Standby, guasti di rete e recupero", "Screen 14-16, 32, 35A, 35B, 41 e 42 / conservo l'identita' associata.", RECOVERY),
            12: PageSpec("Options e ritorno al chiamante", "Screen 10, 11, 36 e 41 / ogni percorso sa dove tornare.", PERSONALIZATION),
        }
    if identity == DocumentId.Planning:
        return {
            1: PageSpec("Cody / Atlante di progettazione", "Schermate attuali, confini dell'implementazione e storico in fondo.", common_cover),
            2: PageSpec("Inventario delle schermate", "Conservo gli screen ID; Already paired e' una variante di conferma.", (
                ("01-09 / Ingresso e rete locale", "Uso 01 Sleeping, 02 Waking, 03 Ready, 04 Welcome, "
                 "05 Let's pair, 06 Install QR, 07 Pairing/QR dispositivo, 08 Try again "
                 "e 09 LAN ready. Conservo 06-bis come dato d'autore e 39 come passaggio Debug."),
                ("10-16 e 40-42 / Setup e collegamento", "Raccolgo 10 Make it yours, 36 Settings, "
                 "11 Skin, 12 Connect Codex, 13 You're all set, 14 Looking for PC, 15 Connected "
                 "/ Waiting for Codex, 16 Idle, 40 Connect Codex? / Already paired, 41 Setup, 42 No connection."),
                ("17-32 / Attivita' e lavoro previsto", "Uso 17 Project active, 18 Working, "
                 "18U Unavailable, 19 Response ready, 20A Paused, 20B Ended e 32 Connection lost. "
                 "Tengo PR 21-24 e domande/voce/risposte 26-31 tra le funzioni previste. Ho ritirato 25."),
                ("33-38 / Dispositivo e sviluppo", "Conservo 33 Low battery, 34 Charging, "
                 "35A Sleep, 35B Wake, 36 Settings, 37 Rig view, 38 Expression channels. "
                 "Per le azioni applico sempre il layout uno, due affiancati oppure due piu' uno."),
            )),
            4: PageSpec("L'avvio parte dallo stato salvato", "Screen 01-09, 14-16, 40 e 42 / evito un commissioning inutile.", BOOT),
            5: PageSpec("Presento Cody e preparo la rete", "Screen 04-07 / qui il QR serve al provisioning Wi-Fi.", (
                ("04 / Welcome", "Entro nel setup di rete automatico solo se il profilo manca "
                 "davvero. Su un dispositivo gia' configurato decido invece da rete e pairing "
                 "osservati nello snapshot canonico."),
                ("05 / Let's pair", "Con Show QR introduco il setup Wi-Fi assistito dal telefono. "
                 "Questa frase appartiene al provisioning di rete; tengo distinto il consenso "
                 "a sostituire il PC Codex. Mostro il pulsante completo, senza teletype."),
                ("06 / App di pairing", "Tengo distinti QR di installazione e QR del dispositivo. "
                 "Conservo la quiet zone del codice e la capsula compatta inferiore entro "
                 "la corda disponibile sul display circolare."),
                ("07 / Provisioning reale", "Mostro il QR di sessione dentro 07 dopo ReadyToScan; "
                 "06-bis resta un dato d'autore. Prima di uscire chiudo l'operazione: un vecchio "
                 "tentativo non deve riaprire una schermata gia' abbandonata."),
            )),
            6: PageSpec("Completo il setup Wi-Fi", "Screen 07-09 e 42 / misuro la disponibilita' della rete.", (
                ("07 / Pairing", "Dal telefono ricevo le credenziali e aspetto associazione e "
                 "IP locale. Mantengo Cancel esplicito. La mia fonte e' il risultato del worker, "
                 "non l'animazione o il solo SSID memorizzato."),
                ("08 / Try again", "Mostro l'operazione fallita e il suo errore. Retry torna al "
                 "passaggio di rete; Cancel attende la chiusura. Un retry Wi-Fi non sostituisce "
                 "il PC Codex ricordato."),
                ("09 / LAN ready", "Con Wi-Fi e IP valido posso usare CCP. Continue apre "
                 "personalizzazione e decisione sul link salvato. Con questa schermata "
                 "confermo la LAN, non la raggiungibilita' di un servizio Internet."),
                ("42 / No connection", "Con credenziali salvate ma senza IP offro Network setup "
                 "e Later affiancati. Con Later entro in Idle per questa sessione. Tengo "
                 "credenziali e pairing durante un'interruzione temporanea."),
            )),
            7: PageSpec("Make it yours, senza cambiare il PC", "Screen 10, 11, 36 e 41 / Options conserva il suo chiamante.", PERSONALIZATION),
            8: PageSpec("Associo e poi mi collego subito", "Screen 12-15 / ho tolto la pausa intermedia di dieci secondi.", (
                ("13 / You're all set", "Pairing saved", "Entro qui dopo la conferma del link "
                 "salvato su entrambi i lati. Con Let's go avvio subito la ricerca del PC su 14."),
                ("14 / Looking for PC", "Looking for PC", "Riutilizzo l'identita' salvata e "
                 "attendo la risposta autenticata. Con 1,5 secondi sul personaggio posso ancora aprire Setup."),
                ("15 / Connected", "Waiting for Codex", "Il PC associato e' raggiungibile. "
                 "Aspetto il contesto dell'ultimo prompt reale attraverso gli hook autorizzati."),
            ), Layout.Screens, "Su 12 mostro l'attesa mentre preparo il codice. Dal codice pronto tengo "
               "Cody sveglio per tre minuti; la finestra del protocollo di pairing dura cinque minuti."),
            9: PageSpec("Cody segue il lavoro che invio", "Screen 17, 18 e 18U / nessuna scelta di chat dal dispositivo.", ACTIVITY, Layout.Screens,
                        "I Cody associati al PC seguono la chat dell'ultimo prompt reale. Il cambio di "
                        "scheda non e' un hook documentato. CCP conserva metadati, non prompt o risposte."),
            10: PageSpec("Risposta, pausa e riposo", "Screen 19, 20A, 20B e 16 / il volto non certifica il risultato del lavoro.", REPLIES, Layout.Screens,
                         "Distinguo l'accettazione locale di un hook dai pixel aggiornati sul display. "
                         "Registro separatamente la prova di trasporto, quella visiva e quella di risveglio."),
            14: PageSpec("Risposte previste; recupero attuale", "Screen 30-32 / separo i futuri messaggi dalla presenza implementata.", (
                ("30 / Review transcript - previsto", "Voglio leggere la futura trascrizione "
                 "prima di Send. Record again torna alla registrazione e Back alla domanda. "
                 "Non attribuisco questa consegna all'adapter di attivita' attuale."),
                ("31 / Reply sent - previsto", "Mostrero' la conferma solo dopo l'ACK del "
                 "destinatario. Stop oggi significa risposta pronta in Codex; non dimostra "
                 "che Cody abbia inviato una risposta."),
                ("32 / Connection lost", "Se perdo i pacchetti dopo aver seguito una chat, "
                 "conservo il pairing e mostro il recupero. Quando la LAN torna, riprendo "
                 "lo snapshot corrente del broker senza chiedere un nuovo codice."),
                ("14 / Contesto ancora assente", "Prima di ricevere attivita' uso la ricerca "
                 "del PC. Al ritorno di READY posso ancora mostrare Waiting for Codex. "
                 "Tengo distinti pairing salvato, presenza e attivita' reale."),
            )),
            16: PageSpec("Mi risveglio e torno al punto giusto", "Screen 35B, 36, 37 e 41 / lo standby non e' un reset del pairing.", (
                ("35B / Wake", "Ripristino il percorso utile dopo la transizione di alimentazione. "
                 "Recupero LAN e presenza autenticata quando serve. Il tempo trascorso al buio "
                 "non conta per il gesto di pressione da 1,5 secondi."),
                ("41 / Accesso a Setup", "Tengo fermo il dito, apro il menu e rilascio prima "
                 "di scegliere. Mantengo l'accesso dalle schermate stabili, di attesa e recupero. "
                 "Un provisioning attivo deve prima chiudere la propria operazione."),
                ("36 / Settings", "Riutilizzo il selettore Skin e le preferenze locali supportate. "
                 "Se arrivo da Options, Done torna a Setup. Tengo il salvataggio fuori dal "
                 "renderer e rendo visibili gli errori effettivi."),
                ("37 / Rig e prove fisiche", "Uso la vista di sviluppo per il rig condiviso. "
                 "Per touch, pannello e percorso al risveglio osservo il dispositivo. "
                 "Una build riuscita resta una build riuscita, senza superpoteri."),
            )),
            21: PageSpec("Un linguaggio UI, colori con significato", "Decisione importante / riferimenti approvati, gradienti e icone.", (), Layout.Actions),
            22: PageSpec("Already paired: decido prima", "Screen 41 -> variante di conferma 40 / correggo il buco di analisi.", PAIRING),
        }
    if identity == DocumentId.Quickstart:
        return {
            1: PageSpec("Installo CCP sul mio PC", "Windows / plugin locale / Python disponibile come comando python.", (
                ("1 / Registro la sorgente locale", "Dalla radice del repository eseguo "
                 "codex plugin marketplace add . per registrare il catalogo incluso. "
                 "Il vecchio ZIP era un'idea di distribuzione, non l'installazione attuale."),
                ("2 / Installo e attivo", "Riavvio Codex, apro Plugins Directory e scelgo "
                 "Cody local plugins. Installo CCP - Cody's Codex Plugin sul PC "
                 "che voglio far ricordare a Cody."),
                ("3 / Autorizzo l'attivita' reale", "Rivedo e autorizzo gli hook in Codex; "
                 "nella CLI locale uso /hooks. Gli hook command richiedono orchestrazione "
                 "locale: installare il plugin da solo non li avvia."),
                ("4 / Preparo la LAN", "Tengo Cody e PC sulla rete locale raggiungibile. "
                 "Se richiesto, autorizzo Python sulle reti Private per UDP 42117/42118. "
                 "CCP non richiede una verifica dell'accesso a Internet."),
            )),
            2: PageSpec("Un pairing. Poi riprendo.", "Screen 12-15 e 41 / uso il codice attuale sul Cody fisico.", (
                ("1 / Apro Connect Codex", "Tengo premuto Cody per 1,5 secondi. Con "
                 "Already paired scelgo Keep pairing, Pair again oppure Forget. "
                 "Senza associazione aspetto il codice; Cancel chiude il tentativo."),
                ("2 / Chiedo il collegamento", "In Codex chiedo di scoprire Cody e "
                 "associarlo con il codice che leggo sul display. Lascio aperto 12. "
                 "Non riciclo codici scaduti o esempi stampati."),
                ("3 / All set -> Let's go", "All set conferma il pairing salvato. "
                 "Let's go cerca subito il PC. Connected conferma CCP; con "
                 "Waiting for Codex aspetto ancora il contesto di un prompt reale."),
                ("4 / Invio il lavoro in Codex", "L'ultimo prompt reale sceglie la chat. "
                 "Se non arriva attivita', controllo hook e orchestrazione locale. "
                 "Per un'interruzione temporanea recupero il link senza nuovo pairing."),
            )),
        }
    if identity == DocumentId.Service:
        return {
            1: PageSpec("CCP collega presenza e attivita'", "Broker Python locale / plugin Codex / contratto ricavato dai sorgenti.", common_cover),
            2: PageSpec("Un broker, due ingressi distinti", "Uso i tool MCP per il link e gli hook reali per il contesto della chat.", (
                ("Codex -> tool MCP locali", "Uso ccp_discover_devices, ccp_pair_device e "
                 "ccp_connection_status per associazione e presenza. Con ccp_list_sessions "
                 "leggo le sessioni osservate. Ho rimosso la selezione manuale."),
                ("Hook autorizzati -> broker", "UserPromptSubmit identifica l'ultima chat "
                 "con un prompt. Gli eventi successivi la aggiornano; le altre chat non "
                 "la scalzano. Non conservo prompt, risposte, trascrizioni o argomenti dei tool."),
                ("Broker -> Cody", "Affido UDP 42118 e identita' PC protetta a un solo broker. "
                 "I client MCP lo condividono sul canale locale autenticato. A FIND rispondo "
                 "con READY e, quando richiesta, attivita' ACTIVITY autenticata."),
                ("Simulatore esplicito", "Con simulator -Ccp apro un endpoint CCP1 loopback "
                 "con identita' e archivio DPAPI propri. La discovery lo include solo con "
                 "include_simulator:true. Preview e risultati restano marcati SIMULATED; "
                 "non eseguo ESP-IDF o i gesti nativi."),
            )),
            5: PageSpec("Persisto i candidati prima della conferma", "Screen 12-13 / journal separati su Cody e PC; aggiungo COMMITTED a CCP1.", TRANSACTION),
            7: PageSpec("Ricerco con l'identita' salvata", "Screen 13 -> 14 -> 15 / ho tolto nuovo codice e attesa artificiale dal recupero.", (
                ("Dopo All set", "Let's go richiede subito la ricerca del PC salvato. Al "
                 "boot di ritorno passo dallo stesso owner. Il codice temporaneo serve "
                 "ad associare il dispositivo, non a ricollegarlo ogni volta."),
                ("Durante una perdita di rete o standby", "Aspetto Wi-Fi/IP e stato awake "
                 "dallo snapshot canonico. Riprovo usando il record salvato. Se un invio "
                 "fallisce, mantengo l'errore fino a un recupero reale."),
                ("Retry esplicito", "Con pairing salvato riprendo la discovery. Keep pairing "
                 "conserva una presenza autenticata ancora valida. Posso aprire Setup "
                 "anche quando il PC resta irraggiungibile."),
                ("Sostituzione esplicita", "START_PAIRING non sovrascrive un link attivo. "
                 "Already paired ammette la sostituzione; tengo il candidato separato "
                 "fino alla prova PC. Forget richiede una conferma e cancella solo i dati CCP."),
            )),
            8: PageSpec("Presenza e attivita' scadono separate", "Screen 14, 15, 18U e 32 / conservo le prove e i loro timestamp.", (
                ("Presenza sul dispositivo", "Invio normalmente FIND ogni due secondi. "
                 "Dopo otto secondi senza READY autenticato torno in discovery. Errori "
                 "temporanei di radio o invio preservano pairing e causa reale."),
                ("Presenza sul PC", "Lo stato pubblico host usa una propria soglia di "
                 "dieci secondi. Un record salvato parte disconnesso fino al traffico "
                 "autenticato. Errori del listener e accesso negato restano errori."),
                ("Attivita' consegnata", "Lego ACTIVITY al nonce atteso e alla sua "
                 "autenticazione. Sul dispositivo i pacchetti scadono dopo otto secondi. "
                 "UI e audio leggono lo snapshot canonico, senza letture autonome."),
                ("Freschezza del lavoro", "Dopo 120 secondi senza un hook reale marco "
                 "l'attivita' come non disponibile. READY non la rinnova. Interpreto "
                 "Stop come risposta pronta; dopo riavvio del broker attendo un nuovo prompt."),
            )),
            10: PageSpec("Installazione e limiti delle prove", "Tengo distinti sorgenti, trasporto osservato e comportamento fisico.", (
                ("Rete e durata del processo", "Uso un dominio IPv4 con broadcast "
                 "raggiungibile. VLAN, VPN, isolamento guest e firewall possono bloccarlo. "
                 "Il broker termina dopo 60 secondi senza lease di trasporti MCP o hook."),
                ("Installazione e hook", "Uso il marketplace locale incluso e autorizzo "
                 "gli hook dopo averli rivisti. Gli hook command richiedono orchestrazione "
                 "locale. Il solo trasporto MCP connesso non ne dimostra l'esecuzione."),
                ("Evidenza disponibile", "Ho il riscontro dell'utente su pairing, All set "
                 "e Connected. Tengo ancora aperta la verifica degli hook reali e della "
                 "consegna fisica. Non trasformo una modifica sorgente in una prova riuscita."),
                ("Recupero della lease MCP", "Rinnovo ogni dieci secondi; dopo un guasto "
                 "di trasporto riprovo a 1, 3, 10 e poi 30 secondi. Mantengo identita' "
                 "ed errore fino alla risposta autenticata. Errori di identita' o auth "
                 "restano terminali; non ripeto hook o pairing automaticamente."),
            )),
        }
    if identity != DocumentId.Connection:
        raise ValueError("Unknown flow document identity: " + str(identity))
    return {
        1: PageSpec("Cody / Atlante del collegamento Codex", "Associo una volta, recupero il PC salvato e seguo il lavoro reale.", common_cover),
        2: PageSpec("Il percorso che uso adesso", "Screen 12 -> 13 -> 14 -> 15 -> attivita' reale; senza scelta manuale della chat.", (
            ("12 / Codice solo quando serve", "Avvio il pairing di un nuovo dispositivo "
             "con LAN utilizzabile. Se e' gia' associato, chiedo prima se conservare o "
             "sostituire il PC. Una disconnessione temporanea non genera un codice."),
            ("13 / Cody ha salvato il link", "All set segue il commit e l'invio ACTIVE. "
             "Il PC ha gia' salvato il candidato. Let's go cerca subito: ho eliminato la "
             "vecchia pausa Idle di dieci secondi."),
            ("14 -> 15 / Cerco il PC ricordato", "Accetto solo identita' salvata e "
             "risposta autenticata. Con Connected confermo CCP. Tengo disponibile "
             "Setup con una pressione sul personaggio da 1,5 secondi."),
            ("15 -> attivita' / Prompt reale", "Prima del primo evento mostro Waiting "
             "for Codex. Gli hook autorizzati portano automaticamente la chat "
             "dell'ultimo prompt. Sul dispositivo non faccio scegliere una chat."),
        )),
        3: PageSpec("Prima del collegamento", "Screen 09, 40 e 42 / verifico rete locale e disponibilita' del plugin.", (
            ("Cody / Wi-Fi e IP locale", "Completo il provisioning dal telefono quando "
             "la rete manca davvero. Con credenziali salvate e senza IP attendo il "
             "recupero o apro Network setup. Tengo separati assenza ed errore di lettura."),
            ("PC / CCP disponibile", "Installo il marketplace locale sul PC Windows "
             "che voglio associare. Il broker richiede Python e raggiungibilita' "
             "UDP sulla rete privata."),
            ("Attivita' / Hook autorizzati", "Dopo installazione e attivazione rivedo "
             "gli hook in Codex, con /hooks nella CLI locale. Uso orchestrazione "
             "locale e aspetto un prompt reale prima di mostrare lavoro."),
            ("Internet / Dipendenza distinta", "Per Cody e CCP uso la LAN, senza "
             "un controllo WAN. Codex puo' perdere il servizio online mentre "
             "il link locale rimane raggiungibile: rappresento i due fatti separati."),
        )),
        4: PageSpec("Leggo il codice attuale di Cody", "Screen 12 / attesa visibile e durata limitata del tentativo.", (
            ("Preparing secure code", "Durante la preparazione mostro Please wait "
             "e About 30 seconds. E' una stima ricavata dalla precedente osservazione, "
             "non una barra di progresso misurato o un tempo garantito."),
            ("Sei caratteri leggibili insieme", "Leggo il codice sul dispositivo "
             "e lo inserisco in Codex. Uso il font grande del titolo e lo mostro "
             "intero. Non lo riproduco nei documenti o nei log."),
            ("Tre minuti sul display", "Dal codice pronto applico 180 secondi "
             "di timeout dedicato. Il touch rinnova l'inattivita'; uscendo ripristino "
             "la regola ordinaria. Il protocollo mantiene una finestra di cinque minuti."),
            ("Recupero e sostituzione", "Per un link salvato uso Keep pairing "
             "o Retry. Apro un nuovo codice solo dopo Pair again esplicito. "
             "Wi-Fi o CCP momentaneamente assenti non richiedono altro commissioning."),
        )),
        5: PageSpec("Associo dal PC giusto", "Prima scopro Cody; poi uso solo il codice visibile sul dispositivo.", (
            ("Chiedo a Codex di collegarlo", "Con CCP attivo chiedo di scoprire "
             "Cody e associarlo con il codice attuale. Uso il dispositivo fisico "
             "come fonte, senza recuperare vecchi codici da immagini o documenti."),
            ("Trovo un solo dispositivo", "Il codice deve corrispondere a una "
             "sola offerta corrente. Con nessuna o piu' corrispondenze tengo "
             "l'errore esplicito. Non scelgo chi risponde per primo."),
            ("Aspetto il risultato", "Lascio aperta la schermata del codice. "
             "Una richiesta ammessa non e' ancora pairing concluso: attendo "
             "salvataggio e conferma su entrambi i lati."),
            ("Poi invio lavoro normale", "Seguo automaticamente la chat "
             "dell'ultimo prompt reale. Se vedo Waiting for Codex controllo "
             "hook ed esecuzione locale; cambiare pairing non crea eventi."),
        )),
        6: PageSpec("All set conferma il pairing", "Screen 13 / distinguo associazione salvata e presenza attuale del PC.", (
            ("Salvo candidati separati", "Prima di PAIR il PC salva il candidato "
             "DPAPI, mantenendo l'attivo. Su Cody PAIR scrive ccp_pending e PAIRED "
             "lo conferma. La ricezione di PAIR non sostituisce il vecchio link."),
            ("Confermo il commit reale", "CONFIRM permette a Cody di promuovere, "
             "rileggere e chiudere il journal. Mostro All set dopo l'invio ACTIVE. "
             "Il PC promuove solo con ACTIVE o COMMITTED autenticati."),
            ("Let's go ha un esito diretto", "Avvio subito la ricerca del "
             "PC salvato. Ho rimosso il giro intermedio di dieci secondi in Idle. "
             "Procedo verso Looking for PC e Connected."),
            ("Conferma finale persa", "Al riavvio cerco con il candidato. READY "
             "fresco ne permette il commit; COMMITTED prova al PC che Cody lo ha "
             "salvato. Ripeto l'ACK dopo ogni READY valido; FIND da solo non promuove."),
        )),
        7: PageSpec("Conservo o sostituisco: prima chiedo", "Screen 41 -> variante di conferma 40 / Already paired.", PAIRING),
        8: PageSpec("Cerco ancora lo stesso PC", "Screen 14 / PC assente e pairing assente sono due stati diversi.", RECOVERY),
        10: PageSpec("Connected e attivita' sono fatti diversi", "Screen 15 / aspetto un contesto autentico, senza inventare lavoro.", CONTEXT),
        11: PageSpec("Dopo interruzioni e standby riprendo", "Screen 14, 15, 32, 35A e 35B / recupero la presenza autentica.", RECOVERY),
        12: PageSpec("Riprovo l'operazione che e' fallita", "Screen 08, 12, 14 e 40 / Retry non cambia il PC in silenzio.", (
            ("Nessun pairing; codice scaduto", "Con Retry esplicito posso "
             "preparare un nuovo codice sulla LAN disponibile. Uso il codice "
             "del tentativo corrente e aspetto la conferma completa."),
            ("Pairing salvato; CCP assente", "Riprendo la ricerca del PC "
             "ricordato. IP mancante, standby o invio fallito conservano "
             "associazione e causa reale fino al recupero."),
            ("Voglio proprio un altro PC", "Apro Setup, Connect Codex e "
             "confermo Pair again su Already paired. Quella scelta esplicita "
             "autorizza un nuovo codice quando ho gia' un'associazione."),
            ("Record salvato non valido", "Dimensione, tipo, versione, CRC "
             "o lettura falliti restano errori. Non sostituisco il record "
             "con un default e non cancello impostazioni di nascosto."),
        )),
        13: PageSpec("Chi decide che cosa?", "Tengo distinti impostazioni, trasporto, chat e presentazione.", (
            ("Io / Scelte esplicite", "Scelgo se associare, conservare il "
             "PC o sostituirlo. Apro Network setup per la rete e Options "
             "per le preferenze. Un Retry ordinario non autorizza la cancellazione."),
            ("Cody / Stato canonico", "Gli owner pubblicano stati, timestamp "
             "ed errori reali. Il renderer li consuma. Conservo il pairing "
             "in NVS; un gesto non inventa la salute della connessione."),
            ("CCP / Identita' e chat", "Affido al broker l'associazione "
             "protetta e gli hook autorizzati. L'ultimo prompt seleziona "
             "il contesto; dispositivo e modello non devono sceglierlo a mano."),
            ("Io / Prove circoscritte", "Una build non prova il touch; "
             "READY non prova gli hook; un hook ricevuto non prova pixel "
             "o audio. Registro ogni risultato con il suo limite concreto."),
        )),
    }


# identity: Document that receives the dated trailing revision.
# changed: Physical main-page numbers replaced in this document.
# Returns new addendum recipes followed by the mandatory final errata.
def appendix_revisions(identity, changed):
    compact = identity == DocumentId.Quickstart
    sections = (
        ("Primo avvio / Dispositivo gia' configurato", "Senza Wi-Fi salvato apro 04-09. "
         "Con credenziali ma senza IP uso 42. Con LAN e senza pairing offro 40. "
         "Con un record valido riprendo 14-15, senza verificare Internet."),
        ("Setup / Consenso alla sostituzione", "Apro 41 tenendo premuto il personaggio "
         "per 1,5 secondi. Con un pairing salvato mostro Already paired: Keep pairing, "
         "Pair again oppure Forget. Retry riprende la ricerca senza codice nuovo."),
        ("Pairing / Presenza / Attivita'", "Con 13 confermo il pairing. Let's go cerca "
         "subito il PC. Su 15 confermo CCP e attendo il prompt reale. Gli hook "
         "seguono la sua chat automaticamente: ho eliminato la scelta manuale."),
        ("Options / Scelte locali", "Apro Make it yours da Options e riutilizzo "
         "Settings e Skin. Torno al chiamante corretto. Tengo separati preferenze, "
         "sostituzione del PC e qualsiasi cancellazione esplicita."),
    )
    pages = [PageSpec("Addendum / pairing conservato", "Registro della decisione / " + DATE + " / screen 04-16, 35B, 36 e 40-42.", sections)]
    recovery_copy = RECOVERY
    if compact:
        recovery_copy = (
            ("LAN attiva; Internet giu'", "Uso ancora il link locale con CCP. "
             "Codex puo' perdere i servizi online separatamente. Tengo "
             "questa situazione distinta da un pairing fallito."),
            ("LAN o PC assenti", "Conservo le identita', attendo e riprovo "
             "sulla rete locale. Posso tenere premuto Cody e aprire Setup. "
             "Per Retry ordinario non serve un altro codice."),
            ("Standby o aggiornamento", "Conservo il pairing e recupero "
             "la presenza autenticata. Il normale flash preserva NVS; "
             "cancellazione o storage invalido richiedono un'altra decisione."),
            ("Connected senza attivita'", "Controllo autorizzazione degli hook "
             "ed esecuzione locale, poi invio un prompt reale. Non fabbrico "
             "eventi o record di fiducia per far sembrare tutto a posto."),
        )
    pages.append(PageSpec("Addendum / casi di recupero", "Screen 14-16, 18U, 32, 35A/35B e 42 / conservo l'identita' salvata.", recovery_copy))
    pages.append(PageSpec("Addendum / conferma e reset", "Screen 12, 13, 40 e 41 / conservo rete, skin, preferenze e log.", TRANSACTION))
    if compact:
        errata = (
            ("Pagine originali " + ", ".join(str(page) for page in sorted(changed)), "Ho "
             "riscritto fronte e retro con installazione locale, hook e screen "
             "12-15/41. Ho superato il concetto ZIP e la scelta manuale della chat."),
            ("Screen 12-15, 40 e 41", "Let's go cerca subito. Already paired "
             "offre Keep pairing, Pair again e Forget. Retry recupera il PC salvato; "
             "Cancel chiude il candidato. Un normale aggiornamento conserva il link."),
            ("Buco di analisi iniziale", "Avevo confuso commissioning, "
             "riconnessione e attivita' e omesso il consenso alla sostituzione. "
             "Ho corretto il flow: qui il buco lo chiudo, non lo nascondo."),
            ("Storico ed evidenza", "Conservo verbatim addenda ed errata "
             "precedenti. Le pagine attuali superano le istruzioni in conflitto. "
             "Tengo separate modifiche scritte e prove fisiche ancora da eseguire."),
        )
    else:
        errata = (
            ("Pagine originali " + ", ".join(str(page) for page in sorted(changed)), "Ho "
             "integrato il flow corrente nelle pagine principali, conservando quelle "
             "non coinvolte e tutti gli addenda/errata precedenti. Riferisco il percorso "
             "agli screen ID anche quando cambia la paginazione."),
            ("Screen 10-16, 32, 35B e 40-42", "Options salva quattro preferenze; Already "
             "paired offre Keep pairing, Pair again e Forget. Separo Retry, Cancel e reset. "
             "Uso journal separati e COMMITTED per il commit reale. Let's go cerca subito."),
            ("Buco di analisi iniziale e responsabilita'", "Avevo mescolato commissioning, "
             "recupero e attivita', senza chiedere conferma della sostituzione. Ho "
             "esplicitato queste decisioni. CCP segue il prompt autentico; ho ritirato "
             "la vecchia scelta manuale della chat."),
            ("Decisione UI ed evidenza", "Conservo il linguaggio approvato: gradienti, "
             "icone, coppia orizzontale o capsula singola e pulsanti senza teletype. "
             "Con questa revisione aggiorno sorgenti e documenti; elenco separatamente "
             "le prove di dispositivo, hook e simulatore ancora da completare."),
        )
    pages.append(PageSpec("Errata and corrections", "Data revisione: " + DATE + " / " + REVISION, errata))
    return pages


# identity: Stable document identity from the requested set.
# staging: Owned repository artifact directory for backups and generated PDFs.
# Raises OSError or ValueError for unexpected source topology or duplicate revisions.
def revise(identity, staging):
    document = DOCUMENTS[identity]
    source = ROOT / "docs/Cody" / document.filename
    original_bytes = source.read_bytes()
    reader = PdfReader(BytesIO(original_bytes))
    if len(reader.pages) != document.original_pages:
        raise ValueError("Source page count changed: " + document.filename)
    if any(REVISION in (page.extract_text() or "") for page in reader.pages):
        raise ValueError("Revision already exists: " + document.filename)
    changes = main_revisions(identity)
    if any(number < 1 or number > document.main_pages for number in changes):
        raise ValueError("A replacement would overwrite dated history: " + document.filename)
    backup = staging / (source.stem + "-original.pdf")
    if backup.exists():
        # A failed draft can be authored again while its unchanged source backup remains protected.
        if backup.read_bytes() != original_bytes:
            raise ValueError("Preserved original differs from the current source: " + str(backup))
    else:
        backup.write_bytes(original_bytes)
    writer = PdfWriter()
    for number, page in enumerate(reader.pages, 1):
        dimensions = (float(page.mediabox.width), float(page.mediabox.height))
        writer.add_page(authored_page(changes[number], dimensions, document.label, number)
                        if number in changes else page)
    dimensions = (float(reader.pages[0].mediabox.width), float(reader.pages[0].mediabox.height))
    additions = appendix_revisions(identity, changes)
    for spec in additions:
        writer.add_page(authored_page(spec, dimensions, document.label, len(writer.pages) + 1))
    writer.add_outline_item("Atlante corrente", 0)
    writer.add_outline_item("Storico: addenda ed errata", document.main_pages)
    writer.add_outline_item("Revisione pairing conservato - " + DATE, document.original_pages)
    writer.add_outline_item("Errata and corrections - " + DATE, len(writer.pages) - 1)
    writer.add_metadata({"/Title": "Cody / " + document.label.title(),
                         "/Author": "Umberto Giacobbi", "/Contact": "hello@umbertogiacobbi.biz", "/Subject": REVISION,
                         "/ModDate": "D:20261010000000+07'00'"})
    destination = staging / source.name
    with destination.open("wb") as output:
        writer.write(output)
    item = {"identity": identity.value, "file": source.name, "original_pages": len(reader.pages),
            "main_pages": document.main_pages, "changed_pages": sorted(changes),
            "added_pages": len(additions), "final_pages": len(writer.pages),
            "original_sha256": sha256(original_bytes).hexdigest(),
            "revised_sha256": sha256(destination.read_bytes()).hexdigest()}
    print(source.name + ": main pages revised; dated history retained; " + str(len(writer.pages)) + " pages")
    return source, destination, item


# argv: Literal authoring arguments forwarded by scripts/device.ps1.
# Raises OSError or ValueError when any requested document cannot be fully authored.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staging", type=Path, default=ROOT / "artifacts/docs" / REVISION.lower())
    args = parser.parse_args(argv)
    staging = args.staging.resolve()
    if not staging.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF staging must remain under repository artifacts")
    staging.mkdir(parents=True, exist_ok=True)
    register_fonts(Path("C:/Windows/Fonts"))
    prepared = [revise(identity, staging) for identity in DOCUMENTS]
    # Stage every requested document before publishing any of the source PDFs.
    for source, destination, _ in prepared:
        source.write_bytes(destination.read_bytes())
    manifest = {"revision": REVISION, "date": "2026-10-10", "documents": [item for _, _, item in prepared]}
    (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
