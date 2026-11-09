# TODO

Outstanding items from the project review, roughly in priority order within each section.

## Architecture

- [ ] Route every model change through actions. `DragRect` in the waveform view still edits labels
      directly, and selection state is changed directly from views. This is the prerequisite for
      undo/redo and a real dirty flag.
- [ ] Replace autosave's "serialize every 5 s and compare" with a dirty flag once the above is done.
- [ ] Make the audio thread lock-free. It currently uses a `try_lock`, and outputs silence on
      contention. Atomics or a small command queue would remove the mutex and the `shared_ptr`,
      which aren't needed since the stream is closed before each buffer swap.
- [ ] Replace linear interpolation with a proper resampler, or let the audio backend do it.
- [ ] Consider dropping PortAudio. SDL3 is already linked and its audio streams resample
      automatically. miniaudio would also bring FLAC and MP3 decoding.
- [ ] Extract a core library so the model has no ImGui or PortAudio dependency (`project_model.cpp`
      includes `imgui.h` for its colour macros), and `audio_buffer` doesn't live in the project
      model header.
- [ ] Add unit tests, starting with label load/save, path resolution and label operations.
- [ ] Long file performance: draw only the visible range of the waveform pyramid, load audio off
      the UI thread, stop copying the audio buffer into the engine, and add levels below 64
      samples per bin for sample-accurate zoom.

## Features

- [ ] Undo/redo.
- [ ] Spectrogram view, with log or mel scale and adjustable gain. The fixed +/-1 amplitude axis
      also makes quiet events hard to see.
- [ ] Keyboard-first flow: number keys for classes, next/previous file and label, nudging edges.
- [ ] Per-file status (to do, done, reviewed with no events), since "no events" currently looks the
      same as "not labelled yet".
- [ ] Import predictions with confidence scores, with accept/reject keys.
- [ ] Exports: DCASE TSV, Audacity and Raven formats, and cutting clips with padding.
- [ ] One lane per class for overlapping sounds. Labels currently stack at full height and a click
      toggles all of them.
- [ ] Slower playback, pre-roll, and energy-based suggestions for event boundaries.
- [ ] View individual channels. Multi-channel files are currently mixed down to mono.
- [ ] FLAC and MP3 support, and case-insensitive extension matching (`.WAV`).
- [ ] Class statistics panel.
- [ ] Annotator field, with merge and agreement checks.
- [ ] Show why labels are hidden in the UI: labels whose audio can't be found are currently only
      reported in the log.

## Build and housekeeping

- [ ] README: add build steps and a dependency list, and correct the schema filenames in the
      project definition steps.
- [ ] Add a `vcpkg.json` manifest and CI.
- [ ] Provide example audio: `*.wav` is git-ignored, so the example project can't run from a fresh
      clone.
- [ ] Lower the C++ standard from 26, as C++23 looks sufficient.
- [ ] Make `AUDIOLAB_ENABLE_WARNINGS` do something, or remove it.
- [ ] Use UTF-8 paths and a Unicode font: `path.string()` and ImGui's default font both mishandle
      non-ASCII filenames on Windows.
- [ ] Rename the `"start [s]"` and `"stop [s]"` JSON keys to `start_s` and `stop_s`, matching the CSV.
