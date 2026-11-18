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
  overlap freely. Palette classes can be marked `"kind": "onset"`. (Going:
  see Phase 5.)
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
      multichannel recording, with region suggestions of varied length.
- [ ] Remember the clip manifest's filter and sort between runs.

## Phase 4: Seeing and hearing

Done: a spectrogram behind the waveform (in-house FFT rather than pocketfft;
linear, log or mel scale, gain and range), computed in tiles on worker threads
and cached for scrolling, a waveform gain for quiet
recordings, drawing only the visible range and the samples themselves when
zoomed in past the pyramid, slower playback and pre-roll, viewing a single
channel, and frequency bands on labels (drawn and edited on the spectrogram,
the selection played filtered to its band).

- [ ] Frequency bands in annotation trees and exports, where the format has
      them: Audacity's spectral selection lines, Raven's low and high
      frequency columns.
- [ ] Load audio (and build the pyramid) off the UI thread, so long files don't
      freeze it for a couple of seconds.

## Phase 5: Regions only (remove onset labels)

Labels mark a sound from where it starts to where it stops, as AudioSet strong
labels, DCASE and Audacity do. A separate onset kind asks the labeller for an
exact onset, which isn't always well defined by ear, and a fixed playback
window around it is too long for a click and too short for a thunder roll.
Whatever uses the labels can find its own onset inside the span. So: regions
only, with zero-length labels still read (old files, imported point labels)
and easy to turn into regions.

- [ ] Drop the `"kind"` key from the palette (and `class_kind`): every class is
      a region class. Ignore the key when reading old palettes, with a notice.
      Update the palette schema, README and ARCHITECTURE.
- [ ] Zero-length labels: keep reading and drawing them, but give a quick way
      to make one a region (drag an edge, or a key that extends it to an end
      estimated from the energy envelope). Decide whether O, which places one,
      stays.
- [x] Clip manifest verdicts: find a clip's label as the one whose span holds
      the clip's onset (nearest start if several), not by its start alone
      (`m_match_s`): a region's start can be well before the onset a pipeline
      finds in it. A verdict that creates a label should make a region (the
      clip's span) rather than a zero-length label.
- [ ] Review settings: `onset_pre_s` / `onset_post_s` apply only to zero-length
      labels now; regions are played with `pre_roll_s` to their stop. Rename or
      fold them together, keep reading the old keys, and retune the defaults
      for regions.
- [ ] Snapping (A): apply it to a region's start only as an option, since the
      start no longer has to be an onset.
- [ ] Loudness assisted labeller: drop its `onsets` option, or have it write
      regions from the snapped rise to where the level falls back.
- [ ] Clip export: check the padded and fixed modes with long regions (a
      region longer than a fixed clip), and that DCASE output keeps the full
      span.

## Phase 6: Assisted labelling

Done: import predictions with scores, with accept and reject keys (as
suggestions, in Phase 3b); assisted labellers, external detector commands from
the project run per recording in the background, and a built-in loudness
detector for regions or onsets.

- [ ] More built-in suggesters, eg. band-limited energy for a class's frequency
      range, or template matching against confirmed labels.
- [ ] Let a command run once over many recordings (a batch mode), for models
      that are slow to load.
- [ ] Class statistics panel.
- [ ] Annotator field, with merge and agreement checks.

## Phase 7: Finding recordings

The file list can only be filtered by status. In a project of many recordings,
labelling one class (all the recordings that might hold claps, say) means
finding them first, and what says where they are is often outside the audio:
folder names, a project's own notes or metadata files, a database, an earlier
model's output. The labeller can't know every project's format, so as with
assisted labellers, the project names commands that do the looking.

Built in:

- [ ] A text box above the file list that keeps the files whose path contains
      all of its words (case-insensitive), combined with the status filter.
- [ ] Filter by class: files with at least one label (or suggestion) of the
      chosen classes, and a column with each file's count of them.

File filters, external commands:

- [ ] A `file-filters` list in the project, beside `assisted-labellers`, each
      with a name and a command:

      ```json
      "file-filters": [
          { "name": "Hinted classes", "command": ["python", "find_by_notes.py", "{root}", "{query}", "{output}"],
            "query": "clap,finger_snap" },
          { "name": "Claps", "command": ["python", "find_by_notes.py", "{root}", "clap", "{output}"] }
      ]
      ```

      The filter combo lists them after the status filters. Choosing one runs
      its command once over the project (not per recording), in the
      background, with the same process handling as assisted labellers (no
      shell, logged output, timeout, cancel).
- [ ] Placeholders: `{root}` (each dataset root, the command run once per root),
      `{project_dir}`, `{output}` (the file to write; without it, standard
      output is read), and `{query}`: the text typed in a query box shown for
      filters that use it, free text the command interprets (a comma-separated
      list of classes, a date range, a site). `"query"` in the project gives its
      starting value. A filter without `{query}` is a fixed one, so a project
      can offer one per group of interest.
- [ ] Output: the matching recordings, one path per line, relative to the root
      it was given (or absolute). Or a CSV with a `path` column and any others
      (a reason, a score, a site), shown as columns in the file list and
      sortable. A path that isn't in the project is reported once and skipped.
- [ ] The result is kept until the filter is run again (a refresh button), so
      switching between filters is instant, and remembered between runs with
      the rest of the file list settings. It combines with the status filter and
      the text box.
- [ ] `examples/example_filter.py`: a template that reads a sidecar metadata
      file per folder (YAML or CSV, standard library only, so CSV) and returns
      the recordings whose notes mention the query's words.
- [ ] README and ARCHITECTURE: the command contract, next to the assisted
      labellers' one.

Reviewing existing labels:

The use case: a coarse class gets finer ones later. Say `insects` was used
for any chirping, and the palette now has `cricket` and `cicada` under it.
Every `insects` label has to be found, heard and relabelled. Review mode only
steps through suggestions, so today that means opening each file and finding
the labels by eye.

- [ ] Review mode over existing labels: a queue of the confirmed labels of the
      chosen classes (with their subclasses, if the palette has groups), in
      the files the current filters keep. It steps, plays and relabels (1-9)
      the same way it does for suggestions, with Enter keeping the label as it
      is. Choosing classes reuses "Filter by class" above.
- [ ] Or the queue comes from a command: a file filter's output may list
      labels rather than files (a CSV with `path`, `start_s`, `stop_s` and
      optionally `class` and a `reason`), so a project's own script decides
      what needs another look: labels of a class, labels an earlier model
      disagrees with, labels from one session. Each row is matched to the
      label whose span holds it, as clip manifest verdicts are.
- [ ] Progress through the queue survives closing the app, so a long relabel
      pass can be done over several sittings.

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
