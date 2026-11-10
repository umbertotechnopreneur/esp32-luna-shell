/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_muse_frame.hpp"

#include <algorithm>

namespace CODY {

inline constexpr uint64_t kMuseShowcaseWakeMs = 1500;
inline constexpr uint64_t kMuseShowcaseIntervalMs = 10000;

struct MuseShowcasePose {
    PixelAvatarPose pose;
    uint32_t pose_ms;
    bool working;
};

// Artistic playback owned by the display worker. It has no authority over
// domain work, microphone, audio, power, or user attention.
class MuseShowcase {
public:
    // seed: Artistic sequence seed supplied by the current preview or boot owner.
    void reset(uint32_t seed)
    {
        seed_ = seed != 0 ? seed : kDefaultSeed;
        rng_ = seed_;
        available_ = kAll;
        index_ = kIdle;
        started_ms_ = kMuseShowcaseWakeMs;
        next_ms_ = kMuseShowcaseIntervalMs;
        last_ms_ = 0;
    }

    // visible_ms: Elapsed mascot-visible time. Hidden and standby time is excluded.
    MuseShowcasePose sample(uint64_t visible_ms)
    {
        if (visible_ms < last_ms_) reset(seed_);
        last_ms_ = visible_ms;
        if (visible_ms < kMuseShowcaseWakeMs) {
            return {PixelAvatarPose::Waking, static_cast<uint32_t>(visible_ms), false};
        }
        if (visible_ms >= next_ms_) {
            select_next();
            // Keep the chosen state on screen for a full slot even if a frame
            // was delayed. Do not replay changes the user could not see.
            started_ms_ = visible_ms;
            next_ms_ = visible_ms + kMuseShowcaseIntervalMs;
        }
        const auto &entry = kEntries[index_];
        uint32_t pose_ms = static_cast<uint32_t>(visible_ms - started_ms_);
        if (entry.pose == PixelAvatarPose::Sleep) {
            // The real power-off pose fades fully away. Hold its closed-eye
            // stage here so the sleeping character remains visible.
            pose_ms = std::min(pose_ms, uint32_t{1020});
        }
        return {entry.pose, pose_ms, entry.working};
    }

private:
    struct Entry { PixelAvatarPose pose; bool working; };
    static constexpr std::array<Entry, 8> kEntries{{
        {PixelAvatarPose::Idle, false}, {PixelAvatarPose::Listening, false},
        {PixelAvatarPose::Thinking, false}, {PixelAvatarPose::Thinking, true},
        {PixelAvatarPose::Reply, false}, {PixelAvatarPose::Error, false},
        {PixelAvatarPose::Sleep, false}, {PixelAvatarPose::Waking, false},
    }};
    static constexpr uint8_t kIdle = 0;
    static constexpr uint16_t kAll = (uint16_t{1} << kEntries.size()) - 1;
    static constexpr uint32_t kDefaultSeed = 0xa341316c;

    // No parameters: draws from the current bag without repeating the last pose.
    void select_next()
    {
        if (available_ == 0) available_ = kAll;
        const auto last = uint16_t{1} << index_;
        const auto choices = static_cast<uint16_t>(available_ & ~last);
        uint8_t count = 0;
        for (uint8_t i = 0; i < kEntries.size(); ++i) {
            if ((choices & (uint16_t{1} << i)) != 0) ++count;
        }
        // A new bag contains all entries and the previous selection was
        // removed from the old bag, so at least one choice always remains.
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        auto pick = rng_ % count;
        for (uint8_t i = 0; i < kEntries.size(); ++i) {
            const auto bit = uint16_t{1} << i;
            if ((choices & bit) == 0) continue;
            if (pick != 0) {
                --pick;
                continue;
            }
            index_ = i;
            available_ &= ~bit;
            return;
        }
    }

    uint64_t started_ms_ = kMuseShowcaseWakeMs;
    uint64_t next_ms_ = kMuseShowcaseIntervalMs;
    uint64_t last_ms_ = 0;
    uint32_t seed_ = kDefaultSeed;
    uint32_t rng_ = kDefaultSeed;
    uint16_t available_ = kAll;
    uint8_t index_ = kIdle;
};

} // namespace CODY
