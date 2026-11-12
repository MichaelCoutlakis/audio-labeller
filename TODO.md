# TODO

Grouped into phases, in the order they're planned. See `ARCHITECTURE.md` for
the design behind them.

## Phase 1: Foundations

Done: core library, doctest unit tests, all changes through actions, undo/redo,
revision-based dirty flag, dr_wav `audio_source`, label file version 2 with
per-file state, content-based source IDs, and SDL3 playback replacing PortAudio.

## Phase 2: Labelling flow

Done: per-file status with a filter, keyboard-first controls (see README), one
lane per class, and a notice for labels whose audio is missing.

- [ ] Configurable nudge step, and a finer step with a modifier.
- [ ] Remember the class lanes setting and file filter between runs.

## Phase 3: Export

Done: `export` settings in the project file, clip export (padded and fixed
modes, merging, truncation flags, background clips), additive re-export,
read-back verification, export records, and "exported" in the file list.

- [ ] Per-recording label exports: DCASE TSV, Audacity and Raven formats.
- [ ] Run exports on a background thread with progress, for large datasets.
- [ ] Settings UI for the export section, rather than editing the project file.

## Phase 4: Seeing and hearing

- [ ] Spectrogram view with pocketfft, with linear, log or mel scale and
      adjustable gain. The fixed +/-1 amplitude axis also makes quiet events
      hard to see.
- [ ] Long file performance: draw only the visible range of the waveform
      pyramid, load audio off the UI thread, and add levels below 64 samples
      per bin for sample-accurate zoom.
- [ ] Slower playback (SDL streams support a frequency ratio) and pre-roll.
- [ ] View individual channels. Multi-channel files are currently mixed down to
      mono for viewing and playback.

## Phase 5: Assisted labelling

- [ ] Import predictions with confidence scores, with accept/reject keys.
- [ ] Suggestions for event boundaries based on loudness.
- [ ] Class statistics panel.
- [ ] Annotator field, with merge and agreement checks.

## Lower priority

- [ ] FLAC and MP3 support (dr_flac, dr_mp3), and case-insensitive extension
      matching (`.WAV`).
- [ ] Export options to downmix and resample clips, to save space.

## Build and housekeeping

- [ ] Configure option - dependencies like SDL3 can come via vcpkg or submodule
- [ ] README: add build steps and a dependency list, and correct the schema
      filenames in the project definition steps.
- [ ] Add a `vcpkg.json` manifest and CI.
- [ ] Provide example audio: `*.wav` is git-ignored, so the example project
      can't run from a fresh clone.
- [ ] Lower the C++ standard from 26, as C++23 looks sufficient.
- [ ] Make `AUDIOLAB_ENABLE_WARNINGS` do something, or remove it.
- [ ] Use UTF-8 paths and a Unicode font: `path.string()` and ImGui's default
      font both mishandle non-ASCII filenames on Windows.
