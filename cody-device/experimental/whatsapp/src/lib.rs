//! Experimental WhatsApp protocol owner for Cody.
//!
//! Optional firmware integration; the caller supplies network, time, a
//! dedicated store, a suitable executor thread and a sink.

use std::collections::VecDeque;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use whatsapp_esp32::{Esp32Executor, Esp32Runtime, Esp32TransportFactory, EspHttpClient, NvsStore};
use whatsapp_rust::bot::Bot;
use whatsapp_rust::{CacheConfig, MsgSecretPolicy};
use whatsapp_rust::async_channel::{self, Receiver, Sender};
use whatsapp_rust::futures::future::{select, Either};
use whatsapp_rust::wacore::store::traits::DeviceStore;
use whatsapp_rust::wacore::types::events::{Event, EventKind};
use whatsapp_rust::wacore::types::message::{EditAttribute, SenderMessageId, StanzaMessageType};

const RECENT_MESSAGE_LIMIT: usize = 64;

struct Reservation {
    stop: Sender<()>,
    receiver: Option<Receiver<()>>,
}

// The reservation is acquired before the ESP task is started so a stop request
// cannot be lost in the gap between task creation and Rust initialization.
static SESSION: Mutex<Option<Reservation>> = Mutex::new(None);

/// Information forwarded to the eventual Cody integration.
/// A QR payload is sensitive and is valid only during the callback.
pub enum Notice<'a> {
    PairingQr { payload: &'a str, valid_for: Duration },
    PairingExpired,
    Connected,
    Disconnected,
    LoggedOut,
    /// An admitted text or media message; deduplication is bounded to this run.
    IncomingMessage,
    UndecryptableMessage,
}

/// Receives events on the protocol executor, not on the UI thread.
/// Implementations must return promptly and copy QR data into bounded, private
/// storage before returning. They must not log the QR or message content.
pub trait NoticeSink: Send + Sync + 'static {
    fn notify(&self, notice: Notice<'_>);
}

const HANDLED_EVENTS: &[EventKind] = &[
    EventKind::PairingQrCode,
    EventKind::PairingQrCodesExhausted,
    EventKind::Connected,
    EventKind::Disconnected,
    EventKind::LoggedOut,
    EventKind::Messages,
    EventKind::UndecryptableMessage,
];

/// Run one client session on an appropriately sized dedicated executor thread.
///
/// `sink`: Thread-safe destination for transient pairing and message events.
/// `stop`: Single-use stop request, retained from task admission until shutdown.
/// Errors: Returns storage, runtime, or client initialization errors unchanged.
/// The caller must initialize the network and clock and provision `wa_store`.
/// This session does not request a store reset and does not restart after a terminal exit.
pub async fn run_session_inner(
    sink: Arc<dyn NoticeSink>, stop: Receiver<()>, runtime: Esp32Runtime, store: Arc<NvsStore>,
) -> anyhow::Result<()> {
    let recent = Arc::new(Mutex::new(VecDeque::<SenderMessageId>::new()));
    // Cody only notifies new messages. Do not retain message-specific secrets
    // for edits, polls or other add-ons; these are distinct from Signal session
    // and pairing keys, which the backend continues to preserve.
    let mut cache_config = CacheConfig::default();
    cache_config.msg_secret_policy = MsgSecretPolicy::Disabled;
    cache_config.seed_msg_secrets_from_history = false;
        let bot = Bot::builder()
            .with_backend_arc(store)
            .with_transport_factory(Esp32TransportFactory::default())
            .with_http_client(EspHttpClient::default())
            .with_runtime(runtime)
            .with_cache_config(cache_config)
            .with_wanted_pre_key_count(20)
            .skip_history_sync()
            .on_event_for(HANDLED_EVENTS, move |event, _client| {
                let sink = Arc::clone(&sink);
                let recent = Arc::clone(&recent);
                async move {
                    match &*event {
                        Event::PairingQrCode(qr) => sink.notify(Notice::PairingQr {
                            payload: &qr.code,
                            valid_for: qr.timeout,
                        }),
                        Event::PairingQrCodesExhausted(_) => sink.notify(Notice::PairingExpired),
                        Event::Connected(_) => sink.notify(Notice::Connected),
                        Event::Disconnected(_) => sink.notify(Notice::Disconnected),
                        Event::LoggedOut(_) => sink.notify(Notice::LoggedOut),
                        Event::Messages(messages) => {
                            for message in messages {
                                let info = &message.info;
                                if info.source.is_from_me || info.id.is_empty() ||
                                    info.source.chat.is_status_broadcast() || info.source.chat.is_newsletter() ||
                                    !matches!(&info.edit, EditAttribute::Empty) ||
                                    !matches!(info.r#type.as_ref(), Some(StanzaMessageType::Text | StanzaMessageType::Media)) {
                                    continue;
                                }
                                let id = SenderMessageId::new(info.source.chat.clone(),
                                    info.id.clone(), info.source.sender.clone());
                                let admitted = match recent.lock() {
                                    Ok(mut entries) if !entries.contains(&id) => {
                                        if entries.len() == RECENT_MESSAGE_LIMIT { entries.pop_front(); }
                                        entries.push_back(id);
                                        true
                                    }
                                    _ => false,
                                };
                                if admitted { sink.notify(Notice::IncomingMessage); }
                            }
                        }
                        Event::UndecryptableMessage(_) => sink.notify(Notice::UndecryptableMessage),
                        _ => {}
                    }
                }
            })
            .build()
            .await?;
    // Retain the release observer, not a client/backend reference, across the
    // shutdown. A new session must not race detached persistence owners.
    let client = bot.client();
    let release = client.store_release();
    let handle = bot.spawn();
    let (shutdown, outcome, stop_requested) = match select(Box::pin(handle), Box::pin(stop.recv())).await {
        Either::Left((outcome, _)) => {
            // A completed run still needs the terminal persistence drains. The
            // handle is consumed by select, so retain the client for this path.
            (client.shutdown().await, outcome, false)
        }
        Either::Right((stop_result, handle)) => {
            let report = (*std::pin::Pin::into_inner(handle)).shutdown().await;
            (report.shutdown, report.run, stop_result.is_ok())
        }
    };
    // Preserve a failure until after the backend ownership fence even when a
    // flush failed, so the next session cannot race outstanding storage work.
    let cleanup = if let Err(error) = shutdown.device {
        Err(anyhow::Error::new(error))
    } else if shutdown.inbound != whatsapp_rust::DrainOutcome::Completed ||
        shutdown.outbound != whatsapp_rust::DrainOutcome::Completed ||
        shutdown.signal_settle != whatsapp_rust::DrainOutcome::Completed ||
        shutdown.message_secrets.failed_batches != 0 {
        Err(anyhow::anyhow!("WhatsApp shutdown did not confirm all persistence operations"))
    } else if !stop_requested || !matches!(outcome, whatsapp_rust::bot::BotRunOutcome::Completed(_)) {
        Err(anyhow::anyhow!("WhatsApp session ended unexpectedly: {outcome:?}"))
    } else {
        Ok(())
    };
    drop(client);
    release.wait().await;
    cleanup
}

/// `sink`: Thread-safe destination for transient pairing and message events.
/// `stop`: Single-use stop request; the receiver stays live until shutdown.
/// `resume_only`: Return without creating a bot when no registered device exists.
/// Returns `false` when resume-only finds no registered device, `true` after
/// an orderly session exit.
/// Errors: Returns original initialization, persistence, or lifecycle failures.
pub fn run_session(sink: Arc<dyn NoticeSink>, stop: Receiver<()>, resume_only: bool) -> anyhow::Result<bool> {
    let store = Arc::new(NvsStore::open_default_on_internal_stack()?);
    let (runtime, executor) = Esp32Runtime::create_default()?;
    executor.block_on(async move {
        if resume_only && !store.load().await?.is_some_and(|device| device.is_registered()) {
            return Ok(false);
        }
        run_session_inner(sink, stop, runtime, store).await?;
        Ok(true)
    })
}

type Callback = extern "C" fn(u32, *const u8, usize, u64, usize);

struct ForeignSink {
    callback: Callback,
    context: usize,
}

impl NoticeSink for ForeignSink {
    /// `notice`: Borrowed event; QR data remains valid only during the call.
    fn notify(&self, notice: Notice<'_>) {
        let (kind, payload, seconds) = match notice {
            Notice::PairingQr { payload, valid_for } => (1, payload.as_bytes(), valid_for.as_secs()),
            Notice::PairingExpired => (2, &[][..], 0),
            Notice::Connected => (3, &[][..], 0),
            Notice::Disconnected => (4, &[][..], 0),
            Notice::LoggedOut => (5, &[][..], 0),
            Notice::IncomingMessage => (6, &[][..], 0),
            Notice::UndecryptableMessage => (7, &[][..], 0),
        };
        let data = if payload.is_empty() { std::ptr::null() } else { payload.as_ptr() };
        (self.callback)(kind, data, payload.len(), seconds, self.context);
    }
}

/// Reserve one session before the caller starts its executor task.
#[no_mangle]
pub extern "C" fn cody_whatsapp_prepare() -> i32 {
    let Ok(mut slot) = SESSION.lock() else { return -1; };
    if slot.is_some() { return -1; }
    let (stop, receiver) = async_channel::bounded(1);
    *slot = Some(Reservation { stop, receiver: Some(receiver) });
    0
}

/// Release an admitted reservation only when the task was not started.
#[no_mangle]
pub extern "C" fn cody_whatsapp_release_unstarted() {
    if let Ok(mut slot) = SESSION.lock() {
        if slot.as_ref().is_some_and(|session| session.receiver.is_some()) { *slot = None; }
    }
}

/// Request a graceful disconnect without revoking or erasing the paired device.
#[no_mangle]
pub extern "C" fn cody_whatsapp_stop() -> i32 {
    let Ok(slot) = SESSION.lock() else { return -1; };
    let Some(session) = slot.as_ref() else { return -1; };
    match session.stop.try_send(()) {
        Ok(()) | Err(async_channel::TrySendError::Full(_)) => 0,
        Err(async_channel::TrySendError::Closed(_)) => -1,
    }
}

/// `callback`: Nonblocking receiver retained for the duration of this call.
/// `context`: Opaque caller-owned value retained until the call returns.
/// `resume_only`: Do not start a pairing flow when no registration exists.
/// Returns -1 for invalid admission, -2 for a session error, 1 when no
/// registered session exists in resume-only mode, or 0 after orderly shutdown.
/// The owner must run this on an executor-sized task.
#[no_mangle]
pub extern "C" fn cody_whatsapp_run(callback: Option<Callback>, context: usize, resume_only: bool) -> i32 {
    let Some(callback) = callback else {
        cody_whatsapp_release_unstarted();
        return -1;
    };
    let stop = {
        let Ok(mut slot) = SESSION.lock() else { return -1; };
        let Some(session) = slot.as_mut() else { return -1; };
        let Some(receiver) = session.receiver.take() else { return -1; };
        receiver
    };
    esp_idf_svc::sys::link_patches();
    let sink = Arc::new(ForeignSink { callback, context });
    let result = match run_session(sink, stop, resume_only) {
        Ok(true) => 0,
        Ok(false) => 1,
        Err(_) => -2,
    };
    if let Ok(mut slot) = SESSION.lock() { *slot = None; }
    result
}
