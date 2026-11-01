# Latest validation round

Keep only the latest firmware validation round here. Replace this summary when
another round completes; do not append delivery history or CI/PR reports.
Open work belongs in [TODO.md](TODO.md) and [planned-todo.md](planned-todo.md).

## 2026-10-20: Cody Debug build

- Source: `027800b1`; firmware: `cody-229-g027800b1`.
- Profile: Cody Companion with wake word and activity classifier; ESP-IDF 6.0.2.
- Result: source build passed at four jobs; resolved dependency lock matched
  the canonical lock.
- Application: 7,083,232 bytes; 191,264 bytes free in the 7,274,496-byte OTA slot.
- Output: `artifacts/build-cody-companion-wake-word-activity-debug/`.
- Scope: build only. No flash, simulator, capture or automated suite was run.
  Physical acceptance remains open.
