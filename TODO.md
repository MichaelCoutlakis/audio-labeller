# TODO

Grouped into phases, in the order they're planned. See `ARCHITECTURE.md` for
the design behind them.

## Phase 1: Foundations

- [ ] Extract a core library (`audio_labeller_core`) with no ImGui or PortAudio
      dependency (`project_model.cpp` includes `imgui.h` for its colour
      macros), and move `audio_buffer` out of the project model header.
- [ ] Add unit tests with doctest, starting with label load/save, path
      resolution and label operations.
- [ ] Route every model change through actions. `DragRect` in the waveform view
      still edits labels directly, and selection state is changed directly from
      views.
- [ ] Undo/redo, using before and after snapshots of the active file's labels.
- [ ] Replace autosave's "serialize every 5 s and compare" with a dirty flag
      from the undo history.
- [ ] Replace AudioFile with an `audio_source` class built on dr_wav, which can
      read ranges of frames from long files.
- [ ] Label file version 2: `version` field, `start_s` and `stop_s` keys, and a
      `files` section for per-file status, source IDs and export records.
      Migrate version 1 on load.
- [ ] Source IDs: hash of file contents, cached in the label file.

## Phase 2: Labelling flow

- [ ] Per-file status: to do, done or skip, shown and filterable in the file
      list. A done file with no labels contains none of the classes.
- [ ] Keyboard-first flow: number keys for classes, next/previous file and
      label, nudging edges, a key to mark a file done.
- [ ] One lane per class for overlapping sounds. Labels currently stack at full
      height and a click toggles all of them.
- [ ] Show why labels are hidden in the UI: labels whose audio can't be found
      are currently only reported in the log.

## Phase 3: Export

- [ ] `export` section in the project file, and its schema.
- [ ] Clip export: padded and fixed modes, merging, truncation flags, with the
      layout in `ARCHITECTURE.md` (`dataset.json`, `sources.csv`,
      `metadata.jsonl`, `labels.tsv`, `clips/`).
- [ ] Additive re-export, keeping entries whose originals were deleted.
- [ ] Read back and verify clips, record the export in the label file, and show
      which originals are safe to delete.
- [ ] Background clips from files marked done, off by default.
- [ ] Per-recording label exports: DCASE TSV, Audacity and Raven formats.

## Phase 4: Seeing and hearing

- [ ] Spectrogram view with pocketfft, with linear, log or mel scale and
      adjustable gain. The fixed +/-1 amplitude axis also makes quiet events
      hard to see.
- [ ] Long file performance: draw only the visible range of the waveform
      pyramid, load audio off the UI thread, stop copying the audio buffer into
      the engine, and add levels below 64 samples per bin for sample-accurate
      zoom.
- [ ] Make the audio thread lock-free. It currently uses a `try_lock`, and
      outputs silence on contention. Atomics or a small command queue would
      remove the mutex and the `shared_ptr`, which aren't needed since the
      stream is closed before each buffer swap.
- [ ] Replace linear interpolation with a proper resampler, or let the audio
      backend do it.
- [ ] Consider dropping PortAudio. SDL3 is already linked and its audio streams
      resample automatically.
- [ ] Slower playback and pre-roll.
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
