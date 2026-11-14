// Host-only persistent NVS adapter for the unchanged firmware settings owner.
// No ESP partition, physical device or installed-application storage is opened.
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

struct nvs_opaque_iterator_t { bool present; };

namespace {
constexpr const char *kTag = "CODY_host_nvs";
constexpr size_t kMaxFile = 4U * 1024U * 1024U;
constexpr size_t kMaxValue = 1024U * 1024U;
constexpr size_t kMaxEntries = 4096;
constexpr uint8_t kMagic[] = {'C','O','D','Y','N','V','S',1};
struct Value { nvs_type_t type{}; std::vector<uint8_t> bytes; };
using Entries = std::map<std::string, Value>;
using Database = std::map<std::string, Entries>;
struct Mutation { bool erased = false; Value value{}; };
struct Handle { std::string name; bool readonly = false; bool clear = false; std::map<std::string, Mutation> pending; };
std::mutex s_mutex;
std::filesystem::path s_workspace_root;
std::filesystem::path s_simulator_root;
std::filesystem::path s_path;
bool s_initialized = false;
bool s_file_existed = false;
Database s_data;
std::map<nvs_handle_t, Handle> s_handles;
nvs_handle_t s_next_handle = 1;
std::atomic<uint64_t> s_temp_sequence{0};

// path: Candidate canonical parent or file path.
// root: Existing canonical directory that must contain every path component.
bool beneath(const std::filesystem::path &path, const std::filesystem::path &root)
{
    auto child = path.begin();
    for (auto base = root.begin(); base != root.end(); ++base, ++child)
        if (child == path.end() || *child != *base) return false;
    return child != path.end();
}

// No parameters: revalidate the configured containment and reject link/device targets.
// Exception: Returns invalid state on a moved root or unsafe destination.
esp_err_t validate_path()
{
    if (s_workspace_root.empty() || s_simulator_root.empty() || s_path.empty()) return ESP_ERR_INVALID_STATE;
    std::error_code ec;
    const auto workspace = std::filesystem::canonical(s_workspace_root, ec);
    if (ec || workspace != s_workspace_root) return ESP_ERR_INVALID_STATE;
    const auto simulator = std::filesystem::canonical(workspace / "artifacts" / "simulator", ec);
    if (ec || simulator != s_simulator_root || simulator != (workspace / "artifacts" / "simulator").lexically_normal())
        return ESP_ERR_INVALID_STATE;
    const auto parent = std::filesystem::canonical(s_path.parent_path(), ec);
    if (ec || (parent != simulator && !beneath(parent, simulator))) return ESP_ERR_INVALID_STATE;
    if (s_path.extension() != ".nvs") return ESP_ERR_INVALID_ARG;
    const auto state = std::filesystem::symlink_status(s_path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return ESP_FAIL;
    if (!ec && (std::filesystem::is_symlink(state) ||
        (std::filesystem::exists(state) && !std::filesystem::is_regular_file(state)))) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

// left: First fully validated committed host snapshot.
// right: Second fully validated committed host snapshot.
bool same_database(const Database &left, const Database &right)
{
    if (left.size() != right.size()) return false;
    for (const auto &space : left) {
        const auto other = right.find(space.first);
        if (other == right.end() || space.second.size() != other->second.size()) return false;
        for (const auto &entry : space.second) {
            const auto match = other->second.find(entry.first);
            if (match == other->second.end() || match->second.type != entry.second.type ||
                match->second.bytes != entry.second.bytes) return false;
        }
    }
    return true;
}

// value: Immutable byte sequence for a small corruption-detection checksum.
uint64_t checksum(const std::vector<uint8_t> &value)
{
    uint64_t hash = 14695981039346656037ULL;
    for (uint8_t byte : value) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}

// output: Bounded destination buffer.
// value: Integer serialized little-endian independently of host alignment.
void append_u32(std::vector<uint8_t> &output, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) output.push_back(static_cast<uint8_t>(value >> shift));
}

// input: Validated byte buffer.
// position: Cursor advanced only for a complete integer.
// value: Receives decoded little-endian integer.
bool take_u32(const std::vector<uint8_t> &input, size_t &position, uint32_t &value)
{
    if (position > input.size() || input.size() - position < 4) return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(input[position++]) << shift;
    return true;
}

// name: Borrowed NVS key or namespace; the IDF length includes a terminator.
// key: Distinguishes the longer-key error from an invalid namespace.
esp_err_t validate_name(const char *name, bool key)
{
    if (!name || !*name) return ESP_ERR_NVS_INVALID_NAME;
    size_t length = 0;
    while (length < NVS_KEY_NAME_MAX_SIZE && name[length] != '\0') ++length;
    if (length == NVS_KEY_NAME_MAX_SIZE) return key ? ESP_ERR_NVS_KEY_TOO_LONG : ESP_ERR_NVS_INVALID_NAME;
    return ESP_OK;
}

// value: Persisted type identity, independent of local enum implementation.
bool supported_type(uint32_t value)
{
    return value == NVS_TYPE_U8 || value == NVS_TYPE_U32 || value == NVS_TYPE_U64 ||
        value == NVS_TYPE_I32 || value == NVS_TYPE_I64 || value == NVS_TYPE_STR || value == NVS_TYPE_BLOB;
}

// path: Caller-selected regular host file, not a device or ESP partition.
// result: Receives data only after full validation, including the file checksum.
// Exception: Returns the original I/O failure or invalid persisted content.
esp_err_t load_database(const std::filesystem::path &path, Database &result)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return ESP_FAIL;
    if (!exists) { result.clear(); return ESP_OK; }
    if (!std::filesystem::is_regular_file(path, ec) || ec) return ESP_ERR_NVS_INVALID_STATE;
    const auto length = std::filesystem::file_size(path, ec);
    if (ec) return ESP_FAIL;
    if (length < sizeof(kMagic) + 4U + 8U || length > kMaxFile) return ESP_ERR_NVS_INVALID_STATE;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return ESP_FAIL;
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return ESP_FAIL;
    if (!std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) return ESP_ERR_NVS_INVALID_STATE;
    uint64_t stored = 0;
    for (unsigned i = 0; i < 8; ++i) stored |= uint64_t(bytes[bytes.size() - 8 + i]) << (i * 8);
    bytes.resize(bytes.size() - 8);
    if (checksum(bytes) != stored) return ESP_ERR_NVS_INVALID_STATE;
    size_t position = sizeof(kMagic);
    uint32_t count = 0;
    if (!take_u32(bytes, position, count) || count > kMaxEntries) return ESP_ERR_NVS_INVALID_STATE;
    Database decoded;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t ns_length = 0, key_length = 0, type = 0, size = 0;
        if (!take_u32(bytes, position, ns_length) || !take_u32(bytes, position, key_length) ||
            !take_u32(bytes, position, type) || !take_u32(bytes, position, size) ||
            ns_length == 0 || ns_length >= NVS_KEY_NAME_MAX_SIZE || key_length == 0 ||
            key_length >= NVS_KEY_NAME_MAX_SIZE || size > kMaxValue || !supported_type(type) ||
            position > bytes.size() || size_t(ns_length) + key_length + size > bytes.size() - position)
            return ESP_ERR_NVS_INVALID_STATE;
        std::string name(reinterpret_cast<const char *>(bytes.data() + position), ns_length); position += ns_length;
        std::string key(reinterpret_cast<const char *>(bytes.data() + position), key_length); position += key_length;
        if (name.find('\0') != std::string::npos || key.find('\0') != std::string::npos) return ESP_ERR_NVS_INVALID_STATE;
        Value value{static_cast<nvs_type_t>(type), {bytes.begin() + position, bytes.begin() + position + size}};
        position += size;
        if ((type == NVS_TYPE_U8 && size != 1) || ((type == NVS_TYPE_I32 || type == NVS_TYPE_U32) && size != 4) ||
            ((type == NVS_TYPE_I64 || type == NVS_TYPE_U64) && size != 8) ||
            (type == NVS_TYPE_STR && (size == 0 || value.bytes.back() != 0 ||
                std::find(value.bytes.begin(), value.bytes.end() - 1, 0) != value.bytes.end() - 1)))
            return ESP_ERR_NVS_INVALID_STATE;
        if (!decoded[name].emplace(std::move(key), std::move(value)).second) return ESP_ERR_NVS_INVALID_STATE;
    }
    if (position != bytes.size()) return ESP_ERR_NVS_INVALID_STATE;
    result = std::move(decoded);
    return ESP_OK;
}

// database: Complete committed host-owned snapshot, never firmware flash bytes.
// Exception: Writes a distinct staging file and atomically publishes it or preserves the old file.
esp_err_t save_database(const Database &database)
{
    size_t count = 0;
    for (const auto &entry : database) count += entry.second.size();
    if (count > kMaxEntries) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    std::vector<uint8_t> bytes(std::begin(kMagic), std::end(kMagic));
    append_u32(bytes, static_cast<uint32_t>(count));
    for (const auto &space : database) for (const auto &entry : space.second) {
        append_u32(bytes, static_cast<uint32_t>(space.first.size()));
        append_u32(bytes, static_cast<uint32_t>(entry.first.size()));
        append_u32(bytes, static_cast<uint32_t>(entry.second.type));
        append_u32(bytes, static_cast<uint32_t>(entry.second.bytes.size()));
        bytes.insert(bytes.end(), space.first.begin(), space.first.end());
        bytes.insert(bytes.end(), entry.first.begin(), entry.first.end());
        bytes.insert(bytes.end(), entry.second.bytes.begin(), entry.second.bytes.end());
        if (bytes.size() > kMaxFile - 8) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }
    const auto hash = checksum(bytes);
    for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(hash >> (i * 8)));
#ifdef _WIN32
    const auto process = static_cast<unsigned long>(GetCurrentProcessId());
#else
    const auto process = static_cast<unsigned long>(getpid());
#endif
    auto staging = s_path;
    staging += ".tmp." + std::to_string(process) + "." + std::to_string(++s_temp_sequence);
#ifdef _WIN32
    // CREATE_NEW never follows/replaces an existing staging file or link.
    HANDLE file = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return ESP_FAIL;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
    if (!ok || !closed) return ESP_FAIL;
    if (!MoveFileExW(staging.c_str(), s_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return ESP_FAIL;
#else
    // O_EXCL and O_NOFOLLOW preserve any prior staging file or link.
    const int file = open(staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (file < 0) return ESP_FAIL;
    size_t offset = 0;
    while (offset < bytes.size()) {
        const auto written = write(file, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { close(file); return ESP_FAIL; }
        offset += static_cast<size_t>(written);
    }
    const bool ok = fsync(file) == 0;
    const bool closed = close(file) == 0;
    if (!ok || !closed) return ESP_FAIL;
    std::error_code ec;
    std::filesystem::rename(staging, s_path, ec);
    if (ec) return ESP_FAIL;
#endif
    return ESP_OK;
}

// handle: Open host handle, valid only while the mutex is held.
// key: Exact NVS key.
// value: Receives a borrowed entry including uncommitted mutations.
esp_err_t lookup(nvs_handle_t handle, const char *key, const Value *&value)
{
    const auto name_error = validate_name(key, true);
    if (name_error != ESP_OK) return name_error;
    auto found = s_handles.find(handle);
    if (found == s_handles.end()) return ESP_ERR_NVS_INVALID_HANDLE;
    const auto pending = found->second.pending.find(key);
    if (pending != found->second.pending.end()) {
        if (pending->second.erased) return ESP_ERR_NVS_NOT_FOUND;
        value = &pending->second.value;
        return ESP_OK;
    }
    if (found->second.clear) return ESP_ERR_NVS_NOT_FOUND;
    const auto space = s_data.find(found->second.name);
    if (space == s_data.end()) return ESP_ERR_NVS_NOT_FOUND;
    const auto entry = space->second.find(key);
    if (entry == space->second.end()) return ESP_ERR_NVS_NOT_FOUND;
    value = &entry->second;
    return ESP_OK;
}

// handle: Writable host NVS handle.
// key: Exact NVS key.
// type: Firmware NVS type identity.
// data: Borrowed bytes copied before returning.
// size: Byte count, including string terminator.
esp_err_t set_value(nvs_handle_t handle, const char *key, nvs_type_t type, const void *data, size_t size)
{
    const auto error = validate_name(key, true);
    if (error != ESP_OK) return error;
    if ((!data && size) || size > kMaxValue) return size > kMaxValue ? ESP_ERR_NVS_VALUE_TOO_LONG : ESP_ERR_INVALID_ARG;
    auto found = s_handles.find(handle);
    if (found == s_handles.end()) return ESP_ERR_NVS_INVALID_HANDLE;
    if (found->second.readonly) return ESP_ERR_NVS_READ_ONLY;
    Mutation mutation{};
    mutation.value.type = type;
    if (size) {
        const auto *first = static_cast<const uint8_t *>(data);
        mutation.value.bytes.assign(first, first + size);
    }
    found->second.pending[key] = std::move(mutation);
    return ESP_OK;
}

// handle: Open host handle.
// key: Exact NVS key.
// type: Expected firmware type.
// data: Optional destination; null queries the needed size.
// length: Input capacity and output required size.
esp_err_t get_value(nvs_handle_t handle, const char *key, nvs_type_t type, void *data, size_t *length)
{
    const Value *value = nullptr;
    const auto error = lookup(handle, key, value);
    if (error != ESP_OK) return error;
    if (value->type != type) return ESP_ERR_NVS_TYPE_MISMATCH;
    if (!length) return ESP_ERR_NVS_INVALID_LENGTH;
    const auto required = value->bytes.size();
    if (data && *length < required) { *length = required; return ESP_ERR_NVS_INVALID_LENGTH; }
    if (data && required) std::memcpy(data, value->bytes.data(), required);
    *length = required;
    return ESP_OK;
}

// handle, key, type, scalar: Forward one integral value through the same typed store.
template <typename T> esp_err_t set_scalar(nvs_handle_t handle, const char *key, nvs_type_t type, T scalar)
{
    using U = typename std::make_unsigned<T>::type;
    U value = static_cast<U>(scalar);
    uint8_t bytes[sizeof(T)]{};
    for (size_t i = 0; i < sizeof(T); ++i) bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    return set_value(handle, key, type, bytes, sizeof(bytes));
}

// handle, key, type, scalar: Decode the exact stored integral type without aliasing.
template <typename T> esp_err_t get_scalar(nvs_handle_t handle, const char *key, nvs_type_t type, T *scalar)
{
    if (!scalar) return ESP_ERR_INVALID_ARG;
    uint8_t bytes[sizeof(T)]{};
    size_t length = sizeof(bytes);
    const auto error = get_value(handle, key, type, bytes, &length);
    if (error != ESP_OK) return error;
    if (length != sizeof(bytes)) return ESP_ERR_NVS_INVALID_STATE;
    using U = typename std::make_unsigned<T>::type;
    U value = 0;
    for (size_t i = 0; i < sizeof(T); ++i) value |= U(bytes[i]) << (i * 8);
    std::memcpy(scalar, &value, sizeof(value));
    return ESP_OK;
}
} // namespace

// The wrappers never let C++ allocation or filesystem exceptions cross the ESP C ABI.
#define HOST_NVS_GUARD_BEGIN try { std::lock_guard<std::mutex> lock(s_mutex);
#define HOST_NVS_GUARD_END } catch (const std::bad_alloc &) { return ESP_ERR_NO_MEM; } catch (...) { return ESP_FAIL; }

// absolute_workspace_root: Explicit host workspace, not inferred from process cwd.
// Exception: Rejects non-workspaces and simulator roots outside that workspace.
extern "C" esp_err_t cody_host_nvs_set_root(const char *absolute_workspace_root)
{
    HOST_NVS_GUARD_BEGIN
    if (s_initialized || !s_handles.empty()) return ESP_ERR_INVALID_STATE;
    if (!absolute_workspace_root || !*absolute_workspace_root) return ESP_ERR_INVALID_ARG;
    const auto candidate = std::filesystem::u8path(absolute_workspace_root);
    if (!candidate.is_absolute()) return ESP_ERR_INVALID_ARG;
    std::error_code ec;
    const auto workspace = std::filesystem::canonical(candidate, ec);
    if (ec || !std::filesystem::is_regular_file(workspace / "scripts" / "device.ps1", ec) || ec ||
        !std::filesystem::is_directory(workspace / "cody-device" / "simulator" / "native", ec) || ec)
        return ESP_ERR_INVALID_ARG;
    const auto expected = (workspace / "artifacts" / "simulator").lexically_normal();
    const auto simulator = std::filesystem::canonical(expected, ec);
    if (ec || simulator != expected || !beneath(simulator, workspace)) return ESP_ERR_INVALID_STATE;
    s_workspace_root = workspace;
    s_simulator_root = simulator;
    s_path.clear();
    return ESP_OK;
    HOST_NVS_GUARD_END
}

// absolute_path: Explicit simulator file; configuration is frozen after init.
extern "C" esp_err_t cody_host_nvs_set_path(const char *absolute_path)
{
    HOST_NVS_GUARD_BEGIN
    if (s_initialized || !s_handles.empty()) return ESP_ERR_INVALID_STATE;
    if (s_simulator_root.empty()) return ESP_ERR_INVALID_STATE;
    if (!absolute_path || !*absolute_path) return ESP_ERR_INVALID_ARG;
    std::filesystem::path candidate = std::filesystem::u8path(absolute_path);
    if (!candidate.is_absolute() || candidate.filename().empty() || candidate.extension() != ".nvs") return ESP_ERR_INVALID_ARG;
    // Test the candidate first; rejection must not replace a previously safe configuration.
    const auto previous = s_path;
    s_path = candidate.lexically_normal();
    const auto result = validate_path();
    if (result != ESP_OK) s_path = previous;
    return result;
    HOST_NVS_GUARD_END
}

// No parameters: initialize from the configured host file, never repairing corrupt data.
extern "C" esp_err_t nvs_flash_init()
{
    HOST_NVS_GUARD_BEGIN
    if (s_initialized) return ESP_OK;
    const auto validation = validate_path();
    if (validation != ESP_OK) return validation;
    std::error_code ec;
    const bool existed = std::filesystem::exists(s_path, ec);
    if (ec) return ESP_FAIL;
    Database loaded;
    const auto error = load_database(s_path, loaded);
    if (error != ESP_OK) { ESP_LOGE(kTag, "Host NVS initialization failed (%s)", esp_err_to_name(error)); return error; }
    s_data = std::move(loaded);
    s_file_existed = existed;
    s_initialized = true;
    return ESP_OK;
    HOST_NVS_GUARD_END
}

// name: Firmware namespace identity.
// mode: Read-only or read-write access.
// out_handle: Receives a unique process-local host handle.
extern "C" esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out_handle)
{
    HOST_NVS_GUARD_BEGIN
    if (!s_initialized) return ESP_ERR_NVS_NOT_INITIALIZED;
    const auto error = validate_name(name, false);
    if (error != ESP_OK) return error;
    if (!out_handle || (mode != NVS_READONLY && mode != NVS_READWRITE && mode != NVS_READWRITE_PURGE)) return ESP_ERR_INVALID_ARG;
    if (mode == NVS_READONLY && s_data.find(name) == s_data.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (s_next_handle == 0) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    const auto next = s_next_handle++;
    s_handles.emplace(next, Handle{std::string(name), mode == NVS_READONLY, false, {}});
    *out_handle = next;
    return ESP_OK;
    HOST_NVS_GUARD_END
}

// handle: Close only this host handle, discarding its uncommitted mutations.
extern "C" void nvs_close(nvs_handle_t handle)
{
    try { std::lock_guard<std::mutex> lock(s_mutex); s_handles.erase(handle); } catch (...) {}
}

#define DEFINE_SCALAR(name, ctype, nvs_type) \
extern "C" esp_err_t nvs_set_##name(nvs_handle_t handle, const char *key, ctype value) { \
    HOST_NVS_GUARD_BEGIN return set_scalar(handle, key, nvs_type, value); HOST_NVS_GUARD_END } \
extern "C" esp_err_t nvs_get_##name(nvs_handle_t handle, const char *key, ctype *value) { \
    HOST_NVS_GUARD_BEGIN return get_scalar(handle, key, nvs_type, value); HOST_NVS_GUARD_END }
DEFINE_SCALAR(u8, uint8_t, NVS_TYPE_U8)
DEFINE_SCALAR(i32, int32_t, NVS_TYPE_I32)
DEFINE_SCALAR(u32, uint32_t, NVS_TYPE_U32)
DEFINE_SCALAR(i64, int64_t, NVS_TYPE_I64)
DEFINE_SCALAR(u64, uint64_t, NVS_TYPE_U64)
#undef DEFINE_SCALAR

// handle, key, value: Store a copied, terminated string as one pending operation.
extern "C" esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    HOST_NVS_GUARD_BEGIN
    if (!value) return ESP_ERR_INVALID_ARG;
    size_t size = 0;
    while (size < kMaxValue && value[size] != '\0') ++size;
    if (size == kMaxValue) return ESP_ERR_NVS_VALUE_TOO_LONG;
    return set_value(handle, key, NVS_TYPE_STR, value, size + 1);
    HOST_NVS_GUARD_END
}

// handle, key, value, length: Read a string or query its required capacity.
extern "C" esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *length)
{ HOST_NVS_GUARD_BEGIN return get_value(handle, key, NVS_TYPE_STR, value, length); HOST_NVS_GUARD_END }

// handle, key, value, length: Store a copied blob as one pending operation.
extern "C" esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{ HOST_NVS_GUARD_BEGIN return set_value(handle, key, NVS_TYPE_BLOB, value, length); HOST_NVS_GUARD_END }

// handle, key, value, length: Read a blob or query its required capacity.
extern "C" esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length)
{ HOST_NVS_GUARD_BEGIN return get_value(handle, key, NVS_TYPE_BLOB, value, length); HOST_NVS_GUARD_END }

// handle, key: Queue deletion only after verifying that the exact key exists.
extern "C" esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    HOST_NVS_GUARD_BEGIN
    auto found = s_handles.find(handle);
    if (found == s_handles.end()) return ESP_ERR_NVS_INVALID_HANDLE;
    if (found->second.readonly) return ESP_ERR_NVS_READ_ONLY;
    const Value *ignored = nullptr;
    const auto error = lookup(handle, key, ignored);
    if (error != ESP_OK) return error;
    found->second.pending[key] = Mutation{true, {}};
    return ESP_OK;
    HOST_NVS_GUARD_END
}

// handle: Queue a namespace-only erase, retaining all other namespaces.
extern "C" esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    HOST_NVS_GUARD_BEGIN
    auto found = s_handles.find(handle);
    if (found == s_handles.end()) return ESP_ERR_NVS_INVALID_HANDLE;
    if (found->second.readonly) return ESP_ERR_NVS_READ_ONLY;
    found->second.clear = true;
    found->second.pending.clear();
    return ESP_OK;
    HOST_NVS_GUARD_END
}

// handle: Atomically publish this handle's pending namespace mutations.
extern "C" esp_err_t nvs_commit(nvs_handle_t handle)
{
    HOST_NVS_GUARD_BEGIN
    auto found = s_handles.find(handle);
    if (found == s_handles.end()) return ESP_ERR_NVS_INVALID_HANDLE;
    if (found->second.readonly) return ESP_ERR_NVS_READ_ONLY;
    const auto validation = validate_path();
    if (validation != ESP_OK) return validation;
    std::error_code ec;
    const bool exists = std::filesystem::exists(s_path, ec);
    if (ec) return ESP_FAIL;
    if (exists != s_file_existed) return ESP_ERR_NVS_INVALID_STATE;
    Database current;
    const auto read_error = load_database(s_path, current);
    if (read_error != ESP_OK) return read_error;
    if (!same_database(current, s_data)) return ESP_ERR_NVS_INVALID_STATE;
    Database next = s_data;
    auto &space = next[found->second.name];
    if (found->second.clear) space.clear();
    for (const auto &entry : found->second.pending) {
        if (entry.second.erased) space.erase(entry.first);
        else space[entry.first] = entry.second.value;
    }
    if (space.empty()) next.erase(found->second.name);
    const auto error = save_database(next);
    if (error != ESP_OK) { ESP_LOGE(kTag, "Host NVS commit failed (%s)", esp_err_to_name(error)); return error; }
    s_data = std::move(next);
    s_file_existed = true;
    found->second.clear = false;
    found->second.pending.clear();
    return ESP_OK;
    HOST_NVS_GUARD_END
}

// handle: Existing namespace handle.
// type: Exact requested NVS type or NVS_TYPE_ANY.
// iterator: Receives a releasable marker only if at least one matching key exists.
extern "C" esp_err_t nvs_entry_find_in_handle(nvs_handle_t handle, nvs_type_t type, nvs_iterator_t *iterator)
{
    HOST_NVS_GUARD_BEGIN
    if (!iterator) return ESP_ERR_INVALID_ARG;
    *iterator = nullptr;
    const auto found = s_handles.find(handle);
    if (found == s_handles.end()) return ESP_ERR_NVS_INVALID_HANDLE;
    if (type != NVS_TYPE_ANY && !supported_type(type)) return ESP_ERR_INVALID_ARG;
    Entries visible;
    const auto space = s_data.find(found->second.name);
    if (!found->second.clear && space != s_data.end()) visible = space->second;
    for (const auto &entry : found->second.pending) {
        if (entry.second.erased) visible.erase(entry.first);
        else visible[entry.first] = entry.second.value;
    }
    for (const auto &entry : visible) if (type == NVS_TYPE_ANY || entry.second.type == type) {
        *iterator = new nvs_opaque_iterator_t{true};
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
    HOST_NVS_GUARD_END
}

// iterator: Borrowed marker from the matching find operation; null is accepted.
extern "C" void nvs_release_iterator(nvs_iterator_t iterator) { delete iterator; }
