// Compiles verbatim production AFE lifecycle helpers with bounded host primitives.
#include "esp_err.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT 0
using TickType_t = uint32_t;
using BaseType_t = int;
constexpr int pdTRUE=1, pdFALSE=0, pdPASS=1;
constexpr TickType_t portMAX_DELAY=std::numeric_limits<TickType_t>::max();
constexpr esp_err_t ESP_ERR_NOT_ALLOWED=0x109;
#define pdMS_TO_TICKS(value) static_cast<TickType_t>(value)
namespace {
constexpr const char *kTag = "AFE-host-fixture";
// tag: Production component identity retained at the host logging boundary.
// format: Original diagnostic format; host tests do not synthesize device logs.
// arguments: Actual production values, consumed so warning checks remain meaningful.
template<typename... Args>
void host_log(const char *tag, const char *format, Args&&... arguments)
{
    (void)tag;
    (void)format;
    ((void)arguments, ...);
}
// condition: Actual production helper outcome required by this fixture.
// message: Failing lifecycle invariant.
void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct HostExit {};
struct Task {
    std::mutex mutex;
    std::condition_variable cv;
    unsigned pending=0, waits=0, gives=0;
    bool stop=false, retired=false, fail_notify=false, suspended=false;
    std::thread *execution=nullptr;
    std::function<void()> before_wait;
};
struct Semaphore {
    std::mutex mutex;
    std::condition_variable cv;
    bool pending=false;
    std::function<void()> before_take;
    unsigned takes=0;
    TickType_t last_timeout=0;
};
using TaskHandle_t=Task*;
using SemaphoreHandle_t=Semaphore*;
thread_local Task *current_task=nullptr;
std::atomic<unsigned> worker_errors{0};
// task: Lifetime-retained fake task; stale notification is a hard fixture failure.
BaseType_t xTaskNotifyGive(TaskHandle_t task)
{
    require(task != nullptr,"null notification handle");
    std::lock_guard<std::mutex> lock(task->mutex);
    require(!task->retired,"notification after task retirement");
    ++task->gives;
    if (task->fail_notify) return pdFALSE;
    ++task->pending;
    task->cv.notify_all();
    return pdPASS;
}
// clear: FreeRTOS clear-all semantics, consumed only when an event is present.
// timeout: Production wait request; parked waits must be indefinite.
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout)
{
    require(current_task != nullptr,"worker context missing");
    auto &task=*current_task;
    auto hook=std::move(task.before_wait);
    if (hook) hook();
    std::unique_lock<std::mutex> lock(task.mutex);
    require(timeout==portMAX_DELAY,"parked worker used periodic polling");
    ++task.waits;
    task.cv.notify_all();
    require(task.cv.wait_for(lock,std::chrono::seconds(3),[&task]{return task.pending || task.stop;}),
            "worker lost wakeup (host watchdog)");
    if (task.stop) throw HostExit{};
    const unsigned result=task.pending;
    task.pending=clear ? 0 : task.pending-1;
    return result;
}
// task: Parent-owned static task being retired after the real cancellation ACK.
void vTaskSuspend(TaskHandle_t task)
{
    std::lock_guard<std::mutex> lock(task->mutex);
    require(!task->retired,"suspending retired task");
    task->suspended=true;
}
// task: Fake deletion joins execution before permitting production handle withdrawal.
void vTaskDelete(TaskHandle_t task)
{
    { std::lock_guard<std::mutex> lock(task->mutex);
      require(task->suspended && task->execution != nullptr,"deletion without parent suspension/ownership");
      task->stop=true; task->cv.notify_all(); }
    task->execution->join();
    task->retired=true;
}
// semaphore: Fake binary event; an already pending edge intentionally coalesces.
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    require(semaphore != nullptr,"null semaphore give");
    std::lock_guard<std::mutex> lock(semaphore->mutex);
    const bool was_pending=semaphore->pending;
    semaphore->pending=true;
    semaphore->cv.notify_all();
    return was_pending ? pdFALSE : pdTRUE;
}
// semaphore: Fake binary event; take never clears an edge before observing it.
// timeout: Remaining real-owner deadline budget, in host milliseconds.
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    require(semaphore != nullptr,"null semaphore take");
    auto hook=std::move(semaphore->before_take);
    if (hook) hook();
    std::unique_lock<std::mutex> lock(semaphore->mutex);
    ++semaphore->takes;
    semaphore->last_timeout=timeout;
    if (!semaphore->cv.wait_for(lock,std::chrono::milliseconds(timeout),[semaphore]{return semaphore->pending;})) return pdFALSE;
    semaphore->pending=false;
    return pdTRUE;
}
// No parameters: real elapsed host time supports production deadline arithmetic.
TickType_t xTaskGetTickCount()
{
    static const auto start=std::chrono::steady_clock::now();
    return static_cast<TickType_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-start).count());
}
// No parameters: publication timestamp transport, unrelated to audio content.
int64_t esp_timer_get_time() { return int64_t(xTaskGetTickCount())*1000; }
std::atomic_bool s_running{false},s_start_released{false},s_start_cancelled{false};
std::atomic_bool s_feed_start_cancelled_ack{false},s_standby_requested{false};
std::atomic_bool s_feed_paused{false},s_fetch_paused{false},s_initialization_attempted{false};
std::atomic<esp_err_t> s_initialization_result{ESP_OK};
std::atomic<TaskHandle_t> s_feed_task_handle{nullptr},s_fetch_task_handle{nullptr};
std::atomic<SemaphoreHandle_t> s_lifecycle_change{nullptr};
std::mutex s_worker_mutex,s_publication_mutex,s_standby_mutex;
Semaphore reference;
constexpr TickType_t kReferenceLockTimeout=10;
constexpr TickType_t kTaskCancellationTimeout=1000;
bool s_audio_suspended=false;
size_t s_reference_read=0,s_reference_write=0,s_reference_count=0;
esp_err_t injected_audio_error=ESP_OK;
int audio_calls=0,reset_calls=0;
struct Afe {
    // unused: Fake AFE buffer owner boundary; production calls this before resume.
    int reset_buffer(void *unused) { (void)unused; ++reset_calls; return 0; }
} afe;
Afe *s_afe_handle=&afe;
void *s_afe_data=nullptr;
// standby: Audio operation under test; retain the requested owner's real injected cause.
esp_err_t LUNASHELL_audio_set_standby(bool standby) { (void)standby; ++audio_calls; return injected_audio_error; }
// No parameters: fake reference mutex, initialized available by each scenario.
SemaphoreHandle_t reference_mutex() { return &reference; }
// samples: Unused PCM transport in a lifecycle-only fixture.
// frames: Unused count; no fake audio samples are generated.
// time: Original lifecycle time.
// error: Unavailable microphone state after actual pause admission.
void publish_microphones(const int16_t *samples,size_t frames,int64_t time,esp_err_t error)
{ (void)samples; (void)frames; (void)time; (void)error; }
}
namespace LUNASHELL {
enum class VoiceState { Listening,Paused,Failed };
class DeviceSnapshotService {
public:
    // No parameters: canonical report transport only, not a fake power policy.
    static DeviceSnapshotService &instance() { static DeviceSnapshotService instance; return instance; }
    // state: Actual production lifecycle decision.
    // error: Actual production failure.
    // time: Production detection time, zero in lifecycle-only scenarios.
    esp_err_t reportVoice(VoiceState state,esp_err_t error,int64_t time)
    { (void)state; (void)error; (void)time; return ESP_OK; }
};
namespace snapshot_detail {
// name: Unused publication label.
// error: Publication transport status; no failures are injected into this independent boundary.
void log_snapshot_publication_failure(const char *name,esp_err_t error) { (void)name; require(error==ESP_OK,"publication failed"); }
}
}
namespace {
#include "afe_lifecycle_under_test.inc"
// task: Fake handle kept alive through join and explicit retirement.
// body: Calls the extracted production lifecycle, never a mirrored state machine.
std::thread launch(Task &task,std::function<void()> body)
{
    return std::thread([&task,body]{current_task=&task; try { body(); } catch(const HostExit&) {} catch(...) { ++worker_errors; }});
}
// task: Worker whose actual indefinite wait must be observed.
// count: Expected minimum wait entries, including spurious notification rechecks.
void awaiting(Task &task,unsigned count=1)
{
    std::unique_lock<std::mutex> lock(task.mutex);
    require(task.cv.wait_for(lock,std::chrono::seconds(3),[&task,count]{return task.waits>=count;}),"worker failed to park");
}
// task: Fake retained static task.
// thread: Host execution to retire before marking the handle inaccessible.
void retire(Task &task,std::thread &thread)
{
    { std::lock_guard<std::mutex> lock(task.mutex); task.stop=true; task.cv.notify_all(); }
    thread.join();
    std::lock_guard<std::mutex> lock(s_worker_mutex);
    if (s_feed_task_handle==&task) s_feed_task_handle=nullptr;
    if (s_fetch_task_handle==&task) s_fetch_task_handle=nullptr;
    task.retired=true;
}
// event: New empty ACK event for one isolated lifecycle scenario.
void reset(Semaphore &event)
{
    s_running=true; s_start_released=false; s_start_cancelled=false; s_feed_start_cancelled_ack=false;
    s_standby_requested=false; s_feed_paused=false; s_fetch_paused=false;
    s_initialization_attempted=true; s_initialization_result=ESP_OK; s_lifecycle_change=&event;
    s_feed_task_handle=nullptr; s_fetch_task_handle=nullptr; injected_audio_error=ESP_OK; audio_calls=0; reset_calls=0;
    reference.pending=true; s_audio_suspended=false;
}
// No parameters: deterministic event-at-the-boundary injection exercises lost-wakeup handling.
void pending_and_start()
{
    Semaphore event; reset(event);
    event.before_take=[] { signal_lifecycle_change(); };
    wait_lifecycle_change(xTaskGetTickCount(),100);
    require(event.takes==1 && !event.pending,"pending-before-wait ACK was lost");
    signal_lifecycle_change(); signal_lifecycle_change();
    wait_lifecycle_change(xTaskGetTickCount(),100);
    require(!event.pending,"binary coalescing was not consumed");
    const auto takes=event.takes;
    wait_lifecycle_change(xTaskGetTickCount()-100,50);
    require(event.takes==takes,"expired deadline received a fresh timeout budget");
    Task feed; s_feed_task_handle=&feed;
    feed.before_wait=[] { s_start_released=true; notify_workers("release-before-wait",false); };
    bool admitted=false;
    auto thread=launch(feed,[&admitted] { admitted=wait_worker_start(true); });
    thread.join();
    require(admitted && feed.waits==1,"startup notification before wait was lost");
    s_feed_task_handle=nullptr;
}
// failure: Inject a real audio suspend error to exercise production rollback.
void pause_resume(bool failure)
{
    Semaphore event; reset(event); s_start_released=true;
    Task feed,fetch; s_feed_task_handle=&feed; s_fetch_task_handle=&fetch;
    std::thread feed_thread,fetch_thread;
    // Owner's first ACK take starts workers after its pause predicate is published.
    event.before_take=[&] {
        feed_thread=launch(feed,[] { park_paused_worker(s_feed_paused); });
        fetch_thread=launch(fetch,[] { park_paused_worker(s_fetch_paused); });
    };
    injected_audio_error=failure ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
    const auto result=apply_afe_standby_locked(true);
    require(result==(failure ? ESP_ERR_INVALID_RESPONSE : ESP_OK),"audio suspend cause lost");
    if (!failure) {
        awaiting(feed); awaiting(fetch);
        const auto count=fetch.waits;
        xTaskNotifyGive(&fetch);
        awaiting(fetch,count+1);
        require(s_feed_paused && s_fetch_paused,"spurious event resumed a paused worker");
        require(apply_afe_standby_locked(false)==ESP_OK && reset_calls==1,"resume did not reset AFE and release both workers");
    }
    feed_thread.join(); fetch_thread.join();
    require(!s_feed_paused && !s_fetch_paused && !s_standby_requested,"pause/resume rollback stranded worker");
    require(feed.gives>0 && fetch.gives>0,"resume/rollback failed to notify both workers");
    s_feed_task_handle=nullptr; s_fetch_task_handle=nullptr;
}
// No parameters: terminal publication must wake both parked workers without the owner mutex.
void terminal_and_cancel()
{
    Semaphore event; reset(event); s_start_released=true; s_standby_requested=true;
    Task feed,fetch; s_feed_task_handle=&feed; s_fetch_task_handle=&fetch;
    auto a=launch(feed,[] { park_paused_worker(s_feed_paused); park_finished_worker(); });
    auto b=launch(fetch,[] { park_paused_worker(s_fetch_paused); park_finished_worker(); });
    awaiting(feed); awaiting(fetch);
    { std::lock_guard<std::mutex> owner(s_standby_mutex); publish_voice(LUNASHELL::VoiceState::Failed,ESP_FAIL); }
    awaiting(feed,2); awaiting(fetch,2);
    require(!s_running && !s_feed_paused && !s_fetch_paused,"terminal failure left a worker paused");
    require(s_feed_task_handle==&feed && s_fetch_task_handle==&fetch,"terminal parking invalidated retained static handles");
    retire(feed,a); retire(fetch,b);
    require(notify_workers("retired",false)==ESP_OK && notify_workers("required",true)==ESP_ERR_INVALID_STATE,
        "retired handles were reused or missing handles hidden");
    Semaphore cancelled; reset(cancelled); s_running=false;
    Task startup; s_feed_task_handle=&startup;
    auto worker=launch(startup,[] { (void)wait_worker_start(true); require(false,"cancelled startup reached AFE"); });
    awaiting(startup);
    startup.execution=&worker;
    require(cancel_feed_start(&startup)==ESP_OK,"production startup cancellation failed");
    require(s_feed_start_cancelled_ack && startup.suspended && startup.retired && s_feed_task_handle==nullptr,
        "startup cancellation skipped acknowledged suspend/delete/handle withdrawal");
    require(notify_workers("after-cancel",false)==ESP_OK,"cancel left a stale notification handle");
}
// No parameters: rollback must close audio only after both real worker ACKs.
void partial_resume_rollback()
{
    Semaphore event; reset(event); s_start_released=true;
    Task feed,fetch; s_feed_task_handle=&feed; s_fetch_task_handle=&fetch;
    std::thread a,b;
    event.before_take=[&] {
        require(audio_calls==0,"audio closed before rollback pause acknowledgments");
        a=launch(feed,[] { park_paused_worker(s_feed_paused); });
        b=launch(fetch,[] { park_paused_worker(s_fetch_paused); });
    };
    require(rollback_afe_resume()==ESP_OK && s_feed_paused && s_fetch_paused && audio_calls==1,
        "partial resume rollback did not park both workers before audio suspension");
    retire(feed,a); retire(fetch,b);
    Semaphore failed; reset(failed); Task first,second;
    first.fail_notify=true; s_feed_task_handle=&first; s_fetch_task_handle=&second;
    require(rollback_afe_resume()==ESP_FAIL && audio_calls==0 && second.gives==1,
        "uncertain rollback closed audio or failed to notify the second worker");
    s_feed_task_handle=nullptr; s_fetch_task_handle=nullptr;
}
// No parameters: an incomplete pause never closed audio and must not reset an active fetch.
void incomplete_pause_rollback()
{
    Semaphore event;
    reset(event);
    s_start_released=true;
    s_standby_requested=true;
    Task feed,fetch;
    s_feed_task_handle=&feed;
    s_fetch_task_handle=&fetch;
    auto thread=launch(feed,[] { park_paused_worker(s_feed_paused); });
    awaiting(feed);
    require(s_feed_paused && !s_fetch_paused && !s_audio_suspended,
        "fixture did not establish partially acknowledged pause");
    require(apply_afe_standby_locked(false)==ESP_OK,
        "rollback of incomplete pause did not restore admission");
    thread.join();
    require(audio_calls==0 && reset_calls==0 && !s_feed_paused && !s_standby_requested,
        "incomplete-pause rollback reset an active AFE or reopened live audio");
    require(feed.gives>0 && fetch.gives>0,"incomplete-pause rollback did not wake both workers");
    s_feed_task_handle=nullptr;
    s_fetch_task_handle=nullptr;
}
// No parameters: a first notification failure must not prevent waking the second worker.
void notification_failure()
{
    Semaphore event; reset(event); Task feed,fetch;
    feed.fail_notify=true; s_feed_task_handle=&feed; s_fetch_task_handle=&fetch;
    require(notify_workers("fault",true)==ESP_FAIL && fetch.gives==1,"first failure suppressed second notification");
    s_feed_task_handle=nullptr; s_fetch_task_handle=nullptr;
}
}
// No parameters: finite fixture watchdogs do not change production wait arguments.
int main()
{
    pending_and_start(); pause_resume(false); pause_resume(true); terminal_and_cancel(); partial_resume_rollback(); incomplete_pause_rollback(); notification_failure();
    require(worker_errors==0,"worker threw an unexpected exception");
    std::puts("PASS real AFE startup/pause/ACK/resume/rollback/terminal helpers; host primitive scheduling only");
}
