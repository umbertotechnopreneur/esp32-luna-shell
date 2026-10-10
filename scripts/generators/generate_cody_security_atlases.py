"""Reorganize the latest atlas supplements and add a structured security section."""

import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import sys

from pypdf import PdfReader, PdfWriter

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/common"))
from cody_pdf_revision_register import RevisionRecord, render_revision_register

REVISION = "CODY-SECURITY-20261010"
STAGING = ROOT / "artifacts/docs/cody-security-20261010"
DOCUMENTS = ("cody-navigation-atlas-v1.pdf", "cody-planning-atlas-v1.pdf")
PRESERVED_PAGES = 46


# name: Exact navigation or planning document identity.
# Raises ValueError for an unsupported document identity.
def latest_records(name):
    if name not in DOCUMENTS:
        raise ValueError("Unsupported atlas: " + name)
    pages = "2, 3, 4, 12" if name == DOCUMENTS[0] else "4, 5, 6, 7, 8"
    date = "10 October 2026"
    return (
        RevisionRecord(date, "Change", "A single final revision register", (
            "The 10 October screen supplements previously on pages 47-50 are reorganized into this grid. Their dates, decisions, screen IDs and evidence limits remain explicit. All preceding 46 pages, including historical artwork and earlier corrections, remain intact.",
            "Earlier decisions are historical: 03 October A01 separates installation and provisioning; 04 October A02 preserves the skin selector's caller, A03 keeps PR status read-only, and A04 gates pairing handoff. The 05-06 October idle and branding records, and the 09-10 October setup, retained pairing, activity and notification revisions remain in their preserved pages.",
        ), affected="Previous pages 47-50; earlier history on pages 13-46 (Navigation) or 23-46 (Planning).",
            evidence="Document reorganization only. Original physical page numbers remain valid through page 46; later references use stable screen IDs."),
        RevisionRecord(date, "Addendum", "Boot and Wi-Fi", (
            "Cody AI Companion and A distraction-free sidekick appear together above the complete floppy artwork. Its 42 frames retain the 3.5-second cycle; startup work can extend wall time. Title y=60, subtitle y=110, artwork (137,164), 192 x 219.",
            "After local startup, absent Wi-Fi opens Welcome 04, missing association/IP opens 42, a valid LAN without pairing opens 40, and retained pairing searches its PC on 14. Screen 05 opens the installation QR on 06 without starting radios; Pair Cody starts Bluetooth on 07. The session QR exists only at ReadyToScan and retires when credentials or connection are in progress. Cancel waits for confirmed cleanup.",
            "Screen 09 admits Continue only with current valid Wi-Fi/IP. Screen 42 offers Network setup or Later. Local LAN and public Internet are separate facts. Debug handoff 39 requires a fresh tap; Release continues to 09.",
        ), affected="Original page 47; branding, screens 01-09, 39, 40 and 42."),
        RevisionRecord(date, "Addendum", "Pairing and connection", (
            "Hold the visible character still for 1.5 seconds to open Setup. Connect Codex and Network setup share a row; Options is centered below. Release the opening touch before selecting. Back follows the valid previous route.",
            "Already paired offers Keep pairing, Pair again and Forget. Only Pair again authorizes replacement; Forget needs a second confirmation. Pairing paused identifies an incomplete candidate. Pairing error preserves the storage cause and permits only explicit removal.",
            "Preparing secure code / Please wait / About 30 seconds is an estimate, not measured progress. Use all six characters on the intended Cody after discovery. The backend pairing window is five minutes; screen inactivity is three minutes from code readiness. You're all set follows the two-sided persisted handshake. Let's go searches the retained PC immediately, without the earlier ten-second Idle detour.",
            "Security clarification: a signed READY now challenges the connection; the broker admits presence and releases current data only after the fresh endpoint-bound COMMITTED response. Genuine prompt hooks establish chat activity, independently of pairing or presence.",
        ), affected="Original page 48; screens 12-15, 40 and 41."),
        RevisionRecord(date, "Addendum", "Settings and complete controls", (
            "Shake to wake, Auto-rotate and Sounds retain persistent On/Off values. Orientation shows Auto when auto-rotation is on; manual orientation is 0 or 180 when off. Sounds Off stops playback while preserving microphone and wake-word operation.",
            "Next opens Settings / Personalization / Choose Cody's look. Continue opens the stable-ID skin selector: Pearl Mist, Aurora Veil, Cosmic Stardust, Enchanted Mosaic and Dreamwave. A save preserves the prior value until successful write/readback. Save failed and Retry retain the cause and requested value; no silent repair is admitted.",
            "Two actions use side-by-side gradient cards; three add one centered card below. A single action uses an icon-left capsule. Captions never use teletype: controls and touch become available together after title and body narration. Preserve QR quiet zones and the local circular chord.",
        ), affected="Original page 49; screens 10, 11, 36 and 41."),
        RevisionRecord(date, "Errata", "Current screen corrections and evidence", (
            "Replace the earlier title-free/twofold boot drawing with the current title, subtitle and 192 x 219 artwork. Replace obsolete setup, secure-code preparation, connection detour and settings descriptions with the current contracts above. Historical values and diagrams retain their dated meaning.",
            "The source-based static schematics use illustrative fonts and values. They are neither simulator frames nor device captures. Layout coordinates are not sensor measurements. This PDF revision does not verify firmware, simulator, hooks or physical behavior.",
        ), affected="Original page 50; current base pages " + pages + "; branding, screens 01-15, 36, 40-42.",
            evidence="English grid replaces the latest four appendix pages; prior corrections and unrelated pages are preserved."),
    )


# Returns the security implementation record shared by both atlases.
def security_records():
    date = "10 October 2026"
    return (
        RevisionRecord(date, "Security", "Fresh proof before presence or current data", (
            "A captured authenticated broadcast FIND must not establish broker presence, select a notification destination or obtain current ACTIVITY, TECH or EVENT data. The broker issues a random challenge, signed together with the observed device IPv4 address and UDP port. Cody verifies that endpoint before answering.",
            "READY and COMMITTED use explicit ready-v2 and committed-v2 authentication domains and the fields device, pc, nonce, challenge, address, port and proof. Only the matching one-time challenge response from the challenged endpoint admits liveness, commit recovery and current-data delivery. Old and new firmware/plugin implementations must be updated together; no legacy handshake fallback is provided.",
        ), affected="Screens 12-15, 18U, 26-27, 31-32; CCP FIND / READY / COMMITTED.",
            evidence="Source implementation and static review. CCP 0.6.3 is not deployed or physically verified by this document revision."),
        RevisionRecord(date, "Security", "Bound unauthenticated work", (
            "Firmware limits the receive pass by packet count and elapsed time so local commands, expiry and protocol maintenance still run while UDP remains busy. Fixed-size global/per-source ingress admission precedes expensive authentication; source-address rotation cannot remove the global budget.",
            "PC discovery bounds total offers and per-source contributions, and pairing bounds distinct verification work and time. Overflow is an explicit failure, rather than a partial result presented as complete. The broker applies ingress limits before expensive credential access and aggregates rejection diagnostics; rotating logs bound retained storage.",
            "Initial limits: firmware receives at most eight packets or a 5-ms slice, with 32 packets/s globally (burst 64), 16/s per IP (burst 32), and 16 source slots. PC UDP allows 64/s globally (burst 128), 16/s per IP (burst 32), and 64 slots. Discovery admits 16 offers, four per IP, and 128 received packets; pairing verification has a two-second deadline before candidate persistence.",
            "Endpoint challenges have a two-second lifetime and a 128-entry cap. Rejection summaries appear at most once per five seconds. Broker logs retain at most three 2-MiB files; local control admits at most 32 concurrent workers, retaining its three-second socket deadline and 64-KiB request-body limit.",
        ), affected="Firmware UDP worker; PC discovery and pairing; CCP ingress and runtime logs.",
            evidence="Initial tuning constants require measurement against real pairing/reconnection bursts. No flood probe, suite, build or flash is claimed."),
        RevisionRecord(date, "Security", "Retained safeguards and deferred protocol work", (
            "Keep local user admission, the expiring pairing window, constant-time HMAC verification, separate active/pending durable records, strict identities and packet bounds, authenticated choices/settings/debug/reset commands, and loopback-only authenticated PC control. Native approval remains on the computer. Failed storage or authentication must not become success.",
            "The six-character code still permits offline candidate checking from a captured pairing transcript; online rate limiting does not remove that risk. CCP payloads remain authenticated but unencrypted. Evaluated PAKE or high-entropy QR pairing, followed by standard authenticated encryption and replay discipline, is deferred as a coordinated protocol redesign. No custom cryptography or confidentiality claim is introduced.",
            "Optional device HTTP behavior is a separate security scope: enabling its plaintext bearer API or broad health endpoint requires its own review. This CCP hardening does not claim to secure an enabled device HTTP service or a hostile local machine.",
            "Log scope: broker/network diagnostics rotate under the exclusive broker owner. The startup-only runtime.log still appends across launches; LAN UDP rejection traffic cannot write to it. Bounded multi-process bootstrap logging remains a separate follow-up.",
        ), affected="Pairing code and link-key derivation; ACTIVITY / TECH / EVENT confidentiality; optional device HTTP.",
            evidence="Remaining risks are recorded in planned-todo.md. Compilation, device stack headroom and physical pairing/recovery checks remain pending in TODO.md."),
    )


# argv: Literal authoring arguments passed by scripts/device.ps1.
# Raises OSError for unreadable inputs or failed publication.
# Raises ValueError when protected sources or current outputs changed independently.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--redraft", action="store_true", help="Reauthor this revision from its protected original PDFs")
    args = parser.parse_args(argv)
    STAGING.mkdir(parents=True, exist_ok=True)
    manifest_path = STAGING / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else {}
    records = {item["file"]: item for item in manifest.get("documents", [])}
    prepared = []
    for name in DOCUMENTS:
        source = ROOT / "docs/Cody" / name
        current = source.read_bytes()
        backup = STAGING / (source.stem + "-original.pdf")
        if args.redraft:
            record = records.get(name)
            if record is None or sha256(current).hexdigest() != record["output_sha256"]:
                raise ValueError("Current PDF differs from the recorded draft: " + name)
            original = backup.read_bytes()
            if sha256(original).hexdigest() != record["original_sha256"]:
                raise ValueError("Protected PDF changed: " + name)
        else:
            if name in records:
                raise ValueError("Revision already authored; use --redraft: " + name)
            original = current
            if backup.exists() and backup.read_bytes() != original:
                raise ValueError("Protected source differs: " + name)
            backup.write_bytes(original)
        reader = PdfReader(BytesIO(original))
        if len(reader.pages) != 50:
            raise ValueError("Expected the reviewed 50-page screen revision: " + name)
        dimensions = (float(reader.pages[0].mediabox.width), float(reader.pages[0].mediabox.height))
        title = "Cody / Navigation Atlas" if name == DOCUMENTS[0] else "Cody / Planning Atlas"
        writer = PdfWriter()
        # Keep original page geometry, drawings and eligible bookmarks through page 46.
        writer.append(reader, pages=(0, PRESERVED_PAGES))
        history = PdfReader(BytesIO(render_revision_register(latest_records(name), dimensions, title,
                                                            start_page=PRESERVED_PAGES + 1)))
        writer.append(history)
        security_start = len(writer.pages)
        security = PdfReader(BytesIO(render_revision_register(security_records(), dimensions, title,
                                                             start_page=security_start + 1, section_title="Security")))
        writer.append(security)
        writer.add_outline_item("Revision register / 10 October 2026", PRESERVED_PAGES)
        writer.add_outline_item("Security / 10 October 2026", security_start)
        writer.add_metadata({"/Title": title, "/Subject": REVISION, "/ModDate": "D:20261010000000+07'00'"})
        destination = STAGING / name
        with destination.open("wb") as stream:
            writer.write(stream)
        prepared.append((source, destination, {"file": name, "original_sha256": sha256(original).hexdigest(),
                         "output_sha256": sha256(destination.read_bytes()).hexdigest(),
                         "preserved_pages": PRESERVED_PAGES, "final_pages": len(writer.pages),
                         "revision_register_page": PRESERVED_PAGES + 1, "security_page": security_start + 1}))
    # Finish both candidates before replacing either requested document.
    for source, destination, record in prepared:
        source.write_bytes(destination.read_bytes())
        print(record["file"] + ": " + str(record["final_pages"]) + " pages; Security starts on " + str(record["security_page"]))
    manifest_path.write_text(json.dumps({"revision": REVISION, "documents": [record for _, _, record in prepared]}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
