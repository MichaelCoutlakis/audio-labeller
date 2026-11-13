# Architecture

This describes the current design and where it's heading. Sections marked
**(planned)** aren't implemented yet; `TODO.md` tracks the order they'll land
in.

## Overview

A native desktop app (C++23, SDL3 + OpenGL, Dear ImGui + ImPlot) for strongly
labelling audio: marking where each sound starts and stops and what class it is,
from a fixed palette of classes. Labelled sounds are exported as clips for
training ML models in Python. It's also where people check and correct labels
proposed by an external pipeline (a detector on long field recordings), which
reads and writes the same per-recording label files.

```
 views (ImGui)  --actions-->  action_handler  -->  project_model / app_state
      ^                             |                    |
      |                             v                    v
      +------- read state ---- audio_engine        label file (autosave)
                               (SDL3 audio)
```

## Layers

| Layer          | Code                                                    | Depends on                         |
| -------------- | ------------------------------------------------------- | ---------------------------------- |
| Core           | `src/model`, `src/io` (audio files, source IDs), export | std, nlohmann/json, dr_libs, spdlog |
|                | `src/interchange`: label file formats, annotation trees |                                    |
|                | `src/review`: review queue, playback windows            |                                    |
|                | `src/manifest`: clip manifests and verdicts             |                                    |
|                | `src/dsp`: onset snapping, FFT and spectrogram          |                                    |
| App            | `src/app`: state, actions, handler, undo history        | Core                               |
| Audio playback | `src/audio`                                             | Core, SDL3 audio                   |
| Views          | `src/views`                                             | App, ImGui, ImPlot                 |
| Shell          | `src/imgui_app`, `src/main.cpp`                         | SDL3, OpenGL, ImGui                |

Core is its own static library, `audio_labeller_core`, with no ImGui or audio
device dependency, so it can be unit tested on its own. Colours are plain RGBA
`uint32_t` there, packed the same way as ImGui's (checked at compile time in
`app/colours.h`). Tests use doctest and live in `tests/`; playback is tested
against SDL's dummy audio driver.

### Extension points

Input formats, label layouts and outputs vary between datasets and tools, so
each lives behind one seam:

- **Audio formats**: `audio_source::decoder` (`io/audio_source.cpp`). A decoder
  per format (dr_wav for WAV, RF64 and W64; dr_flac; dr_mp3), chosen by
  extension, case-insensitively. Formats that can't be copied byte for byte are
  exported as float WAV.
- **Label file formats**: `label_format` (`interchange/label_format.h`) reads
  and writes the labels of one recording as `text_label`s (times, text, score).
  Audacity, DCASE and Raven are implemented; `make_label_format` picks one by
  name.
- **Data layout**: `annotation_tree` maps a recording's portable path to its
  label file in a tree that mirrors the dataset roots. A different layout (eg.
  one file per dataset) would be another mapping over the same formats.
- **Label text**: `label_text` / `parse_label_text` turn a label's class and
  state into text and back (`x:` for rejected), separately from the format.
- **Outputs**: clip export (`export/`), annotation trees and the CSV export
  each take the project's labels and write their own layout.
- **Clip manifests**: column names are configurable, so a pipeline's CSV is read
  as it is.

## Data flow and actions

Views never change the model. They read state and push `actions::app_action`
values (a `std::variant`) onto a queue. After all views have rendered,
`audio_labeller_app::on_frame` dispatches the queue to `action_handler`.

Each dispatch is an error boundary: an exception from one action (a corrupt
audio file, a missing device) is reported in an error popup and the session
carries on.

Every change goes through an action, including dragging label edges: `DragRect`
edits a copy, and a `move_label` action is sent when it changes. Keyboard
shortcuts are turned into actions in `views/shortcuts.cpp`.

### Undo/redo

Undo uses snapshots rather than inverse commands, because snapshots can't get
out of step with the code that changes labels. Each undoable action records the
active file's labels and the selection before and after the change. Repeated
`move_label` actions on the same label within one drag merge into one undo step.
Label sets per file are small (kilobytes), so the memory cost is negligible.

Undo covers label changes only, not per-file status or playback. Entries for a
file other than the active one restore its labels but not the selection.

The model counts revisions: every change goes through its mutating functions,
which increment the count, and autosave writes only when it differs from the
revision last saved.

## Files

All paths in a project file are relative to that project file. Saves are atomic:
written to a `.tmp` file and renamed over the target, so a failure can never
leave a truncated file. A file that can't be read is never overwritten.

| File                                          | Written by | Purpose                                                        |
| --------------------------------------------- | ---------- | -------------------------------------------------------------- |
| `audio-labelling-app-settings.json`           | App        | Current project, audio device, view and editing options. In the working directory. |
| Project (`schemas/project.schema.json`)       | User       | Dataset roots, palette path, label file path, export, annotation trees, review and clip manifest settings. The export settings window rewrites its `export` section. |
| Palette (`schemas/label-palette.schema.json`) | User       | Classes: stable integer ID, name, colour, kind (region or onset). |
| Label file (`schemas/labels.schema.json`)     | App        | Labels and suggestions, per-file status and channel, source IDs and export records. |
| Annotation trees                              | Both       | Per-recording label files for other tools, see below.          |

Project paths may contain `${NAME}`, replaced by the environment variable NAME
when resolved. Dataset roots are kept as written for file identity, so a label
file is the same on machines where the variables differ.

### File identity

An audio file is identified by its dataset root, exactly as written in the
project, plus its path relative to that root with forward slashes. This survives
moving the project to another machine or running from another working directory.
If exactly one root in the project changes, labels follow it, as that's taken to
be a moved dataset.

Labels whose audio file can't be found are kept verbatim in the label file, so
an unplugged drive or deleted original never loses labels.

Each file also has a **source ID**: the first 12 hex digits of a 64-bit FNV-1a
hash of the file's bytes. It's computed when first needed and cached in the
label file along with the file's size and modification time. Because it depends
on content rather than location, it's stable across renames and moves, and the
same recording copied twice is detected. It names exported clips and links them
back to their source.

### Label file version 2

Version 1 files are migrated on load. Version 2 adds:

- `"version": 2`
- Keys `start_s` and `stop_s` in place of `"start [s]"` and `"stop [s]"`.
- A `files` array, with one entry per file that has state:
  - `dataset-root`, `filename`: identity, as for labels
  - `status`: `todo` (the default), `done` or `skip`. A `done` file with no
    labels has been checked and contains none of the classes.
  - `source-id`, `size`, `mtime`: the cached source ID
  - `exported`: time, export directory and whether the clips were verified

### Label file version 3

Adds to each label an optional `state`, `suggested` or `rejected` (absent for a
label), and an optional `score`, the proposer's confidence. A label whose
`stop_s` equals its `start_s` is an **onset**: a transient defined by its start
alone. Files gain an optional `channel` (numbered from 1, or `mix`). The version
was bumped so that an older app can't read suggestions as labels.

## Labels, onsets and suggestions

A label is a region (start and stop) or an onset (a point). Palette classes can
be marked `"kind": "onset"`, so that dragging with them makes onsets; a
recording can carry context regions and onsets overlapping them.

Suggestions come from importing an annotation tree as suggestions. They're drawn
faded with a dashed outline and their score, and become labels when accepted,
relabelled or edited by class; rejecting keeps them as `rejected`, hidden unless
shown, as a record of what was checked. Only confirmed labels are exported as
clips, and background clips avoid suggestions too.

### Layout

`app/lanes.h` gives each visible label a vertical band. By default labels are
**stacked** like overlapping calendar events: labels that overlap in time form a
group, split into as many columns as it needs at its most crowded, and labels
that overlap nothing use the full height. Onsets are lines over the full height.
A fixed lane per class and full height for every label remain as options. The
hit test uses the same bands.

### Review

`review_queue` lists every suggestion in files that aren't skipped, in file
order, or by score (lowest first) if any has one. Review mode steps through it
across files: each step opens the file, zooms to `view_pre_s` before to
`view_post_s` after the suggestion (like the training window), and plays it with
pre-roll. The position is kept by its order key, so stepping on from a
suggestion that's just been accepted continues from where it was.

Onsets are played from `onset_pre_s` before to `onset_post_s` after. Snapping
moves an onset to the sharpest rise in Teager-Kaiser energy within
`snap_window_s`, as one undo step.

## Annotation trees

Each entry of the project's `annotations` list is a tree of label files that
mirrors the dataset roots: `<root>/<path relative to its dataset root>` with the
extension replaced (or appended) by the format's. Trees are imported as labels
or suggestions (or not at all) and exported.

- **Export** writes a file for each file marked done (and optionally the rest),
  so that an existing file means reviewed and an empty one means "checked, no
  events". Rejected labels are written as `x:<class>`. Files are written
  atomically and only if they change.
- **Import** matches text to classes by name, adds labels not already present
  (same class and times within 1 ms), and counts label files with no recording.
  Importing clears the undo history, as an older snapshot would drop what it
  added. An empty file read as labels marks its recording done.

A typical pipeline round trip: the pipeline writes proposals into one tree,
imported as suggestions; reviewers accept, reject or relabel; export writes the
reviewed tree the pipeline reads back to cut clips and train.

## Assisted labelling

A **suggester** (`assist/assisted_labeller.h`) proposes `text_label`s for one
recording at a time, on a worker thread, without touching the project. There are
two kinds, both listed in one dropdown:

- **External commands**, from the project's `assisted-labellers`. Detectors are
  usually Python models, so they run as processes rather than as plugins loaded
  into the app: any language works, no ABI is shared, a crash loses only that
  run, and a model can be changed without rebuilding. `io/process.cpp` starts
  the program without a shell (`CreateProcessW` or `fork`/`exec`), with UTF-8
  arguments, its output logged to a file, and kills it on cancel or timeout. The
  command writes a label file in one of the label formats, read back as for
  annotation trees.
- **Built-ins**, compiled in: currently loudness (`dsp/loudness.cpp`): frames
  some margin above the noise floor (a low percentile of frame levels), joined
  across short gaps, as regions or as onsets snapped to their rise. A new
  built-in is a `suggester` and a case in `make_suggester`.

A run is copied into an `assist_job` (the recordings, their portable paths and
channels), run by `run_assist` with progress and cancellation, and applied on
the UI thread by `apply_assist`, through the same `add_text_labels` as imports:
class names are matched to the palette and suggestions already present aren't
added again. A failure on one recording is reported and the rest carry on.

## Clip manifests

A clip manifest is a pipeline's CSV of cut clips, with each clip's source
recording, onset and label. It's shown as a sortable, filterable table. Verdicts
(correct, relabelled, not an event) edit the label nearest the onset (within
`match_s`, preferring the clip's class) in the source recording, or create one,
so the correction lives in the annotations rather than a separate review file.
Clips are played straight from their files; the active file's audio is put back
in the engine before anything else plays.
## Audio

### Reading files

The `audio_source` class opens a file without reading it, reports sample rate,
channels, frame count and sample format, and reads any range of frames, through
a decoder chosen by extension (see Extension points). Export reads only the
ranges it needs, so long recordings don't have to fit in memory. RF64, 24 and
32-bit PCM and float files with several channels at up to 192 kHz are tested.

The view gets one channel, or a mix of all of them, and a min/max pyramid for
drawing the waveform. Only the visible range is drawn, with a gain for quiet
recordings; zoomed in past the pyramid's finest level (64 samples a bin), the
samples themselves are drawn. **(planned)** Loading moves to a background
thread, so long files don't freeze the UI.

### Playback

`audio_engine` plays the mono clip through an SDL audio stream bound to the
device, which converts it to the device's sample rate and format. The stream
stays open while a device is selected, so very short sounds aren't cut off by
device start-up. Devices are remembered by name, with "System default" first.

Threads: the UI thread owns everything except the `playback_cursor`, which SDL's
audio thread advances in the stream callback. SDL holds the stream's lock during
the callback, and the engine takes the same lock for its brief changes, so no
separate mutex is needed. The clip is shared (`shared_ptr<const audio_buffer>`)
rather than copied.

Playback can be slowed (which lowers the pitch) with the stream's frequency
ratio.

### Spectrogram

`dsp/spectrogram.cpp` computes levels with a small radix-2 FFT (1024 points,
Hann window) on linear, log or mel frequency scales. When zoomed out, each
column is the maximum of several FFT frames, so short events between frames
aren't missed.

`dsp/spectrogram_tiles` computes it in **tiles** on worker threads, so the UI
never waits for an FFT. A tile is 256 columns by 256 rows at a zoom level, where
level k has 2^k samples per column; the view uses the coarsest level that's
still at least as sharp as the screen. Tiles of a level line up whatever the
view, so scrolling reuses them. Each frame the view asks for the visible tiles:
ready ones are drawn, missing ones (and one either side) are queued from the
middle of the view outwards, replacing the previous frame's queue, and coarser
tiles already computed are drawn underneath until they arrive. The cache keeps
the most recently used tiles; new audio or settings start a new generation, and
work for the old one is dropped. Levels are kept in dB, so changing the gain or
range only recolours the textures (a few per frame).

## Export

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
  durations.tsv     filename and duration of every clip, as psds_eval expects
  clips/
    3f9a1c2e7b4d_000012340.wav    <source_id>_<clip start in ms, 9 digits>
```

`metadata.jsonl` follows the layout Hugging Face's `audiofolder` loader reads: a
`file_name` column relative to the export directory. One line per clip, shown
here formatted:

```json
{
  "file_name": "clips/3f9a1c2e7b4d_000012340.wav",
  "source_id": "3f9a1c2e7b4d",
  "source_offset_s": 12.34,
  "duration_s": 2.1,
  "sample_rate": 48000,
  "channels": 2,
  "events": [
    {
      "onset_s": 0.5,
      "offset_s": 1.6,
      "event_label": "Dog",
      "class_id": 0,
      "label_id": 17,
      "truncated": false
    }
  ]
}
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
- **Fixed mode**: a window of fixed length centred on the label, 10 s by default
  to match the DCASE tasks. A label longer than the window gets a longer clip.
- Labels whose clips would overlap are merged into one clip, up to a maximum
  clip length (30 s by default).
- Every label that overlaps a clip is included in it, cut to the clip's bounds,
  with `truncated` set if it was cut.
- **Background clips** (optional, off by default): windows containing no labels,
  taken only from files marked `done`, since only there is "no label" known to
  mean "none of the classes". Their `events` list is empty.

Settings live in an optional `export` section of the project file (see
`schemas/project.schema.json`), for example:

```json
"export": { "directory": "dataset", "mode": "padded", "padding_s": 0.5,
            "background_per_file": 3, "background_length_s": 5 }
```

Only files marked `done` are exported, from the File List's "Export clips"
button. Clip start and stop times are rounded to whole samples, and the label
times in the manifests are relative to the samples actually cut. Clips are
written by `io/wav_writer.cpp`: PCM and float sources are copied byte for byte,
other WAV formats are decoded to 32-bit float. The export currently runs on the
UI thread.

### Background export

`prepare_export` copies what the export needs from the project (files marked
done, their confirmed labels, cached source IDs) into an `export_job`.
`run_export` runs on a worker thread with only the job, reporting progress and
checking for cancellation between files. When it finishes, the UI thread applies
the results (source IDs, export records) to the project. Labelling carries on
meanwhile, and closing the app cancels and waits for it.

### Other label formats

The labels of whole recordings are written as per-file DCASE TSV, Audacity
label tracks or Raven selection tables by annotation trees (see above).

## Error handling

- Expected failures (bad files, missing devices, failed saves) throw, and are
  caught at the action dispatch, at project open, or in `main`, which ensures
  destructors run and labels are saved.
- A project that fails to open is replaced by an empty model, which never writes
  files.
- Autosave failures are reported once, then retried silently.

## Dependencies

All are permissively licensed, per `CONTRIBUTING.md`.

| Dependency         | Licence               | Use                                       | Status  |
| ------------------ | --------------------- | ----------------------------------------- | ------- |
| SDL3               | Zlib                  | Window, input, GL context, audio playback | Current |
| Dear ImGui, ImPlot | MIT                   | UI, waveform plot                         | Current |
| nlohmann/json      | MIT                   | JSON                                      | Current |
| spdlog (fmt)       | MIT                   | Logging                                   | Current |
| dr_wav (dr_libs)   | Public domain / MIT-0 | WAV reading and writing                   | Current |
| doctest            | MIT                   | Unit tests                                | Current |
| dr_flac, dr_mp3    | Public domain / MIT-0 | FLAC and MP3 reading                      | Current |
