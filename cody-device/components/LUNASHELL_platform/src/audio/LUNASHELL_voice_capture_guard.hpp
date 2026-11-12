#pragma once

#include <mutex>

namespace LUNASHELL {
// No parameters: the AFE feed and codec gain-control owner serialize one RX operation.
// A gain operation must never wait on the audio mutex while holding this guard.
std::mutex &voice_capture_guard();
}
