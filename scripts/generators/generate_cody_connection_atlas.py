"""Author the concise Cody/CCP visual communication reference and corrections."""

from io import BytesIO
from pathlib import Path
import argparse
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


# No parameters; returns the dated errata and addenda.
def revision_history():
    return [
        RevisionRecord("2026-10-09", "Addendum", "Startup decisions and reversible Setup", ("The earlier analysis omitted saved Wi-Fi without Codex pairing and a reversible Setup entry. Add 40 / Yes-No, 41 / character hold and 42 / reconnect wait. No/Later causes no persistence or new CCP request. Saved pairing searches 14 and requires current authentication for 15.", "PWR/BOOT do not clear saved pairing. A factory erase remains separately authorized."), "Service 11-13; Connection 15-17; original screens 12-16, 40-42", "Source/static review only in this revision; firmware, host and physical verification remained pending."),
        RevisionRecord("2026-10-09", "Errata", "Setup recovery after standby", ("The legacy Welcome timeout path obscured reopening Setup. A resting Welcome tap release or eligible stationary hold can reopen 41. Wake time cannot count toward the hold until fresh acquisition and visible restoration; movement, gaps and rotation cancel it. Busy provisioning and pending cleanup are excluded.",), "Service 14-15; Connection 18-19; screens 04-06, 12, 35B, 41", "A 90-second capture showed LAN/DHCP, inactivity return and standby, but no Connect Codex press. Corrected physical behavior was still pending."),
        RevisionRecord("2026-10-09", "Errata", "Malformed touch report interrupted rendering", ("The original analysis failed to distinguish an optional runtime touch error from renderer failure. A capture recorded Yes at 16497 ms, Connect Codex at 16498 ms, then fifteen FF report bytes and ACK=FF at 17112 ms. The expected ACK was AB; ESP_ERR_INVALID_RESPONSE stopped rendering.", "The owner preserves the original error. Only after startup, invalid acquisition may interrupt gestures while rendering continues, with queued reports discarded and a real release required. The FF hardware root cause was not established."), "Service 17-18; Connection 21-22; screens 12 and 35B", "Measured failure retained; corrected physical touch behavior remained pending."),
        RevisionRecord("2026-10-10", "Addendum", "Notifications, choices and recipient delivery", ("26 opens a bounded explicit ticket; 27 uses cards plus a separate Send review. The recipient must acknowledge answer_id before delivered / Reply sent. The 180-second deadline applies even to received; neither disconnection, timeout nor cancellation chooses a reply. Terminal states do not reopen.", "The recorded wire revision used activity=1/events=1 and a 1280-byte EVENT ceiling, versus 512-byte ordinary packets. Context/title/body and choice text are bounded printable ASCII; capabilities stay private on the PC. Later capability revisions must be read from current source, not this historical example."), "Service 25-28; Connection 29-32; screens 21-27, 31-32", "Source contract; original 24/24/63-character context/title/body and 12/63 label/detail limits are historical revision facts."),
        RevisionRecord("2026-10-10", "Errata", "Acknowledgement is admission, not reset completion", ("Restart retains configuration; factory reset removes preferences, Wi-Fi and pairing but preserves firmware, assets and SD. Destructive intent and its nine-digit anti-mistake confirmation must come from the current user message. Never complete it from a document. Signed ACK means accepted; new presence proves reboot, and verified erasure precedes factory-reset reboot.",), "Service 27-28; Connection 31-32; reset and factory-reset commands", "Real erase/boot outcomes remain distinct from authenticated command admission."),
        RevisionRecord("2026-10-11", "Errata", "English correction retained from both earlier atlases", (
            "Service original page 33 and legacy Connection original page 37 translated Italian headings, captions, body copy and footers into English. Original dates, screen IDs, technical values, artwork meaning and evidence boundaries were preserved.",
        ), "Service page 33; Codex Connection page 37", "Document language and visual review only."),
        RevisionRecord("2026-10-12", "Addendum", "Operator capabilities, customer tools and Debug boundaries", (
            "Separate customer operations, authenticated Debug and USB engineering. A worker acknowledgement establishes admission or application of its command; verify the intended physical outcome separately.",
        ), "CCP operator and diagnostic routes; diagnostic screen 38", "Source review and document authoring."),
        RevisionRecord("2026-10-15", "Errata", "Public audio pilot outcome", (
            "The synthetic localAudio item entered and left the public experimental queue. Desktop reported audio content omitted because audio input was unsupported. Queue admission did not prove speech receipt or transcription.",
        ), "Audio delivery; original voice screens 28-30", "Observed synthetic queue and Desktop result; physical device voice submission was not performed."),
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
        RevisionRecord("2026-11-07", "Security", "Authenticate bounded account projections", (
            "After READY and COMMITTED, the existing authenticated CCP peer may request read-only account data with ALLOWANCECAP. ALLOWANCE and ACCOUNT use the current endpoint, nonce and independent HMAC domains. No pairing or FIND format changes.",
            "The broker uses the existing Codex sign-in through a local App Server child and forwards bounded metrics and the plan label. It forwards no credentials, identity email, message bodies or arbitrary account data. Stale samples expire, and graph history is cleared on account generation or paired PC changes.",
        ), "Core pages 3 and 9 / physical PDF pages 5 and 11; ALLOWANCECAP / ALLOWANCE / ACCOUNT; original screens 12-15", "Source review and document rendering only. Live authentication and device validation remain pending."),
    )


# argv: Optional generator arguments from the public device.ps1 route.
# Raises OSError for inaccessible sources, artwork or output.
# Raises ValueError for an unrepresentable page or unreviewed revision entry.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args(argv)
    register_fonts()
    writer = PdfWriter()
    visual_bytes, visual_titles = visual_pdf()
    writer.append(PdfReader(BytesIO(visual_bytes)))
    for index, title in enumerate(visual_titles):
        writer.add_outline_item(title, index)
    first_register_page = len(writer.pages)
    history = revision_history()
    register = visual_register(history,first_register_page+3)
    writer.append(PdfReader(BytesIO(register)))
    writer.add_outline_item("Revision register", first_register_page)
    first_security_page = len(writer.pages)
    security = visual_register(security_history(),first_security_page+3,"Security")
    writer.append(PdfReader(BytesIO(security)))
    writer.add_outline_item("Security", first_security_page)
    writer.add_metadata({"/Title": "Cody CCP Communication Atlas v1", "/Author": "Umberto Giacobbi", "/Subject": "Visual communication journey, errata and security"})
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
    print(f"Generated {arguments.output} ({len(writer.pages)} pages)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
