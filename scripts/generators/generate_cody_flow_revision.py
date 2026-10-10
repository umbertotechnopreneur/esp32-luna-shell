# VBWR B
#
# Project: ESP32 Luna Shell
# Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
# Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
#
# VibeWare: Human intent, AI execution, and plenty of tokens
# Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
#
# Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
# Human guidance: Umberto Giacobbi; requested VibeWare branding.
#
# Copyright (c) 2026 Umberto Giacobbi
#
# VBWR E

"""Revise the five Cody flow PDFs in place while preserving their dated history."""

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
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
REVISION = "CODY-NOTIFICATIONS-20261010"
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
    DocumentId.Navigation: DocumentSpec("cody-navigation-atlas-v1.pdf", "NAVIGATION ATLAS", 42, 12),
    DocumentId.Planning: DocumentSpec("cody-planning-atlas-v1.pdf", "PLANNING ATLAS", 42, 22),
    DocumentId.Quickstart: DocumentSpec("cody-ccp-pairing-quickstart-a5-v1.pdf", "CCP QUICK START", 17, 2),
    DocumentId.Service: DocumentSpec("cody-ccp-service-atlas-v1.pdf", "CCP SERVICE ATLAS", 24, 10),
    DocumentId.Connection: DocumentSpec("cody-codex-connection-atlas-v1.pdf", "CODEX CONNECTION ATLAS", 28, 14),
}

COVER = (
    ("Conservo il PC, poi ricevo il lavoro", "Tengo distinti pairing, presenza LAN e attivita' "
     "dell'ultimo prompt reale. L'estensione EVENT porta avvisi e domande esplicite; non "
     "inventa una chat quando manca un hook. Il contesto dichiarato e' un'etichetta."),
    ("Decido soltanto dopo aver letto", "Da Codex needs you apro due o tre scelte. La selezione "
     "apre un riepilogo con Send e Back. Mostro Reply sent solo dopo l'ACK del destinatario. "
     "Una domanda dura 180 secondi; la scadenza non sceglie al mio posto."),
    ("Le PR restano in sola lettura", "Conservo la decisione A03: gli screen 21-24 riportano "
     "stati e nomi dei check falliti. Il 25 e' ritirato; diagnosi e azioni PR restano sul PC. "
     "Un'approvazione nativa rimanda a On your PC, senza Allow o Deny su Cody."),
    ("Come leggo questa revisione", "Ho aggiornato pagine principali e limiti del flow. "
     "Conservo gli addenda precedenti e aggiungo errata datate. Distinguo il contratto "
     "nei sorgenti dalle prove host e fisiche; registro gli esiti correnti in done.md."),
)

NOTICES = (
    ("26 / Codex needs you", "Ricevo una domanda dal chiamante MCP esplicito; per gli "
     "avvisi mostro From Codex. Leggo testo inglese ASCII. Il contesto e' dichiarato dal "
     "chiamante, senza attribuirgli l'identita' verificata della chat o degli hook."),
    ("26 -> 27 / Apro la domanda", "Con Open leggo le due o tre risposte e le rispettive "
     "descrizioni. Conservo una sola domanda aperta per dispositivo. Attivita', notifiche "
     "successive e indicatori batteria non la sostituiscono."),
    ("Approvazioni / On your PC", "Un PermissionRequest puo' richiamare la mia "
     "attenzione, ma la decisione nativa resta nel computer. Non aggiungo Allow, Deny "
     "o un'autorizzazione generica ricavata da un testo incompleto."),
    ("Notifica non significa risposta", "Una notifica non offre scelte e non produce "
     "REPLY. Riservo Reply sent alle risposte consegnate. Per le PR ammetto soltanto "
     "gli stati e la lista read-only previsti da A03; nessuna azione operativa."),
)

QUESTIONS = (
    ("26 / Codex needs you e Open", "Mostro il contesto dichiarato, il titolo e l'avviso. "
     "La richiesta ha un ticket distinto e dura 180 secondi. Aprire o chiudere una "
     "vista non allunga la scadenza e non seleziona una risposta."),
    ("27 / Choose a reply", "Uso due card affiancate; con tre aggiungo quella "
     "centrata sotto. Ogni scelta ha un ID stabile e un significato proprio. "
     "Back torna a 26 e conserva la domanda, senza inviare."),
    ("27 / Review choice", "La scelta apre un riepilogo con descrizione "
     "e capsula Send. Back mi riporta alle opzioni. Dopo Send tengo la scelta "
     "ferma e mostro Sending reply. Non aggiungo un quarto pulsante sotto tre card."),
    ("31 / Reply sent dopo consegna", "L'ACK UDP received lascia Waiting for Codex: "
     "CCP ha registrato la scelta. Solo con delivered mostro Reply sent e "
     "Delivered to Codex; Done riprende il contesto precedente 17, 18 o 16."),
)

DELIVERY = (
    ("Ticket e capability privati", "Il chiamante MCP crea un ticket di 128 bit "
     "con capability privata. Poll e cancel richiedono entrambi i valori. "
     "La capability resta sul PC e non viaggia sulla LAN."),
    ("received non e' delivered", "Il poll restituisce scelta e answer_id. "
     "Un poll successivo con acknowledge_answer_id uguale prova la ricezione "
     "del chiamante: solo allora passo a delivered e allo screen 31."),
    ("Invio e risposta ripetuta", "Dopo pending entro in Sending locale, senza "
     "cambiare scelta. Received non e' ancora delivered. Ripetere la stessa "
     "risposta e' idempotente; una diversa non sovrascrive la prima."),
    ("Scadenza e consegna incerta", "I 180 secondi valgono anche in received. "
     "Se ho gia' ammesso Send mostro Reply unconfirmed: puo' mancare solo l'ACK. "
     "Prima di Send uso Request expired. Nessun Retry o ACK tardivo riapre la domanda."),
)

PR_STATUS = (
    ("21 / PR pending", "Osservo soltanto il PR indicato. Conservo l'esatto head "
     "dello snapshot esplicito fornito da Codex. Non uso Stop come prova dei check "
     "e non offro merge, approve, close, rerun o comment."),
    ("22 / Checks running e Checks passed", "Mostro numeri solo da un elenco "
     "completo e attuale sullo stesso head. Checks passed non ha pulsanti: "
     "attendo la fine della narrativa e tre secondi, poi torno al lavoro."),
    ("23 -> 24 / View failures", "L'unica azione apre i nomi reali dei check falliti: "
     "al massimo sei, fino a 48 caratteri ASCII ciascuno. Un input non rappresentabile "
     "viene rifiutato esplicitamente; non taglio nomi e non invento totali."),
    ("24 sola lettura / 25 ritirato", "Mostro tre nomi per pagina, fino a due "
     "righe ciascuno; scorro verticalmente e tengo Back laterale. Non offro "
     "View log o righe selezionabili. Log, diagnosi e correzioni restano sul PC."),
)

RECOVERY = (
    ("Domanda protetta", "Una domanda aperta resta davanti agli aggiornamenti "
     "ordinari. Conservo selezione e percorso. Batteria, charging o un avviso "
     "successivo non fanno sparire una decisione che devo ancora leggere."),
    ("32 / Offline", "Conservo il contenuto e cerco il PC salvato. La scadenza "
     "continua: tornare sulla LAN non rinnova i 180 secondi. Timeout, Cancel "
     "e disconnessione non scelgono un'opzione e non confermano consegna."),
    ("Scadenza e annullamento", "Prima di Send mostro Request expired; dopo "
     "Send, Reply unconfirmed e Check on your PC. Uso Request cancelled o "
     "Reply rejected per gli altri esiti, con Done. Non riapro stati terminali."),
    ("35A / 35B e nuovi avvisi", "Mantengo Wi-Fi durante lo standby di Cody "
     "associato. Accodo il wake per un avviso autenticato al worker power, "
     "preservando AFE e wake word. Registro a parte gli esiti del risveglio fisico."),
)

SCOPE = (
    ("MCP esplicito e hook reali", "Tengo separati il ticket della richiesta e "
     "la chat seguita dagli hook. context e' un'etichetta del chiamante; non "
     "fabbrico UserPromptSubmit, identita' o testo per far sembrare attiva una chat."),
    ("Approvazioni sul computer", "PermissionRequest produce al massimo "
     "un avviso On your PC. Non autorizzo strumenti nativi da Cody e non "
     "converto un avviso parziale in un generico consenso."),
    ("PR e diagnosi sul PC", "Applico A03 del 4 ottobre: 21-24 sola lettura, "
     "25 ritirato. Errori, log e stack trace restano sul PC; le domande 26/27 "
     "non sono un percorso alternativo per gestire una PR."),
    ("Voce e prove distinte", "28 Recording, 29 Transcribing e 30 Review "
     "transcript restano un percorso futuro. Tengo separati sorgenti, suite "
     "host e prova touch sul dispositivo; gli esiti correnti sono in done.md."),
)

WIRE = (
    ("EVENT autenticato", "Aggiungo events=1 a FIND con activity=1. Dopo READY "
     "ricevo uno snapshot EVENT legato al nonce corrente. EVENT ammette 1280 "
     "byte; gli altri pacchetti restano entro 512. Non frammento contenuti."),
    ("Contenuto bounded ASCII", "Uso context fino a 24, title fino a 24 e "
     "body fino a 63 caratteri stampabili. Scelte: ID univoco e label fino a 12, "
     "detail fino a 63. Rifiuto campi o combinazioni non rappresentabili."),
    ("Tre tempi diversi", "La domanda dura 180 secondi e i duplicati non "
     "la prolungano. Presenza sul device e attivita' degli hook mantengono "
     "i propri tempi di 8 e 120 secondi: non li uso come risposta o consenso."),
    ("Un owner, una fonte canonica", "Il trasporto pubblica stato, errore "
     "e freschezza nello snapshot canonico. La UI non apre socket. Il simulatore "
     "usa lo stesso protocollo ma resta SIMULATED, con opt-in esplicito."),
)

RESTART = (
    "Riavvio esplicito / dati conservati",
    "Scrivo 'resetta il device codice 043533477' nel messaggio corrente: nove cifre, zero compreso. "
    "E' un controllo anti-equivoco, non una password; non lo completo dai documenti. "
    "Uso il peer paired LAN, dati conservati. ACK = accepted; boot = nuova presenza.",
)

FACTORY_RESET = (
    "Factory reset / primo setup",
    "Con 'formatta cody 043533477' elimino preferenze, Wi-Fi e pairing; conservo "
    "firmware, asset e SD. ACK indica ammissione. Riavvio solo dopo cancellazione "
    "verificata; poi riparto dal setup Wi-Fi. Un errore resta visibile e non avvia il reboot.",
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
    references = ROOT / "docs/cody/ui-proposals/20261009"
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
              "Back laterale e corde del cerchio. Sul 27 la scelta apre un riepilogo: uso "
              "Send in capsula e Back per rivedere le opzioni. Queste immagini sono i "
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


# identity: Stable requested document identity, independent of page order.
# Returns replacements only for main pages affected by the new interaction contract.
# Raises ValueError for an identity outside the explicitly requested document set.
def main_revisions(identity):
    if identity == DocumentId.Navigation:
        return {
            1: PageSpec("Cody / Atlante della navigazione", "Notifiche, domande e PR in sola lettura; conservo il pairing e lo storico.", COVER),
            5: PageSpec("Chi porta l'attivita', chi invia una domanda", "Screen 15-20 e 26 / hook reali e richieste MCP esplicite hanno identita' separate.", NOTICES),
            6: PageSpec("L'attivita' non chiude una domanda", "Screen 17-20, 26 e 27 / proteggo il contenuto ancora da decidere.", RECOVERY),
            7: PageSpec("Le pull request restano in sola lettura", "Screen 21-24 / applico A03 del 4 ottobre; il 25 e' ritirato.", PR_STATUS),
            8: PageSpec("Leggo, scelgo e confermo", "Screen 26 -> 27 -> riepilogo 27 -> 31 / aspetto l'ACK del destinatario.", QUESTIONS),
            9: PageSpec("Il percorso voce resta futuro", "Screen 28-30 / non confondo la risposta a card con registrazione o trascrizione.", (
                ("27 -> 28 / Recording - futuro", "Conservo il percorso pensato per push to talk. "
                 "L'ingresso voce e la cattura non sono parte di questa estensione. Le card "
                 "e il riepilogo esplicito coprono la decisione corrente."),
                ("28 -> 29 / Transcribing - futuro", "Prevedo uno stato distinto dopo il rilascio. "
                 "Cancel deve tornare alla domanda senza scegliere per me. Non dichiaro "
                 "una trascrizione disponibile per il solo fatto che il microfono funziona."),
                ("30 / Review transcript - futuro", "Leggo prima di Send, posso registrare di "
                 "nuovo o tornare alle opzioni con la bozza conservata. Errori di cattura "
                 "o trascrizione devono restare visibili, senza Reply sent falso."),
                ("31 / La stessa regola di consegna", "La conferma finale richiede l'ACK "
                 "del destinatario. Il trasporto attuale delle scelte non implementa "
                 "automaticamente il futuro percorso voce. Tengo i due perimetri separati."),
            )),
            10: PageSpec("Offline, scadenza e risveglio", "Screen 26/27, 32 e 35A/35B / nessuna risposta automatica.", RECOVERY),
        }
    if identity == DocumentId.Planning:
        return {
            1: PageSpec("Cody / Atlante di progettazione", "Domande esplicite, notifiche e confini A03; aggiorno anche le pagine originali.", COVER),
            2: PageSpec("Inventario e perimetro degli screen", "Uso gli screen ID del documento; le identita' enum del firmware sono separate.", (
                ("01-16 e 40-42 / Avvio e collegamento", "Conservo provisioning LAN, Options, "
                 "Skin e pairing con consenso alla sostituzione. Let's go cerca subito il "
                 "PC salvato. Niente scelta manuale della chat."),
                ("17-20 e 18U / Attivita'", "Seguo il prompt reale attraverso hook autorizzati. "
                 "Stop significa risposta pronta, senza certificare test o PR. Una domanda "
                 "aperta ha precedenza su questi aggiornamenti ordinari."),
                ("21-27 e 31 / Stato e decisioni", "21-24 sono PR in sola lettura; 25 ritirato. "
                 "26 annuncia avvisi/domande, 27 contiene scelte e riepilogo con Send. "
                 "31 richiede l'ACK del destinatario; 32 conserva il percorso offline."),
                ("28-30 e 33-39 / Confini", "28-30 restano voce futura. Conservo 33/34 come "
                 "indicatori, 35A/35B per sleep/wake e 36-39 per preferenze e sviluppo. "
                 "Non dichiaro questa nuova estensione verificata fisicamente."),
            )),
            11: PageSpec("Osservo una PR senza gestirla", "Screen 21-23 / nessuna azione di merge, approve, close, rerun o comment.", (
                ("21 / PR pending", "PR pending", "Mostro lo stato del PR indicato senza "
                 "azioni. La fonte e' uno snapshot esplicito riferito all'esatto head."),
                ("22 / Checks running", "Checks running", "Mostro conteggi solo da una "
                 "lista completa e corrente. Checks passed resta tre secondi dopo "
                 "la narrativa, poi torno al lavoro senza un pulsante Done."),
                ("23 / Checks failed", "View failures", "L'unica azione apre 24, la lista "
                 "di nomi reali. Ogni diagnosi o correzione continua sul PC."),
            ), Layout.Screens, "A03 rimane vincolante. Un dato assente o incompleto non diventa "
               "un conteggio inventato; un evento Stop non equivale a check passati."),
            12: PageSpec("Errori sul PC; domande sul Cody", "Screen 24, 25 ritirato e 26 / correggo le illustrazioni precedenti in conflitto.", (
                PR_STATUS[2], PR_STATUS[3], NOTICES[0], NOTICES[2],
            )),
            13: PageSpec("Due o tre scelte, poi Send", "Screen 26 e due varianti del 27 / gradienti, icone e conferma reversibile.", (
                ("26 / Codex needs you", "Open", "Leggo l'avviso e apro la richiesta. Una "
                 "domanda resta protetta dagli aggiornamenti ordinari e dura 180 secondi."),
                ("27 / Choose a reply", "2 / 3 choices", "Due card sono affiancate; la "
                 "terza va sotto al centro. Ogni ID resta stabile anche se cambia l'ordine."),
                ("27 / Review choice", "Send", "Leggo la descrizione e confermo con Send. "
                 "Back torna alle scelte; selezionare una card da solo non invia."),
            ), Layout.Screens, "Il vecchio elenco verticale e' superato. Mostro i pulsanti completi "
               "dopo il testo principale; niente teletype nelle label. 31 arriva solo dopo delivered."),
            14: PageSpec("Quando posso dire Reply sent?", "Screen 31 / registro ricevuto, consegna confermata, scadenza e annullamento separati.", DELIVERY),
            16: PageSpec("Ritrovo il percorso al risveglio", "Screen 26/27, 32 e 35A/35B / standby e scadenza restano fatti diversi.", RECOVERY),
            21: PageSpec("Un linguaggio UI, anche per decidere", "Riferimenti approvati / card 2 o 2+1; riepilogo separato con capsula Send.", (), Layout.Actions),
            22: PageSpec("Dove finisce il compito di Cody", "A03 resta in vigore / approvazioni native e diagnosi non passano al dispositivo.", SCOPE),
        }
    if identity == DocumentId.Quickstart:
        return {
            1: PageSpec("Installo CCP sul mio PC", "Windows / plugin locale / Python disponibile come comando python.", (
                ("1 / Registro la sorgente locale", "Dalla radice del repository eseguo "
                 "codex plugin marketplace add . per registrare il catalogo incluso. "
                 "Installo e abilito CCP - Cody's Codex Plugin dal catalogo Cody local plugins."),
                ("2 / Collego il dispositivo", "Uso il codice visibile su Cody. Se e' "
                 "gia' associato scelgo Keep pairing; Pair again richiede consenso. "
                 "Un'interruzione della LAN non richiede un codice nuovo."),
                ("3 / Attivita' oppure richiesta esplicita", "Gli hook autorizzati "
                 "seguono l'ultimo prompt reale con orchestrazione locale. Posso "
                 "inviare avvisi/domande con MCP senza inventare hook o identita' di chat."),
                ("4 / Tengo il perimetro chiaro", "Per CCP bastano Wi-Fi e IP sulla LAN. "
                 "Le PR restano in sola lettura e le approvazioni native On your PC. "
                 "Tengo separate le prove dai sorgenti e registro gli esiti in done.md."),
            )),
            2: PageSpec("Leggo, scelgo, invio", "Screen 26 -> 27 -> 31 / una ricezione UDP non basta a dire consegnato.", QUESTIONS),
        }
    if identity == DocumentId.Service:
        return {
            1: PageSpec("CCP porta stati e richieste esplicite", "Presenza, attivita', ticket e consegna hanno prove diverse.", COVER),
            2: PageSpec("Hook e ticket hanno identita' separate", "CCP 0.6 / non uso context per inventare una chat seguita.", SCOPE),
            7: PageSpec("Estendo CCP1 con EVENT e REPLY", "Screen 26/27 e PR 21-24 / messaggi autenticati e contenuti rappresentabili.", WIRE),
            8: PageSpec("La ricevuta del broker non e' la consegna", "Screen 31 / ticket privato, poll del chiamante e ACK esplicito.", DELIVERY),
            9: PageSpec("Confini di contenuto e fiducia", "Decisioni limitate / niente capability sulla LAN o azioni PR nel wire.", (
                ("Conservo le protezioni del pairing", "Uso salt e sessione nuovi, prove HMAC "
                 "legate alle identita' e un solo match del codice. Il PC protegge le chiavi "
                 "con DPAPI; Cody conserva un record NVS versionato."),
                ("Autenticazione, non cifratura", "CCP1 non e' PAKE o TLS. PBKDF2 rende piu' "
                 "costosi i tentativi offline; il CRC NVS rileva corruzione, senza cifrare "
                 "la chiave. Anche il contenuto EVENT e' autenticato, non cifrato."),
                DELIVERY[0],
                ("Rete locale e revisione di produzione", "Il codice di pairing a sei caratteri non viaggia in chiaro "
                 "nei pacchetti CCP1; non e' una promessa sulla cronologia Codex. Tengo "
                 "aperta la revisione di guessing, replay/rate limit, key storage e reti ostili."),
            )),
            10: PageSpec("Rete, durata e limiti delle prove", "Domanda 180 secondi / presenza 8 / attivita' hook 120.", RECOVERY),
        }
    if identity == DocumentId.Connection:
        return {
            1: PageSpec("Cody / Dal collegamento alla decisione", "Conservo il PC; distinguo notifica, scelta e conferma di consegna.", COVER),
            10: PageSpec("Connected, poi avvisi e domande", "Screen 15, 26 e 27 / l'etichetta context non e' l'identita' della chat.", NOTICES),
            11: PageSpec("Offline non risponde per me", "Screen 26/27, 32 e 35A/35B / conservo il percorso e rispetto la scadenza.", RECOVERY),
            13: PageSpec("Chi conferma la risposta?", "Screen 31 / il destinatario MCP deve riconoscere la ricezione.", DELIVERY),
        }
    raise ValueError("Unknown flow document identity: " + str(identity))


# identity: Document that receives the dated trailing revision.
# changed: Physical main-page numbers replaced in this document.
# Returns four pages ending with the mandatory current revision errata.
def appendix_revisions(identity, changed):
    page_list = ", ".join(str(page) for page in sorted(changed))
    if identity == DocumentId.Quickstart:
        page_copy = "Ho aggiornato le due pagine iniziali: installazione locale, " \
            "richieste esplicite e flow 26/27/31. Conservo lo storico precedente."
    else:
        page_copy = "Ho aggiornato le pagine principali coinvolte, mantenendo " \
            "quelle estranee al flow e gli addenda precedenti. Uso gli screen ID."
    errata = (
        ("Pagine originali " + page_list, page_copy),
        ("Screen 21-27 e 31-32", "Ho applicato A03: PR in sola lettura e 25 ritirato. "
         "Ho rimosso View log e il vecchio elenco verticale. Ora 27 ha card e "
         "riepilogo Send; 31 richiede delivered dopo l'ACK del destinatario."),
        ("Buco di analisi iniziale", "Avevo lasciato ambigui ricevuta, consegna "
         "e scadenza, oltre a illustrazioni PR superate. Li separo con ticket, "
         "received/delivered e 180 secondi. Timeout e offline non scelgono risposte."),
        ("Storico, comandi e prove", "Ho separato riavvio e factory reset: il "
         "primo conserva i dati, il secondo cancella la configurazione per "
         "tornare al setup. ACK indica ammissione. Conservo storico ed errori "
         "reali; gli esiti host e fisici sono in done.md."),
    )
    return [
        PageSpec("Addendum / notifiche e decisioni", "Revisione " + DATE + " / screen 26, 27 e 31.", QUESTIONS),
        PageSpec("Addendum / PR solo in lettura", "A03 resta in vigore / screen 21-24; 25 ritirato.", PR_STATUS),
        PageSpec("Addendum / consegna e comandi", "Screen 31, 32 e nuovo setup / distinguo accettazione e risultato.",
                 (DELIVERY[1], DELIVERY[3], RESTART, FACTORY_RESET)),
        PageSpec("Errata and corrections", "Data revisione: " + DATE + " / " + REVISION, errata),
    ]

# staging: Existing artifact directory holding the previous draft manifest and original PDFs.
# Raises OSError for unavailable originals, current drafts, manifest or archive output.
# Raises ValueError when a current draft or preserved original differs from its recorded hash.
def redraft_originals(staging):
    manifest_bytes = (staging / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest.get("revision") != REVISION:
        raise ValueError("Redraft manifest belongs to a different revision")
    records = manifest.get("documents")
    if not isinstance(records, list) or len(records) != len(DOCUMENTS):
        raise ValueError("Redraft manifest must identify every requested document")
    by_name = {}
    for record in records:
        if not isinstance(record, dict) or record.get("file") in by_name:
            raise ValueError("Redraft manifest contains an invalid or duplicate document")
        by_name[record.get("file")] = record
    if set(by_name) != {document.filename for document in DOCUMENTS.values()}:
        raise ValueError("Redraft manifest document set differs from this revision")

    originals = {}
    prior_drafts = {}
    for identity, document in DOCUMENTS.items():
        record = by_name[document.filename]
        if record.get("identity") != identity.value:
            raise ValueError("Redraft document identity differs: " + document.filename)
        current = (ROOT / "docs/cody" / document.filename).read_bytes()
        original = (staging / (Path(document.filename).stem + "-original.pdf")).read_bytes()
        if sha256(current).hexdigest() != record.get("revised_sha256"):
            raise ValueError("Current PDF changed after the recorded draft: " + document.filename)
        if sha256(original).hexdigest() != record.get("original_sha256"):
            raise ValueError("Preserved original changed: " + document.filename)
        originals[identity] = original
        prior_drafts[document.filename] = current

    # Keep the prior editorial draft before rewriting; never roll back the published PDFs.
    archive = staging / "draft-history" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    archive.mkdir(parents=True, exist_ok=False)
    (archive / "manifest.json").write_bytes(manifest_bytes)
    for filename, draft in prior_drafts.items():
        (archive / filename).write_bytes(draft)
    return originals


# identity: Stable document identity from the requested set.
# staging: Owned repository artifact directory for backups and generated PDFs.
# original_bytes: Hash-verified preserved input for an explicit redraft, otherwise None.
# Raises OSError or ValueError for unexpected source topology or duplicate revisions.
def revise(identity, staging, original_bytes=None):
    document = DOCUMENTS[identity]
    source = ROOT / "docs/cody" / document.filename
    if original_bytes is None:
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
    writer.add_outline_item("Revisione notifiche e decisioni - " + DATE, document.original_pages)
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
    parser.add_argument("--redraft", action="store_true",
                        help="Replace only the recorded current draft, using its verified preserved originals")
    args = parser.parse_args(argv)
    staging = args.staging.resolve()
    if not staging.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("PDF staging must remain under repository artifacts")
    staging.mkdir(parents=True, exist_ok=True)
    originals = redraft_originals(staging) if args.redraft else {}
    register_fonts(Path("C:/Windows/Fonts"))
    prepared = [revise(identity, staging, originals.get(identity)) for identity in DOCUMENTS]
    # Stage every requested document before publishing any of the source PDFs.
    for source, destination, _ in prepared:
        source.write_bytes(destination.read_bytes())
    manifest = {"revision": REVISION, "date": "2026-10-10", "documents": [item for _, _, item in prepared]}
    (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
