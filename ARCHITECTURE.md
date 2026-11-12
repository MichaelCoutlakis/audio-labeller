# Architecture

This describes the current design and where it's heading. Sections marked
**(planned)** aren't implemented yet; `TODO.md` tracks the order they'll land in.

## Overview

A native desktop app (C++23, SDL3 + OpenGL, Dear ImGui + ImPlot) for strongly
labelling audio: marking where each sound starts and stops and what class it is,
from a fixed palette of classes. Labelled sounds are exported as clips for
training ML models in Python.

```
 views (ImGui)  --actions-->  action_handler  -->  project_model / app_state
      ^                             |                    |
      |                             v                    v
      +------- read state ---- audio_engine        label file (autosave)
                               (PortAudio)
```

## Layers

| Layer | Code | Depends on |
|---|---|---|
| Core | `src/model`, audio file I/O, export | std, nlohmann/json, dr_wav, spdlog |
| App | `src/app`: state, actions, handler, undo history | Core |
| Audio playback | `src/audio` | Core, PortAudio |
| Views | `src/views` | App, ImGui, ImPlot |
| Shell | `src/imgui_app`, `src/main.cpp` | SDL3, OpenGL, ImGui |

**(planned)** Core becomes its own static library, `audio_labeller_core`, with
no ImGui or PortAudio dependency, so it can be unit tested on its own. Colours
are stored as plain RGBA `uint32_t` there, and only converted for ImGui in the
views. Tests use doctest and live in `tests/`.

## Data flow and actions

Views never change the model. They read state and push `actions::app_action`
values (a `std::variant`) onto a queue. After all views have rendered,
`audio_labeller_app::on_frame` dispatches the queue to `action_handler`.

Each dispatch is an error boundary: an exception from one action (a corrupt
audio file, a missing device) is reported in an error popup and the session
carries on.

**(planned)** Every change goes through an action, including dragging label
edges. `DragRect` edits a copy, and a `move_label` action is sent when it
changes.

### Undo/redo (planned)

Undo uses snapshots rather than inverse commands, because snapshots can't get
out of step with the code that changes labels. Each undoable action records the
active file's labels and the selection before and after the change. Repeated
`move_label` actions on the same label within one drag merge into one undo step.
Label sets per file are small (kilobytes), so the memory cost is negligible.

The history also gives a dirty flag, which replaces autosave's current
"serialize every 5 s and compare".

## Files

All paths in a project file are relative to that project file. Saves are
atomic: written to a `.tmp` file and renamed over the target, so a failure can
never leave a truncated file. A file that can't be read is never overwritten.

| File | Written by | Purpose |
|---|---|---|
| `audio-labelling-app-settings.json` | App | Current project and audio device. In the working directory. |
| Project (`schemas/project.schema.json`) | User | Dataset roots, palette path, label file path, export settings. |
| Palette (`schemas/label-palette.schema.json`) | User | Classes: stable integer ID, name, colour. |
| Label file (`schemas/labels.schema.json`) | App | Labels, and **(planned)** per-file status and export records. |

### File identity

An audio file is identified by its dataset root, exactly as written in the
project, plus its path relative to that root with forward slashes. This
survives moving the project to another machine or running from another working
directory. If exactly one root in the project changes, labels follow it, as
that's taken to be a moved dataset.

Labels whose audio file can't be found are kept verbatim in the label file, so
an unplugged drive or deleted original never loses labels.

**(planned)** Each file also gets a **source ID**: the first 12 hex digits of a
64-bit FNV-1a hash of the file's bytes. It's computed once and cached in the
label file along with the file's size and modification time. Because it depends
on content rather than location, it's stable across renames and moves, and the
same recording copied twice is detected. It names exported clips and links them
back to their source.

### Label file version 2 (planned)

Version 1 files are migrated on load. Version 2 adds:

- `"version": 2`
- Keys `start_s` and `stop_s` in place of `"start [s]"` and `"stop [s]"`.
- A `files` array, with one entry per file that has state:
  - `dataset-root`, `filename`: identity, as for labels
  - `status`: `todo` (the default), `done` or `skip`. A `done` file with no
    labels has been checked and contains none of the classes.
  - `source-id`, `size`, `mtime`: the cached source ID
  - `exported`: time, export directory and whether the clips were verified

## Audio

### Reading files (planned)

An `audio_source` class wraps dr_wav. It opens a file without reading it,
reports sample rate, channels, frame count and sample format, and reads any
range of frames. Export reads only the ranges it needs, so long recordings don't
have to fit in memory. It replaces AudioFile, which reads the whole file, and
later FLAC and MP3 can be added with dr_flac and dr_mp3 from the same family.

The view gets a mono mix and a min/max pyramid of the file for drawing the
waveform. **(planned)** Loading moves to a background thread and the pyramid
gets finer levels, so long files neither freeze the UI nor lose detail at
high zoom.

### Playback

`audio_engine` keeps a PortAudio stream open while a file is loaded, so very
short sounds aren't cut off by stream start-up. If the device rejects the file's
sample rate (for example WASAPI in shared mode) it plays at the device's rate,
resampling with linear interpolation.

Threads: the UI thread owns everything. The audio callback reads the playback
state under a `try_lock` and outputs silence on contention, so it never blocks.
**(planned)** This becomes lock-free (atomics or a small command queue), with a
better resampler for slowed-down playback.

### Spectrogram (planned)

Computed with pocketfft on a background thread into texture tiles, with linear,
log and mel frequency scales and adjustable gain.

## Export (planned)

Export turns labelled recordings into a dataset for training. The intended
workflow is: label long recordings from the dataset roots, export clips around
the labels, then delete the originals. So the export is treated as the dataset
of record:

- **Lossless.** Clips keep the source's sample rate, channels and sample format.
  Downmixing and resampling are left to training code, where they're cheap and
  reversible. Options to do them at export may be added later to save space.
- **Self-contained.** Every clip carries its labels and a link to its source.
- **Additive.** A re-export replaces only the entries for the sources being
  exported, so entries whose originals were deleted are kept.
- **Verified.** Each clip is read back and compared before its source is marked
  exported. The app shows which originals are safe to delete but never deletes
  them itself.

### Output layout

```
<export dir>/
  dataset.json      format version, palette, export settings, app version
  sources.csv       source_id -> original root and path, duration, rate, channels
  metadata.jsonl    one row per clip, with its labels nested
  labels.tsv        one row per label: DCASE strong format
  clips/
    3f9a1c2e7b4d_000012340.wav    <source_id>_<clip start in ms, 9 digits>
```

`metadata.jsonl` follows the layout Hugging Face's `audiofolder` loader reads:
a `file_name` column relative to the export directory. One line per clip,
shown here formatted:

```json
{"file_name": "clips/3f9a1c2e7b4d_000012340.wav",
 "source_id": "3f9a1c2e7b4d", "source_offset_s": 12.34, "duration_s": 2.1,
 "sample_rate": 48000, "channels": 2,
 "events": [{"onset_s": 0.5, "offset_s": 1.6, "event_label": "Dog",
             "class_id": 0, "label_id": 17, "truncated": false}]}
```

`labels.tsv` has the columns `filename  onset  offset  event_label`. This is the
format used by the DCASE sound event detection tasks, and read by sed_eval,
sed_scores_eval and psds_eval.

`source_id` is the grouping key for splitting into train and test sets: clips
from one recording are similar, so they must all go to the same split (for
example with scikit-learn's `GroupKFold`).

Times are in seconds. Onsets and offsets are relative to the clip.

### Clip rules

- **Padded mode** (default): each label plus 0.5 s before and after.
- **Fixed mode**: a window of fixed length centred on the label, 10 s by
  default to match the DCASE tasks. A label longer than the window gets a longer
  clip.
- Labels whose clips would overlap are merged into one clip, up to a maximum
  clip length (30 s by default).
- Every label that overlaps a clip is included in it, cut to the clip's bounds,
  with `truncated` set if it was cut.
- **Background clips** (optional, off by default): windows containing no
  labels, taken only from files marked `done`, since only there is "no label"
  known to mean "none of the classes". Their `events` list is empty.

Settings live in an `export` section of the project file.

### Other label formats (planned)

The same module writes the labels of whole recordings as per-file DCASE TSV,
Audacity label tracks and Raven selection tables.

## Error handling

- Expected failures (bad files, missing devices, failed saves) throw, and are
  caught at the action dispatch, at project open, or in `main`, which ensures
  destructors run and labels are saved.
- A project that fails to open is replaced by an empty model, which never
  writes files.
- Autosave failures are reported once, then retried silently.

## Dependencies

All are permissively licensed, per `CONTRIBUTING.md`.

| Dependency | Licence | Use | Status |
|---|---|---|---|
| SDL3 | Zlib | Window, input, GL context | Current |
| Dear ImGui, ImPlot | MIT | UI, waveform plot | Current |
| PortAudio | MIT | Playback | Current |
| nlohmann/json | MIT | JSON | Current |
| spdlog (fmt) | MIT | Logging | Current |
| AudioFile | MIT | WAV reading | To be replaced by dr_wav |
| dr_wav (dr_libs) | Public domain / MIT-0 | WAV reading and writing | Planned |
| doctest | MIT | Unit tests | Planned |
| pocketfft | BSD-3-Clause | Spectrogram | Planned |
