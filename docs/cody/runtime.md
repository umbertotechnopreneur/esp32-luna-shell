<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Cody behind the experience

**VibeWare · Cody Companion**

**Author:** Umberto Giacobbi · **Created:** 2026-10-04 · **Updated:** 2026-11-12 · **Revision:** 1.0

A friendly face is only part of the story. This guide brings together the product's main boundaries, future ideas and pointers for anyone who wants to explore the source.

## The experience today

The source brings together the VibeWare startup, Cody's character, device settings and a local Codex connection. Supported behavior depends on the selected board and build. The physical experience, timing, audio and other outstanding checks remain in [TODO.md](../../TODO.md).

The 1.75 GPS prototype supports removable microSD; the 1.75C does not. The boards have different capabilities, so a feature on one should not be assumed on the other. See the [hardware guide](../bsp/boards/purchased-amoled-comparison.md).

## Minimal character direction

Cody can respond to observed activity with small expressions and movements. Some reactions require a current, matching chat and turn. They are artistic cues, not measurements of a person's feelings or proof that a task is complete. See [Meet Cody](behavior.md) and [expressions](personality-icons.md).

## Device-first notifications

Cody can present connection changes, questions and other supported events. Sounds respect the user's settings and quiet hours. A question answer is delivered only when the receiving side confirms it; pending or ambiguous outcomes stay distinct. More detail is in the [PC plugin](../../cody-desktop/plugins/cody-codex-plugin/README.md) and [CCP protocol](../../cody-desktop/plugins/cody-codex-plugin/protocol/CCP1.md).

## Clock presentation

Cody uses available time information for its clock and time-based presentation. Automatic network time and PC synchronization are separate options. The device stores UTC and uses the configured offset for local display; a cold boot without a usable clock source can show time as unavailable. See the [time service](../bsp/services/time.md).

## One-way voice input source increment

Speech and voice input are separate features. The default Cody profile includes English flash speech but omits microphone acquisition; the explicit `none` variant omits synthesis and microphone acquisition. The Cody wake-word variant combines Sophia for standby wake, English flash speech and the voice-input owner, which hands recorded audio to the paired PC. A ready connection and supported desktop configuration are required; queue admission does not establish transcription or that the model understood the recording. Physical behavior still needs verification. See [wake on word](../bsp/services/wake-on-word.md).

## The road ahead

Ideas for Cody include richer awareness, targeted messages, work controls and reminders. They are plans, not a claim that every action is available. Voice and usage screens were deferred. The current priorities, prerequisites and verification gates are in [TODO.md](../../TODO.md) and [planned-todo.md](../../planned-todo.md).

The direction is to first make activity and attention easier to understand, then explore choosing a specific chat, sending a reviewable message and requesting an update. Later ideas include stopping work, opening a chat on the PC and reminders. Each needs a supported connection to the intended chat and a clear result; Cody must not guess a target, silently repeat an uncertain request or treat a request as completed work.

The multi-chat work distinguishes sources, chats, turns, calls and questions so an action can target the right item. A local bookmark does not change the PC's followed chat. Read the [domain types](../../cody-device/components/CODY_domain/include/CODY_domain_types.hpp), [domain component](../../cody-device/components/CODY_domain/README.md) and [state protocol](../../cody-desktop/plugins/cody-codex-plugin/protocol/CODY_STATE_V1.md) for implementation details.

## Remote connections

The normal Cody-to-PC connection uses authenticated CCP on the local network. A private cloud relay is a separate, evolving project. An optional device build can check relay reachability; that check alone does not establish enrollment or event delivery. Remote enrollment, device delivery and operational acceptance remain open. The relay source and infrastructure are private; credentials, device identities and messages do not belong in public documentation. See [relay commands](../../scripts/README.md#private-relay-through-aspire) and [open work](../../TODO.md).

The relay's public liveness check, protected agent and device routes, and storage readiness are separate concerns. Replacing a running relay also requires a deliberate handover of its single storage writer; a successful health response alone does not prove storage or delivery is working.

## Experiments and source

An experimental activity observer uses pretrained model labels for a limited set of movements. It is not available in the Cody profile and is not an automatic power policy. The original model weights have no identified license and are not included in the repository; permission to distribute firmware containing them remains unresolved.

Cody uses a native 2D interface. The [companion source](../../cody-device/components/CODY_companion/), [CODY2D source](../../cody-device/components/CODY2D/) and [shared platform](../../cody-device/components/LUNASHELL_platform/) are the best places to explore technical details. The [screen catalog](screen-catalog.md) preserves original screen identities and evidence; [layout](layout.md) and the [design system](design-system.md) preserve the interface contracts.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
