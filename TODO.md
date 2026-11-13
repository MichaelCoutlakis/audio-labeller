# TODO

Grouped into phases, in the order they're planned. See `ARCHITECTURE.md` for the
design behind them.

## Phase 1: Foundations

Done: core library, doctest unit tests, all changes through actions, undo/redo,
revision-based dirty flag, dr_wav `audio_source`, label file version 2 with
per-file state, content-based source IDs, and SDL3 playback replacing PortAudio.

## Phase 2: Labelling flow

Done: per-file status with a filter, keyboard-first controls (see README), a
notice for labels whose audio is missing, a configurable nudge step with a
finer one on Alt, view and editing options remembered between runs, and
stacked labels that share the height only where they overlap (class lanes and
full height remain as options).

## Phase 3: Export

Done: `export` settings in the project file, clip export (padded and fixed
modes, merging, truncation flags, background clips), additive re-export,
read-back verification, export records, "exported" in the file list,
per-recording label files (DCASE TSV, Audacity and Raven, through annotation
trees), exports on a background thread with progress and cancel, and a
settings window for the export section.

## Phase 3b: External pipeline automatically labels events for review

The use case: An external pipeline proposes labels automatically in a long
multichannel field recording, cuts the training clips and trains; the app is
where people check and correct the labels. Automatic labels on real recordings
are imperfect. The review has to be fast.

Done:

- Interchange: annotation trees mirroring the dataset roots, imported as
  labels or suggestions and exported, in Audacity, DCASE or Raven format. The
  label file replaces the audio's extension by default (`rec.txt`), or appends
  to it (`rec.wav.txt`) with `"naming": "append"`. Export writes an empty file
  for every `done` file with no events, and an empty file read as labels marks
  its recording done. `${ENV_VARS}` in project paths.
- Point events: onsets (zero-length labels) placed with O under the mouse or at
  the playhead, drawn as lines with the class colour and name, and played with
  `onset_pre_s` and `onset_post_s` of context. Context regions and onsets
  overlap freely. Palette classes can be marked `"kind": "onset"`.
- Reviewing proposals: suggestions drawn faded, accepted (Enter), rejected (X)
  or relabelled (1-9); `x:` candidates hidden unless shown (H); review mode (R)
  stepping through all files with Tab, zooming and playing each with pre-roll,
  lowest score first; onset snapping by Teager-Kaiser energy (A, undone with
  Ctrl+Z).
- Checking the cut clips: a clip manifest window, filterable and sortable by
  any column; clips played and judged with one key each, verdicts editing the
  label at the onset in the source recording; J jumps to the recording.
- Channel choice per project (`"channel"`) or per file.
- RF64, 24 and 32-bit PCM and float 4-channel files at 48 to 192 kHz are tested.
  A 10 minute, 4 channel, 24-bit file (345 MB) loads in about 1.7 s in a debug
  build, with a 115 MB view buffer (the opt-in "long recording" test).
- A spectrogram (see Phase 4).

Still to do:

- [ ] Check the review flow against a real pipeline's files and a real
      multichannel recording, and tune the default windows.
- [ ] Remember the clip manifest's filter and sort between runs.

## Phase 4: Seeing and hearing

Done: a spectrogram behind the waveform (in-house FFT rather than pocketfft;
linear, log or mel scale, gain and range), computed in tiles on worker threads
and cached for scrolling, a waveform gain for quiet
recordings, drawing only the visible range and the samples themselves when
zoomed in past the pyramid, slower playback and pre-roll, and viewing a single
channel.

- [ ] Load audio (and build the pyramid) off the UI thread, so long files don't
      freeze it for a couple of seconds.

## Phase 5: Assisted labelling

Done: import predictions with scores, with accept and reject keys (as
suggestions, in Phase 3b).

- [ ] Suggestions for event boundaries based on loudness.
- [ ] Class statistics panel.
- [ ] Annotator field, with merge and agreement checks.

## Lower priority

Done: FLAC and MP3 support, and case-insensitive extension matching.

- [ ] Export options to downmix and resample clips, to save space.

## Build and housekeeping

Done: `AUDIOLAB_SDL_SUBMODULE` to use an installed SDL3 instead of the
submodule, README build steps and dependency list, C++23,
`AUDIOLAB_ENABLE_WARNINGS` (warnings for the app's own code, which builds
clean with Clang), UTF-8 project paths and a system font with wide coverage.

- [ ] Add a `vcpkg.json` manifest and CI. Note that a `vcpkg.json` switches
      existing vcpkg toolchain builds into manifest mode.
- [ ] Provide example audio: `*.wav` is git-ignored, so the example project
      can't run from a fresh clone.
