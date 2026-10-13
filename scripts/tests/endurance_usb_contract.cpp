// Actual USB command parser/dispatcher with fake registered readers and TX boundary.
#include "LUNASHELL_logger.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <initializer_list>
using std::atomic_load_explicit;
using std::memory_order_acquire;
constexpr size_t LOG_USB_BLOCK_BYTES = 128;
std::atomic<bool> s_reader_ready{true}, s_endurance_reader_ready{true};
unsigned sd_reads = 0, endurance_reads = 0;
size_t returned_length = 128;
esp_err_t reader_error = ESP_OK;
std::string response;

// condition: Required result from the production parser/dispatcher.
// message: Contract failure emitted before a nonzero exit.
void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
// text: Complete bounded response from the real formatter.
esp_err_t send_response(const char *text) { response = text; return ESP_OK; }
// buffer: Transport-owned destination, never a storage buffer.
// capacity: Exact receiver block capacity.
// written: Injected owner length, including rejection scenarios.
esp_err_t read_endurance(void *buffer, size_t capacity, size_t *written)
{
    ++endurance_reads;
    require(capacity == 128, "receiver changed endurance block capacity");
    if (reader_error != ESP_OK) { return reader_error; }
    std::memset(buffer, 0x5a, capacity); *written = returned_length; return ESP_OK;
}
// file: Existing typed SD identity; arbitrary paths are never forwarded.
// offset: Existing log block position.
// expected_revision: Existing pinned log revision.
// buffer: Destination block.
// capacity: Destination capacity.
// written: Actual successful SD bytes.
// total: Actual file length.
// revision: Actual file identity.
// native_error: Original filesystem error.
esp_err_t read_sd(LUNASHELL_logger_log_file_t file, uint32_t offset, uint32_t expected_revision,
                 void *buffer, size_t capacity, size_t *written, uint32_t *total,
                 uint32_t *revision, int *native_error)
{
    (void)file; (void)offset; (void)expected_revision; (void)buffer; (void)capacity;
    ++sd_reads; *written = 0; *total = 0; *revision = 4; *native_error = 0; return ESP_OK;
}
LUNASHELL_logger_sd_reader_t s_sd_reader = read_sd;
LUNASHELL_logger_endurance_reader_t s_endurance_reader = read_endurance;
// input: Mutable line on the first call, null for subsequent tokens.
// delimiters: Existing protocol separators.
// context: Caller-owned token cursor; portable stand-in for libc strtok_r only.
char *host_strtok_r(char *input, const char *delimiters, char **context)
{
    char *cursor = input == nullptr ? *context : input;
    cursor += std::strspn(cursor, delimiters);
    if (*cursor == '\0') { *context = cursor; return nullptr; }
    char *end = cursor + std::strcspn(cursor, delimiters);
    if (*end != '\0') { *end++ = '\0'; }
    *context = end; return cursor;
}
#define strtok_r host_strtok_r
#include "endurance_usb_under_test.inc"

// text: Literal command fixture; never sent to hardware.
void command(const char *text)
{
    char buffer[112]; require(std::strlen(text) < sizeof(buffer), "fixture command overflow");
    std::strcpy(buffer, text); response.clear(); process_command(buffer);
}
// No parameters: source admission, exact capacity, original errors and SD isolation.
int main()
{
    command("LUNA LOG READ 0123abcd endurance 0 0");
    require(response.find("LUNA LOG DATA 0123abcd 1 0 128 ") == 0 &&
            endurance_reads == 1 && sd_reads == 0 && response.size() < 384,
            "single-frame endurance route or transport bounds changed");
    for (const char *invalid : {"LUNA LOG READ 0123abcd endurance 1 0",
                               "LUNA LOG READ 0123abcd endurance 0 1",
                               "LUNA LOG READ 0123abcd endurance 0 0 extra",
                               "LUNA LOG READ 0123abcd arbitrary 0 0"}) {
        command(invalid);
        require(response.find("LUNA LOG ERROR 0123abcd ") == 0 && endurance_reads == 1 && sd_reads == 0,
                "invalid source request reached an owner");
    }
    command("LUNA LOG READ invalid! endurance 0 0");
    require(response.empty() && endurance_reads == 1, "invalid token reached owner or response");
    reader_error = ESP_FAIL;
    command("LUNA LOG READ 0123abcd endurance 0 0");
    require(response == "LUNA LOG ERROR 0123abcd -1 0\n", "original reader error lost");
    reader_error = ESP_OK;
    for (size_t invalid_length : {size_t{0}, size_t{127}, size_t{129}}) {
        returned_length = invalid_length;
        command("LUNA LOG READ 0123abcd endurance 0 0");
        require(response.find("LUNA LOG ERROR ") == 0, "invalid callback length formatted as data");
    }
    s_endurance_reader_ready = false;
    command("LUNA LOG READ 0123abcd endurance 0 0");
    require(response == "LUNA LOG ERROR 0123abcd " + std::to_string(ESP_ERR_NOT_SUPPORTED) + " 0\n" && sd_reads == 0,
            "missing endurance owner silently fell back to SD");
    command("LUNA LOG READ 0123abcd previous 0 0");
    require(sd_reads == 1 && response.find("LUNA LOG DATA 0123abcd 4 0 0 ") == 0,
            "existing SD source stopped using its own reader");
    std::puts("Endurance USB production parser contracts passed");
    return 0;
}
