/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// Compile the actual owner, including admission and completion handling. The
// deterministic dispatcher below models the executor boundary, not ESP32 timing.
#include "../../../components/LUNASHELL_platform/src/runtime/LUNASHELL_settings.cpp"

namespace fixture {
enum class Operation { FlashInit, Open, Close, Read, Write, EraseKey, Erase, Commit, Find, Release };
constexpr nvs_handle_t kUserHandle = 7;
int iterator_token = 0;
bool worker_dispatch = false;
bool watch_active = false;
bool reject_notification = false;
bool force_residual = false;
bool contradictory_not_found = false;
esp_err_t erase_error = ESP_OK;
esp_err_t commit_error = ESP_OK;
esp_err_t find_error = ESP_OK;
esp_err_t watch_error = ESP_OK;
std::map<std::string, uint64_t> staged;
std::map<std::string, uint64_t> persisted;
std::map<std::string, uint64_t> foreign_namespace{{"preserve", 429U}};
std::vector<Operation> operations;

// handle: Only the dedicated settings namespace may be addressed.
// operation: Exact fake SDK operation admitted by the real worker dispatcher.
// Exception: Aborts for caller-side NVS use or any foreign namespace handle.
void observe(nvs_handle_t handle, Operation operation)
{
    assert(worker_dispatch && handle == kUserHandle);
    operations.push_back(operation);
}

// handle: Verified dedicated user namespace handle.
// key: Original public settings key, never an unrestricted namespace name.
// value: Scalar input used only to seed the host model.
// Exception: Assertions reject calls bypassing the owner executor.
template <typename Value>
esp_err_t write(nvs_handle_t handle, const char *key, Value value)
{
    observe(handle, Operation::Write);
    staged[key] = static_cast<uint64_t>(value);
    return ESP_OK;
}

// handle: Verified dedicated user namespace handle.
// key: Original key requested by the real settings owner.
// value: Output remains untouched on genuine absence.
// Exception: Returns NOT_FOUND only for an absent entry in the fake namespace.
template <typename Value>
esp_err_t read(nvs_handle_t handle, const char *key, Value *value)
{
    observe(handle, Operation::Read);
    const auto entry = staged.find(key);
    if (entry == staged.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *value = static_cast<Value>(entry->second);
    return ESP_OK;
}

// No parameters: isolate cases as separate boots without exposing an unseal API.
// Exception: Aborts if a prior operation left a request or watchdog unresolved.
void new_case()
{
    assert(!worker_dispatch && !watch_active);
    assert(LUNASHELL::s_executor_state.load() == LUNASHELL::SettingsExecutorState::Ready);
    LUNASHELL::s_factory_reset_sealed.store(false);
    erase_error = commit_error = find_error = watch_error = ESP_OK;
    reject_notification = force_residual = contradictory_not_found = false;
    staged = {{"cody_firstboot", 1U}, {"cody_sounds", 0U}, {"ccp_pairing", 55U}};
    persisted = staged;
    operations.clear();
}

// settings: Real singleton whose public write methods must remain sealed.
// Exception: Aborts if any writer succeeds or reaches NVS after reset admission.
void assert_writes_sealed(LUNASHELL::UserSettingsService &settings)
{
    const auto prior_operations = operations;
    const uint8_t blob = 42;
    assert(settings.setBool("bool", true) == ESP_ERR_INVALID_STATE);
    assert(settings.setInt32("i32", -12) == ESP_ERR_INVALID_STATE);
    assert(settings.setUInt32("u32", 12) == ESP_ERR_INVALID_STATE);
    assert(settings.setInt64("i64", -12) == ESP_ERR_INVALID_STATE);
    assert(settings.setUInt64("u64", 12) == ESP_ERR_INVALID_STATE);
    assert(settings.setFloat("float", 1.0F) == ESP_ERR_INVALID_STATE);
    assert(settings.setString("text", "default") == ESP_ERR_INVALID_STATE);
    assert(settings.setBlob("blob", &blob, sizeof(blob)) == ESP_ERR_INVALID_STATE);
    assert(settings.eraseKey("cody_sounds") == ESP_ERR_INVALID_STATE);
    assert(settings.clear() == ESP_ERR_INVALID_STATE);
    assert(operations == prior_operations);
}

// settings: Same sealed owner, retried only through the explicit factory API.
// Exception: Aborts unless retry commits and verifies the isolated namespace.
void assert_explicit_retry(LUNASHELL::UserSettingsService &settings)
{
    erase_error = commit_error = find_error = watch_error = ESP_OK;
    reject_notification = force_residual = contradictory_not_found = false;
    operations.clear();
    assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_OK);
    const std::vector<Operation> expected{Operation::Erase, Operation::Commit, Operation::Find};
    assert(operations == expected);
    assert(staged.empty() && persisted.empty());
    assert(foreign_namespace.at("preserve") == 429U);
    assert_writes_sealed(settings);
}
}  // namespace fixture

// No parameters: initialization is also owned by the executor.
esp_err_t nvs_flash_init()
{
    assert(fixture::worker_dispatch);
    fixture::operations.push_back(fixture::Operation::FlashInit);
    return ESP_OK;
}

// name: Must be the owner namespace; no partition-wide erase API is linked.
// mode: The production read/write mode.
// handle: Receives the only valid settings handle.
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    assert(fixture::worker_dispatch && std::string(name) == "LUNASHELL_user");
    assert(mode == NVS_READWRITE && handle != nullptr);
    fixture::operations.push_back(fixture::Operation::Open);
    *handle = fixture::kUserHandle;
    return ESP_OK;
}

// handle: Destructor closure must remain inside the same executor boundary.
void nvs_close(nvs_handle_t handle)
{
    fixture::observe(handle, fixture::Operation::Close);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Original public value passed to the fake NVS typed setter.
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    return fixture::write(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Original public value passed to the fake NVS typed setter.
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value)
{
    return fixture::write(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Original public value passed to the fake NVS typed setter.
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    return fixture::write(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Original public value passed to the fake NVS typed setter.
esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t value)
{
    return fixture::write(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Original public value passed to the fake NVS typed setter.
esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value)
{
    return fixture::write(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Read output, preserved when the requested key is absent.
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    return fixture::read(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Read output, preserved when the requested key is absent.
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *value)
{
    return fixture::read(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Read output, preserved when the requested key is absent.
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *value)
{
    return fixture::read(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Read output, preserved when the requested key is absent.
esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *value)
{
    return fixture::read(handle, key, value);
}

// handle: Dedicated owner handle.
// key: Scalar setting key.
// value: Read output, preserved when the requested key is absent.
esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *value)
{
    return fixture::read(handle, key, value);
}

// Unnamed handle: Must never be reached by a sealed string write.
// Unnamed key: Public string key, rejected before SDK access.
// Unnamed value: Public string value, rejected before SDK access.
// Exception: Aborts if the write seal was bypassed.
esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *)
{
    std::abort();
}

// Unnamed handle: String reads are outside this fixture's reset coverage.
// Unnamed key: Unused requested string key.
// Unnamed value: Unused string destination.
// Unnamed size: Unused string destination size.
// Exception: Aborts on an unexpected dependency instead of returning fabricated data.
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *)
{
    std::abort();
}

// Unnamed handle: Must never be reached by a sealed blob/float write.
// Unnamed key: Public blob key, rejected before SDK access.
// Unnamed value: Public blob data, rejected before SDK access.
// Unnamed size: Public blob size, rejected before SDK access.
// Exception: Aborts if the write seal was bypassed.
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t)
{
    std::abort();
}

// Unnamed handle: Blob reads are outside this fixture's reset coverage.
// Unnamed key: Unused requested blob key.
// Unnamed value: Unused blob destination.
// Unnamed size: Unused blob destination size.
// Exception: Aborts on an unexpected dependency instead of returning fabricated data.
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *)
{
    std::abort();
}

// handle: Dedicated user namespace, checked at the SDK boundary.
// key: Exact key selected by the caller.
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    fixture::observe(handle, fixture::Operation::EraseKey);
    return fixture::staged.erase(key) != 0 ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}

// handle: Only the dedicated user namespace may be erased.
// Exception: Returns the injected original erase error without mutating the model.
esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    fixture::observe(handle, fixture::Operation::Erase);
    if (fixture::erase_error == ESP_OK) {
        fixture::staged.clear();
    }
    return fixture::erase_error;
}

// handle: Only the dedicated user namespace is committed.
// Exception: Returns the original injected commit failure and retains persistent data.
esp_err_t nvs_commit(nvs_handle_t handle)
{
    fixture::observe(handle, fixture::Operation::Commit);
    if (fixture::commit_error == ESP_OK) {
        fixture::persisted = fixture::staged;
    }
    return fixture::commit_error;
}

// handle: Namespace-scoped SDK enumeration must use the same opened owner handle.
// type: Must inspect every type so blobs and scalar keys cannot survive unnoticed.
// iterator: Models absent, residual, erroneous and contradictory enumeration results.
// Exception: Preserves the injected storage error independently of residual entries.
esp_err_t nvs_entry_find_in_handle(nvs_handle_t handle, nvs_type_t type, nvs_iterator_t *iterator)
{
    fixture::observe(handle, fixture::Operation::Find);
    assert(type == NVS_TYPE_ANY && iterator != nullptr);
    *iterator = nullptr;
    if (fixture::find_error != ESP_OK) {
        return fixture::find_error;
    }
    if (fixture::force_residual || !fixture::persisted.empty() || fixture::contradictory_not_found) {
        *iterator = &fixture::iterator_token;
        return fixture::contradictory_not_found ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

// iterator: Every non-null iterator must be released, including contradictory readback.
void nvs_release_iterator(nvs_iterator_t iterator)
{
    assert(iterator == &fixture::iterator_token);
    fixture::observe(fixture::kUserHandle, fixture::Operation::Release);
}

// size: Bounded production request buffer size.
// capabilities: Must request flash-safe internal byte-addressable memory.
// Exception: Returns the real host allocation result; no hardware allocation is inferred.
void *heap_caps_malloc(size_t size, uint32_t capabilities)
{
    assert(capabilities == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return std::malloc(size);
}

// pointer: Buffer previously allocated by this host heap double, or null.
void heap_caps_free(void *pointer)
{
    std::free(pointer);
}

// worker: Actual settings worker entry, registered but not run as an infinite host loop.
// name: Dedicated settings worker name.
// stack_size: Production static worker stack element count.
// argument: Production worker argument, expected null.
// priority: Existing settings worker priority.
// stack: Existing statically allocated settings stack storage.
// control: Existing statically allocated task control storage.
TaskHandle_t xTaskCreateStatic(void (*worker)(void *), const char *name, uint32_t stack_size,
                              void *argument, UBaseType_t priority, StackType_t *stack,
                              StaticTask_t *control)
{
    assert(worker == LUNASHELL::settings_worker && std::string(name) == "LUNASHELL_nvs");
    assert(stack_size == LUNASHELL::kWorkerStackSize && argument == nullptr);
    assert(priority == LUNASHELL::kWorkerPriority && stack == LUNASHELL::s_worker_stack);
    return control;
}

// task: Existing admitted internal settings executor.
// Exception: Can inject a rejected notification without executing any NVS operation.
BaseType_t xTaskNotifyGive(TaskHandle_t task)
{
    assert(task == LUNASHELL::s_worker && fixture::watch_active && !fixture::worker_dispatch);
    if (fixture::reject_notification) {
        return pdFALSE;
    }
    fixture::worker_dispatch = true;
    const esp_err_t result = LUNASHELL::execute_request(LUNASHELL::s_request);
    LUNASHELL::s_request.result = LUNASHELL_liveness_watch_end(LUNASHELL_LIVENESS_SETTINGS, result);
    LUNASHELL::s_completed_generation.store(LUNASHELL::s_request.generation);
    fixture::worker_dispatch = false;
    return xSemaphoreGive(LUNASHELL::s_completion);
}

// Unnamed clear_on_exit: Infinite worker loop is intentionally never started in this fixture.
// Unnamed wait: No host scheduling or timing is emulated.
// Exception: Aborts on unexpected execution of the production infinite task loop.
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t)
{
    std::abort();
}

// storage: Production completion semaphore's statically allocated control.
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage)
{
    storage->available = false;
    return storage;
}

// semaphore: Completion mailbox, published exactly once per dispatched request.
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(!semaphore->available);
    semaphore->available = true;
    return pdTRUE;
}

// semaphore: Existing completion mailbox with no physical or RTOS timing semantics.
// Unnamed wait: The synchronous host dispatcher has already completed the operation.
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t)
{
    const bool available = semaphore->available;
    semaphore->available = false;
    return available ? pdTRUE : pdFALSE;
}

// point: Must be the settings executor's existing liveness owner.
// Exception: Returns an injected admission failure without taking watch ownership.
extern "C" esp_err_t LUNASHELL_liveness_watch_begin(LUNASHELL_liveness_point_t point)
{
    assert(point == LUNASHELL_LIVENESS_SETTINGS && !fixture::watch_active);
    if (fixture::watch_error == ESP_OK) {
        fixture::watch_active = true;
    }
    return fixture::watch_error;
}

// point: Existing settings executor liveness owner.
// result: Original operation error must survive cleanup unchanged.
extern "C" esp_err_t LUNASHELL_liveness_watch_end(LUNASHELL_liveness_point_t point, esp_err_t result)
{
    assert(point == LUNASHELL_LIVENESS_SETTINGS && fixture::watch_active);
    fixture::watch_active = false;
    return result;
}

// No parameters: exercise only real settings code with deterministic SDK doubles.
// Exception: Assertions terminate on lost errors, erased foreign state or reopened writes.
int main()
{
    using fixture::Operation;
    auto &settings = LUNASHELL::UserSettingsService::instance();
    assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_ERR_INVALID_STATE);
    assert(fixture::operations.empty() && !LUNASHELL::s_factory_reset_sealed.load());
    assert(settings.initialize() == ESP_OK);
    assert((fixture::operations == std::vector<Operation>{Operation::FlashInit, Operation::Open}));

    fixture::new_case();
    assert(settings.setUInt32("before_reset", 92U) == ESP_OK);
    uint32_t value = 0;
    assert(settings.getUInt32("before_reset", &value) == ESP_OK && value == 92U);
    fixture::assert_explicit_retry(settings);
    value = 99U;
    assert(settings.getUInt32("before_reset", &value) == ESP_ERR_NVS_NOT_FOUND && value == 99U);
    fixture::assert_explicit_retry(settings);  // Idempotent explicit reset of an empty namespace.

    fixture::new_case();
    fixture::erase_error = ESP_ERR_TIMEOUT;
    assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_ERR_TIMEOUT);
    assert((fixture::operations == std::vector<Operation>{Operation::Erase}));
    assert(fixture::persisted.size() == 3 && fixture::staged.size() == 3);
    fixture::assert_writes_sealed(settings);
    fixture::assert_explicit_retry(settings);

    fixture::new_case();
    fixture::commit_error = ESP_FAIL;
    assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_FAIL);
    assert((fixture::operations == std::vector<Operation>{Operation::Erase, Operation::Commit}));
    assert(fixture::persisted.size() == 3);
    fixture::assert_writes_sealed(settings);
    fixture::assert_explicit_retry(settings);

    fixture::new_case();
    fixture::find_error = ESP_ERR_NO_MEM;
    assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_ERR_NO_MEM);
    assert((fixture::operations == std::vector<Operation>{Operation::Erase, Operation::Commit, Operation::Find}));
    fixture::assert_writes_sealed(settings);
    fixture::assert_explicit_retry(settings);

    for (bool contradictory : {false, true}) {
        fixture::new_case();
        fixture::force_residual = !contradictory;
        fixture::contradictory_not_found = contradictory;
        assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_ERR_INVALID_RESPONSE);
        assert((fixture::operations == std::vector<Operation>{Operation::Erase, Operation::Commit,
                                                             Operation::Find, Operation::Release}));
        fixture::assert_writes_sealed(settings);
        fixture::assert_explicit_retry(settings);
    }

    fixture::new_case();
    fixture::watch_error = ESP_ERR_TIMEOUT;
    assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_ERR_TIMEOUT);
    assert(fixture::operations.empty());
    fixture::assert_writes_sealed(settings);
    fixture::assert_explicit_retry(settings);

    fixture::new_case();
    fixture::reject_notification = true;
    assert(settings.resetOwnedNamespaceForFactoryReset() == ESP_ERR_INVALID_STATE);
    assert(fixture::operations.empty() && !fixture::watch_active);
    fixture::assert_writes_sealed(settings);
    fixture::assert_explicit_retry(settings);

    std::puts("Factory settings: real owner, scoped erase, failures, readback, sealed writes and explicit retry passed; hardware not accessed.");
    return 0;
}
