/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#include <chrono>
#include <memory>
#include <thread>

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include "audio/audio_engine.h"

namespace
{
/// SDL's dummy audio driver consumes audio in real time without a sound card
struct dummy_audio
{
    dummy_audio()
    {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        m_ok = SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
    ~dummy_audio() { SDL_QuitSubSystem(SDL_INIT_AUDIO); }
    bool m_ok{};
};

template <typename F>
bool wait_for(F &&condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while(std::chrono::steady_clock::now() < end)
    {
        if(condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
}

TEST_CASE("audio_engine plays a region through SDL")
{
    dummy_audio sdl;
    REQUIRE(sdl.m_ok);

    auto clip = std::make_shared<audio_buffer>();
    clip->m_sample_rate_hz = 16000; // Converted by SDL to the device's rate
    clip->m_samples.assign(16000, 0.1f);

    audio_engine engine;
    const auto devs = engine.get_playback_devices();
    REQUIRE(!devs.empty());
    engine.set_device(devs.front());
    engine.set_audio_clip(clip);

    engine.select_region({0.25, 0.5}, false);
    engine.play();
    CHECK(engine.get_state().playing);

    // A quarter of a second plays, then stops at the end of the region
    CHECK(wait_for([&] { return !engine.get_state().playing; }));
    CHECK(engine.get_state().playhead_s == doctest::Approx(0.5).epsilon(1e-3));

    engine.select_region({0.0, 0.1}, true);
    engine.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(engine.get_state().playing); // Still looping
    engine.stop();
    CHECK(!engine.get_state().playing);

    engine.close();
}
