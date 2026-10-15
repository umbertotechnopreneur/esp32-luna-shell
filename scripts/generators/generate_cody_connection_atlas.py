"""Author the concise Cody/CCP visual communication reference and dated register."""

from io import BytesIO
from pathlib import Path
import argparse
from hashlib import sha256
import json
import sys

import pymupdf
from PIL import Image
from pypdf import PdfReader, PdfWriter

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "common"))
from cody_pdf_revision_register import RevisionRecord, register_fonts
from cody_connection_atlas_visual import visual_pdf, visual_register

DEFAULT_OUTPUT = ROOT / "docs/cody/cody-ccp-connection-atlas-v1.pdf"
STAGING = ROOT / "artifacts/docs/cody-communication-atlas-20261012"
SOURCES = (
    "cody-ccp-connection-atlas-v1.pdf",
    "cody-ccp-service-atlas-v1.pdf",
    "cody-codex-connection-atlas-v1.pdf",
    "cody-ccp-pairing-quickstart-a5-v1.pdf",
)



# source_directory: Artifact location for protected historical PDF snapshots.
# Raises OSError if a source PDF or snapshot cannot be read or written.
def preserve_sources(source_directory):
    source_directory.mkdir(parents=True,exist_ok=True)
    manifest = []
    for name in SOURCES:
        backup = source_directory/name
        if not backup.exists():
            import subprocess
            backup.write_bytes(subprocess.run(["git","show","1d46253f:docs/cody/"+name],
                cwd=ROOT,capture_output=True,check=True).stdout)
        document = pymupdf.open(backup)
        manifest.append({"file":name,"pages":len(document),"sha256":sha256(backup.read_bytes()).hexdigest()})
        (source_directory/(name+".txt")).write_text("\n\n".join(
            f"ORIGINAL PAGE {index+1}\n"+page.get_text() for index,page in enumerate(document)),encoding="utf-8")
    (source_directory/"manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")


# No parameters; returns the historical dated changes and corrections.
def revision_history():
    approved = ROOT / "docs" / "cody" / "ui-proposals" / "20261009" / "selected-multi-action.png"
    return [
        RevisionRecord("2026-10-04", "Change", "A03: PRs remain read-only", ("Retain PR/check states and the indicated head, show only real failed-check names, and leave diagnosis/actions on the PC. Screen 25 is retired. Native approvals remain computer-owned; voice screens 28-30 are future work.",), "Service scope/security; Connection 9-14; screens 21-30", "Original design decision; not a physical capture."),
        RevisionRecord("2026-10-09", "Addendum", "Startup decisions and reversible Setup", ("The earlier analysis omitted saved Wi-Fi without Codex pairing and a reversible Setup entry. Add 40 / Yes-No, 41 / character hold and 42 / reconnect wait. No/Later causes no persistence or new CCP request. Saved pairing searches 14 and requires current authentication for 15.", "PWR/BOOT do not clear saved pairing. A factory erase remains separately authorized."), "Service 11-13; Connection 15-17; original screens 12-16, 40-42", "Source/static review only in this revision; firmware, host and physical verification remained pending."),
        RevisionRecord("2026-10-09", "Errata", "Setup recovery after standby", ("The legacy Welcome timeout path obscured reopening Setup. A resting Welcome tap release or eligible stationary hold can reopen 41. Wake time cannot count toward the hold until fresh acquisition and visible restoration; movement, gaps and rotation cancel it. Busy provisioning and pending cleanup are excluded.",), "Service 14-15; Connection 18-19; screens 04-06, 12, 35B, 41", "A 90-second capture showed LAN/DHCP, inactivity return and standby, but no Connect Codex press. Corrected physical behavior was still pending."),
        RevisionRecord("2026-10-09", "Change", "Approved action layout and Options entry", ("Two semantic gradient cards sit side by side; a third is centered below. A single action is an icon-left wide capsule. Back stays lateral, render/touch share bounds, and QR quiet zones are protected. Options reopens preferences and skin before returning to Setup.",), "Service 16-18; Connection 20-22; screens 05-15, 35B, 36, 40-42", "Approved design reference, not a device screenshot.", str(approved) if approved.exists() else None, "Approved multi-action reference / 09 October 2026."),
        RevisionRecord("2026-10-09", "Errata", "Malformed touch report interrupted rendering", ("The original analysis failed to distinguish an optional runtime touch error from renderer failure. A capture recorded Yes at 16497 ms, Connect Codex at 16498 ms, then fifteen FF report bytes and ACK=FF at 17112 ms. The expected ACK was AB; ESP_ERR_INVALID_RESPONSE stopped rendering.", "The owner preserves the original error. Only after startup, invalid acquisition may interrupt gestures while rendering continues, with queued reports discarded and a real release required. The FF hardware root cause was not established."), "Service 17-18; Connection 21-22; screens 12 and 35B", "Measured failure retained; corrected physical touch behavior remained pending."),
        RevisionRecord("2026-10-10", "Addendum", "Earlier CCP 0.3 activity proposal", ("Metadata-only hooks, shared local runtime, explicit manual session selection and nonce-bound ACTIVITY were the proposal recorded then. Screen 15 proved PC presence only; 18U made unavailable activity explicit. Two-second polling, eight-second presence and 120-second hook freshness were separate.", "Historical limitation: branch acquisition, notifications and voice were then future work. Manual chat selection was later retired in favor of the genuine latest prompt; later source revisions supersede that policy."), "Service 19-20; Connection 23-24; original Service 1-2, 7-8, 10 / Connection 10-11, 13", "User-reported Connected applied to earlier firmware/CCP 0.2, not proof of the new 0.3 activity proposal."),
        RevisionRecord("2026-10-10", "Change", "Retained pairing and two-sided commit", ("Separate commissioning, recovery and real chat activity. Already paired offers Keep pairing, Pair again and explicitly confirmed Forget. Retry keeps the same PC; Cancel abandons the candidate; Forget removes only Cody's CCP records. Preferences and Wi-Fi remain separate.", "PC DPAPI candidate and Cody ccp_pending preserve old active records. CONFIRM/fresh READY proves the peer; persisted ACTIVE/COMMITTED proves promotion. Repeat COMMITTED on each current READY to recover lost acknowledgement. Explicit replacement is required for an existing pending PC candidate."), "Service 21-24; Connection 25-28; screens 04-16, 32, 35B, 36, 40-42", "Source revision; paired Internet outages, restarts and wake behavior require their own device evidence."),
        RevisionRecord("2026-10-10", "Addendum", "Notifications, choices and recipient delivery", ("26 opens a bounded explicit ticket; 27 uses cards plus a separate Send review. The recipient must acknowledge answer_id before delivered / Reply sent. The 180-second deadline applies even to received; neither disconnection, timeout nor cancellation chooses a reply. Terminal states do not reopen.", "The recorded wire revision used activity=1/events=1 and a 1280-byte EVENT ceiling, versus 512-byte ordinary packets. Context/title/body and choice text are bounded printable ASCII; capabilities stay private on the PC. Later capability revisions must be read from current source, not this historical example."), "Service 25-28; Connection 29-32; screens 21-27, 31-32", "Source contract; original 24/24/63-character context/title/body and 12/63 label/detail limits are historical revision facts."),
        RevisionRecord("2026-10-10", "Errata", "Acknowledgement is admission, not reset completion", ("Restart retains configuration; factory reset removes preferences, Wi-Fi and pairing but preserves firmware, assets and SD. Destructive intent and its nine-digit anti-mistake confirmation must come from the current user message. Never complete it from a document. Signed ACK means accepted; new presence proves reboot, and verified erasure precedes factory-reset reboot.",), "Service 27-28; Connection 31-32; reset and factory-reset commands", "Real erase/boot outcomes remain distinct from authenticated command admission."),
        RevisionRecord("2026-10-10", "Change", "Current boot, setup and complete controls", ("Boot gains the title/subtitle above complete floppy art. Installation QR starts no radio, Bluetooth session QR exists only when ready, and local Wi-Fi/IP gates Continue. Code preparation estimates do not fabricate progress. All set searches immediately, removing the old Idle detour.", "Four local preference rows, stable skin IDs and readback-confirmed saves retain real errors. Controls appear together after narrative completion, without teletype labels. Historic artwork sizes and obsolete route sketches remain dated references, not the current contract."), "Service 29-32; Connection 33-36; boot 01-03, Wi-Fi 04-09/42, preferences 10/36/11, Codex 12-15/40/41", "Static source geometry and approved art only; this document revision executed no simulator or physical-device validation."),
        RevisionRecord("2026-10-10", "Change", "Consolidated service and connection atlas", ("Merge the former 32-page service atlas and 36-page connection atlas into one English reference. Deduplicate shared setup/notification history, retain original page/screen crosswalks and the review worksheet, and replace scattered closing errata with this reusable dated register.", "The register uses a fixed Date/Type column and a separate narrative/artwork column, repeating its header across pages. Supported categories are Security, Errata, Addendum and Change. Old source documents remain recoverable in Git; their historical evidence and qualifications are preserved above."), "Both source atlases in full; consolidated pages 1-13 and Revision register", "PDF authoring and visual review are document checks, not firmware/security runtime validation."),
    ]


# No parameters; returns the distinct dated security register entries.
def security_history():
    return (
        RevisionRecord("2026-10-10", "Security", "Fresh endpoint-bound proof before current data", (
            "A captured authenticated FIND can request a challenge, but cannot establish presence, change the current notification destination or obtain ACTIVITY, TECH or EVENT. READY is signed over device, pc, nonce, a random challenge, observed device IPv4 address and UDP port. Cody verifies that endpoint and answers COMMITTED.",
            "The ready-v2 and committed-v2 authentication domains require the matching fresh one-time response from that endpoint before commit recovery, liveness and current-data delivery. This is a coordinated firmware/plugin update; no old-handshake fallback is provided.",
        ), "Core page 4; FIND / READY / COMMITTED; screens 12-15 and current activity/questions", "CCP 0.6.3 source implementation/static review only; no deployment, pairing or adversarial runtime evidence accompanies this PDF."),
        RevisionRecord("2026-10-10", "Security", "Bound unauthenticated work and retained logs", (
            "Firmware limits each UDP receive pass by packet count and time, then runs local commands and protocol maintenance even while traffic remains queued. Fixed global and per-source ingress budgets precede expensive authentication. Rotating source addresses cannot bypass the global budget.",
            "PC discovery bounds total packets/offers and per-source offers; verifier work also has a time/work ceiling. Overflow is an explicit failure with no association staged, not an incomplete result labelled complete. Broker admission precedes credential work; rejection summaries and the exclusively owned rotating broker log bound network diagnostic growth.",
            "Residual: startup-only runtime.log still appends across launches. LAN UDP rejection traffic cannot grow that bootstrap file; bounding repeated-launch output is deferred separately from the implemented broker/network log rotation.",
        ), "Core page 10; device worker, PC discovery/pairing, broker ingress and runtime logs", "Source-only limits need measurement against real pairing/reconnection bursts. No suite, flood probe, build or flash is claimed."),
        RevisionRecord("2026-10-10", "Security", "Remaining protocol risks are explicit", (
            "Preserve local pairing admission, expiring codes, constant-time proofs, distinct active/pending journals, strict packet/identity checks, signed choices/settings/debug/reset and loopback-authenticated PC control. Native approval stays on the PC; storage/authentication failures remain failures.",
            "Six-character pairing transcripts still permit offline candidate checking; rate limiting does not solve it. CCP payloads remain authenticated and unencrypted. Standard PAKE or high-entropy QR admission followed by authenticated encryption is deferred as a coordinated redesign. An enabled optional device HTTP service needs its own review.",
        ), "Pairing verifier/link-key derivation; ACTIVITY / TECH / EVENT; optional HTTP", "No confidentiality or production-security certification. Compilation, stack headroom and physical recovery remain pending."),
    )


# argv: Optional generator arguments from the public device.ps1 route.
# Raises OSError for inaccessible sources, artwork or output.
# Raises ValueError for an unrepresentable page or unreviewed revision entry.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args(argv)
    register_fonts()
    preserve_sources(STAGING/"protected-sources")
    writer = PdfWriter()
    visual_bytes, visual_titles = visual_pdf()
    writer.append(PdfReader(BytesIO(visual_bytes)))
    for index, title in enumerate(visual_titles):
        writer.add_outline_item(title, index)
    first_register_page = len(writer.pages)
    history = revision_history()
    history.insert(0,RevisionRecord("2026-10-04","Change","One intended PC on a busy LAN",(
        "Cody retains one explicit PC identity even among 100 PCs; other responders do not replace it. First commissioning, a persisted relationship and authenticated current presence are separate facts. The original connection worksheet remains a separate writing page.",),
        "Original Connection pages 9 and 14; screens 12-16, 32, 35B, 40-42","Original design reference, not a current capture."))
    history.extend((RevisionRecord("2026-10-11","Errata","English correction retained from both earlier atlases",(
        "Service original page 33 and legacy Connection original page 37 translated historical Italian headings, diagram captions, body copy and footers into English. Original dates, screen IDs, technical values, artwork meaning and evidence boundaries were preserved. This dated correction is now consolidated in the canonical final grid.",),
        "Service page 33; Codex Connection page 37; original historical pages referenced therein","Document language and visual review only; no firmware or physical validation."),
        RevisionRecord("2026-10-12","Change","One canonical CCP communication reference",(
        "Unify the existing CCP Connection Atlas, former Service Atlas and legacy Codex Connection Atlas into coherent system, commissioning, wire/API, audio and evidence chapters. Keep the separate four-page A5 pairing quickstart as the nontechnical user guide. Preserve earlier meaningful decisions, original page/screen crosswalks and dated Errata, Addendum, Change and Security records.",
        "Current packet fields, order and ceilings; hook/technical projections; clock/name/debug/reset; MCP calls; and resource budgets are source-derived. Metadata remains authenticated plaintext. Audio is a separate AES-256-GCM source increment and does not strengthen six-character commissioning.",
        "Voice documentation follows source version 0.6.5 and the 12 October UI decision: enhanced PCM from the next real AFE block after wake detection, a ten-second PSRAM ring, authenticated UDP 42119, frozen native thread/turn identity and localAudio input. Capture lasts at most 60 seconds; deliberate shake cancels Listening or Sending without a voice Cancel button. Hide Cody. The upper synthetic wave is 40 pixels high. One central row cycles model, cost band, thinking effort, branch, then project; unavailable values are skipped using the existing fade/scroll style. There is no fixed cost pill. The lower hint alternates Shake to fade and We will send the audio to Codex for transcription in the same position; the long message scrolls inside the clipped row. Show both elapsed and remaining seconds. Native endpoint attachment, account/backend, dependencies, installation, cryptographic wire checks, device stack headroom and physical capture/delivery remain unverified. Whole native deadline: 15 seconds; uncertainty never becomes success.",),
        "All three technical source PDFs; A5 quickstart context; current CCP1.md, ccp_voice_input.py and LUNASHELL_voice_input.cpp",
        "PDF generation, rendering and visual review only. No tests, scans, configure/build/size, simulator, flash, hardware or native-input execution.")))
    history.append(RevisionRecord("2026-10-12","Change","Audio review: bounded window, readiness and honest uncertainty",(
        "Replace the one-packet DATA window with four 480-sample packets. Cumulative authenticated ACK names the highest contiguous sequence; out-of-order DATA is discarded while that prefix is acknowledged. Retry identical ciphertext after 100 ms. Transport runs every 5 ms with at least one scheduler tick. Ten-second unacknowledged PSRAM/grace may survive CCP disconnection only for the same authenticated key and IP. Transport failure after transmitted END remains Unconfirmed.",
        "Add a periodic read-only native initialize/thread-read probe: 5-second whole budget, refresh every 5 seconds, freshness 10 seconds. START requires recent readiness matching the genuine followed chat. Readiness does not certify localAudio, model support or transcription. Keep the START chat frozen, then resolve its current hook turn at submission for expectedTurnId. A concurrent native turn conflict fails explicitly; keep the WAV and do not retry automatically.",
        "Use the recording hex ID as clientUserMessageId for correlation without promising idempotency. Status exposes recording ID, native turn, WAV artifact and uncertainty. These source changes supersede the earlier single-packet and START-frozen-turn description without erasing that dated history.",),
        "Current main pages 24-26; original voice screens 28-30; firmware voice owner and PC native-audio broker",
        "Source implementation and PDF authoring only; tests, build, scans, installation and physical capture/delivery remain unverified."))
    history.append(RevisionRecord("2026-10-12","Addendum","Operator capabilities, customer tools and Debug boundaries",(
        "Add a source-backed operator guide for firmware/client, Windows CCP broker/server and the Codex plugin. Record installation and hook-review prerequisites, physical LAN pairing and explicit optional loopback audio configuration without publishing keys or tokens.",
        "List all seventeen MCP tools and all three supported skills, plus the ten genuine hook types. Separate customer/Release operations from Debug-only face_channels, character and normal views. Worker applied ACK is not physical pixel evidence; unknown timeouts and ambiguous mutations never become success or automatic retries.",
        "Document restart versus verified factory erasure, current-message intention and confirmation requirements, preserved data and admitted-versus-completed evidence. There is no CCP mount browser, arbitrary shell, face override or runtime first-boot setter; preserve the explicit persisted/compile-time distinction and separate USB engineering operations.",),
        "Original Communication main pages 1-28, protocol/MCP control chapters; added operator pages 27-34; CODY first-boot settings, plugin catalog, skills and command handlers",
        "Static source review and PDF generation/rendering/visual inspection only; no live tools, settings changes, tests, simulator, build, hardware, reboot, reset or audio submission."))
    history.append(RevisionRecord("2026-10-12","Change","Plain-language pairing and voice handoff",(
        "Add four illustrated plain-English opening pages to the canonical atlas. They explain the separate roles of Cody, the paired PC helper and the genuine desktop chat without requiring the reader to understand packet formats or endpoints.",
        "Document the current source rule for voice input: a visible-character double tap requests one recording; the PC binds START to the genuinely followed chat; a changed followed chat rejects the finished recording before native submission. The plugin keeps the private WAV for review, never chooses the new chat, creates no substitute chat, and never retries ambiguous native delivery automatically.",
        "Clarify that device audio is neither written to SD nor internal flash, and that actual end-to-end desktop acceptance, model support and transcription are still unverified. The detailed source chapters and their existing evidence boundaries remain after the new overview pages.",),
        "Current atlas opening pages 1-4; source voice screens 28-30; original communication pages 1-28; CCP voice input source increment",
        "Static source review plus PDF generation/rendering/visual inspection only. No tests, configure/build/size, simulator, flash, hardware capture, pairing, or native audio submission."))
    history.append(RevisionRecord("2026-10-12","Change","Coordinated cover and navigable contents",(
        "Add VibeWare cover artwork, author copyright and Draft / Confidential / Internal use only classification. Contents and bookmarks point to the final document pages; historical page and screen references retain their original identities.",),
        "All original pages; existing correction and Security sections preserved",
        "PDF authoring and visual review; no firmware execution. Consolidated here from the publisher's earlier separate revision page without changing its date or substance."))
    history.append(RevisionRecord("2026-10-15","Change","Voice countdowns, public audio API and integration policy",(
        "Update existing voice and operator chapters: Preparing and complete-screen admission, three-second listening countdown, five acquired seconds of silence, three-second sending countdown and immediate cancellation before submission. Physical user standby wakes share the recording flow; internal notification/recovery/shutdown wakes do not.",
        "Keep the audio explanation in this atlas: AFE/VAD is not speech recognition; encrypted PCM becomes a private PC WAV. OpenAI's exported localAudio input converts to native input_audio for Codex processing. CCP performs no speech-to-text. Preserve the intended chat and distinguish queue admission, actual Desktop receipt and understood speech.",
        "Remove the custom launcher/proxy prototype from source and the installed local cache before activation. Record the owner's rule: no hacks, undocumented entry points or fragile overrides without prior explanation and specific approval. General autonomy does not authorize an exception.",
        "The owner selected the public experimental queue prototype through physical Cody; the exact answer was acknowledged as delivered, and the owner confirmed the question flow physically. The installed native alpha passed initialize/queue-list and accepted one synthetic WAV through queue-add. No thread resume/start or model turn was requested by that queue writer. Same-chat Desktop delivery remains pending.",),
        "Existing opening voice page; original voice screens 28-30; current audio/native/operator chapters; added Audio input chapter in this same atlas",
        "Build/flash and physical recording validation are unchanged. Official sources: learn.chatgpt.com/docs/app-server; openai/codex commit 4ca84a10696623fcd7c200db3df9c41671d3bad0, thread_queue_processor.rs, ext/queue/src/service.rs and protocol/src/local_media.rs. Current source and installed alpha are different evidence levels."))
    history.append(RevisionRecord("2026-10-15", "Change", "Public audio pilot outcome", (
        "The synthetic localAudio item was admitted to this exact chat's public experimental queue and later left it. The next Desktop user turn said audio content omitted because audio input is unsupported. Queue transport worked, but this model/runtime did not expose the speech to Codex; no transcription or response to the spoken phrase occurred.",
        "Do not enable physical-device submissions through the route. Desktop Voice and dictation are documented user-interface features; no supported programmatic external-WAV ingress to this same chat was verified. OpenAI's separate audio transcription API would change the requested architecture and is not installed as a substitute.",
    ), "Current audio pilot; original voice screens 28-30", "Public queue observation plus the received Desktop turn; no physical-device audio submission."))
    history.append(RevisionRecord("2026-10-15", "Change", "Visual journey replaces the table-heavy atlas", (
        "Replace repeated narrative tables with a ten-page illustrated journey for system roles, Setup, pairing, activity, question delivery, recording, audio handoff, available operations and diagnosis. Existing UI proposal images are labelled as proposals rather than device captures.",
        "Retain earlier dates, substantive corrections, original page/screen identities and the Security section in the final concise register. Exact protocol fields remain in versioned CCP1.md; detailed earlier editions remain recoverable in Git.",
    ), "All former communication-atlas pages; original screens 01-42", "Document rendering and visual review only; no physical behavior is inferred."))
    register = visual_register(history,first_register_page+3)
    writer.append(PdfReader(BytesIO(register)))
    writer.add_outline_item("Revision register", first_register_page)
    first_security_page = len(writer.pages)
    security = visual_register(security_history(),first_security_page+3,"Security")
    writer.append(PdfReader(BytesIO(security)))
    writer.add_outline_item("Security", first_security_page)
    writer.add_metadata({"/Title": "Cody CCP Communication Atlas v1", "/Author": "Umberto Giacobbi", "/Subject": "Visual communication journey and dated revision register"})
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with arguments.output.open("wb") as destination:
        writer.write(destination)
    render_directory = STAGING/"final-render"
    render_directory.mkdir(parents=True,exist_ok=True)
    for pattern in ("page-*.png","contact-*.png"):
        for old in render_directory.glob(pattern):
            old.unlink()
    document = pymupdf.open(arguments.output)
    thumbnails = []
    for index,page in enumerate(document):
        image = page.get_pixmap(matrix=pymupdf.Matrix(1.15,1.15))
        image.save(render_directory/f"page-{index+1:02}.png")
        thumbnail = Image.frombytes("RGB",[image.width,image.height],image.samples)
        thumbnail.thumbnail((421,298))
        thumbnails.append(thumbnail)
    for index in range((len(thumbnails)+5)//6):
        contact = Image.new("RGB",(1263,596),"#c7d1db")
        for offset,thumbnail in enumerate(thumbnails[index*6:index*6+6]):
            contact.paste(thumbnail,((offset%3)*421,(offset//3)*298))
        contact.save(render_directory/f"contact-{index+1:02}.png")
    (STAGING/"receipt.json").write_text(json.dumps({"pages":len(document),"main_pages":first_register_page,
        "revision_records":len(history),"security_records":len(security_history()),
        "protected_sources":"protected-sources/manifest.json","evidence":"PDF authoring; runtime validation deferred"},indent=2),encoding="utf-8")
    print(f"Generated {arguments.output} ({len(writer.pages)} pages)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
