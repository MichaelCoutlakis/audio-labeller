/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once

#include <memory>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "../model/audio_buffer.h"
#include "../model/types.h"
#include "playback_cursor.h"

/// Plays a mono clip through an SDL audio stream, which converts it to the device's sample rate and
/// format. The stream stays open while a device is selected, so that very short sounds aren't cut
/// off by device start-up.
class audio_engine
{
public:
    audio_engine();
    ~audio_engine();
    audio_engine(const audio_engine &) = delete;
    audio_engine &operator=(const audio_engine &) = delete;

    /// Needs SDL's audio subsystem to be initialised. The first entry is the system default, which
    /// follows changes to the default device.
    std::vector<audio_dev> get_playback_devices() const;

    void set_device(const audio_dev &device);

    void set_audio_clip(std::shared_ptr<const audio_buffer> clip);

    void select_region(time_span t, bool loop);
    void set_playback_loop(bool loop);

    void play();
    void stop();

    playback_state get_state() const;

    /// Release the device. Must be called before SDL is shut down.
    void close();

private:
    static void SDLCALL feed(void *user_data, SDL_AudioStream *stream, int additional_bytes, int);

    SDL_AudioSpec clip_spec() const;

    /// The stream's lock guards the members below. SDL holds it while calling feed().
    class stream_lock;

    SDL_AudioStream *m_stream{nullptr};
    std::shared_ptr<const audio_buffer> m_clip{std::make_shared<audio_buffer>()};
    playback_cursor m_cursor;
    std::vector<float> m_scratch;
};
