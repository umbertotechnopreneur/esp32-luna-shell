# Cody / CCP Service Atlas and PDF attribution

Requested by Umberto Giacobbi on 4 October 2026.

Created [`cody-ccp-service-atlas-v1.pdf`](../Cody/cody-ccp-service-atlas-v1.pdf),
an English ten-page A4 landscape service explainer in the existing atlas style.
The document uses deterministic vector diagrams, the original front-facing
cloud, one unchanged connection-atlas ImageGen scene, and note lines throughout.

## Page map

1. Service purpose: discovery, pairing and authenticated liveness.
2. Codex/MCP, local Python CCP and the separate CCP1 UDP device link.
3. DISCOVER broadcast, direct OFFER replies, pairing and collection windows.
4. Display-code verification, exactly-one-match gate and ambiguous/no-match cases.
5. PAIR / PAIRED / CONFIRM / ACTIVE persistence sequence and retry windows.
6. One paired identity among 100 PCs; explicitly an illustration, not a load test.
7. Screen 13 -> 16 -> 14 -> 15 and authenticated FIND / READY rediscovery.
8. Two-second liveness cadence, eight-second device expiry and separate PC status.
9. Security boundary, offline-guessing caveat, DPAPI, NVS CRC and code handling.
10. Deployment prerequisites, current scope and unverified end-to-end behavior.

The code and protocol were read, not executed. Sources are
`plugins/cody-codex-plugin/protocol/CCP1.md`, `README.md`, `mcp.json`,
`mcp/ccp_server.py`, and
`components/LUNASHELL_platform/src/network/LUNASHELL_codex_link.cpp`.
No Codex task, chat-selection or message transport is claimed.

## Attribution applied to the whole Cody PDF collection

All five current PDFs now have Author `Umberto Giacobbi` and contact
`hello@umbertogiacobbi.biz`, both visibly and in PDF metadata. The four older
documents retain their original content, copyright, page numbers and page count:
planning (26), navigation (16), connection (14), and A5 quick start (2).
An additive 6.4-point attribution line occupies the unused footer band on their
58 pages. The new service atlas uses a larger attribution in its main footer.

Current hashes and backup locations are recorded in Obsidian under
`atlas/source/cody-pdf-attribution-review.json`; earlier artifact hashes remain
historical evidence and are not silently rewritten.

## Files and review

Primary folder:
`C:\Users\umber\OneDrive\Obsidian\30_Engineering\Products\Cody Companion\atlas`.
The five repository PDF mirrors have identical bytes to their Obsidian originals.

- `source/build_ccp_service_atlas.py`: new atlas authoring source.
- `source/ccp-service-page-manifest.json`: ten-page index.
- `source/apply_pdf_attribution.py`: explicit, idempotent attribution pass with backups.
- `source/review_ccp_service_documents.py`: rendering and historical body comparison.
- `source/ccp-service-document-review.json`: page dimensions and preservation evidence.
- `source/cody-pdf-attribution-review.json`: metadata, hashes, mirrors and backup paths.
- `artifacts/document-review/ccp-service-atlas/`: local render/backup evidence.

All ten new pages were rendered at a 1500-pixel long edge and visually reviewed;
revised pages were re-rendered and inspected after arrow/text spacing fixes.
All 58 older pages were rendered before and after attribution at 96 DPI.
Their bodies above the footer band are pixel-identical. Every updated footer
was inspected in four labelled contact sheets. Page text and dimensions are
preserved; names/contact are present on every page and in document metadata.

No firmware tests, build, simulator, plugin install/run, firewall/network changes,
or physical-device/printing checks were performed. Existing unrelated repository
work was left intact. This is documentation evidence, not runtime validation.
