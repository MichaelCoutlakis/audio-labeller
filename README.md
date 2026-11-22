# Audio Labeller application

This is an application for strong labelling of audio. 

Features:
- Multi audio file projects export to a single label file
- Define the temporal location of the sound in addition to the class: a region
  from where it starts to where it stops
- Playback of the audio and labels, at reduced speed if needed, with fixed or
  automatic gain for quiet recordings
- Review of labels proposed by a detector or pipeline: accept, reject or
  relabel each, stepping through all files lowest confidence first
- Assisted labelling: run detector scripts, or a built-in loudness detector,
  to propose labels
- Per-recording label files in Audacity, DCASE or Raven formats, for
  exchanging labels with other tools
- A clip export for training, and a checker for clips cut by another tool
- Waveform and spectrogram views; WAV (including RF64), FLAC and MP3
- A high-pass filter for wind noise, set per project: applied to the waveform,
  spectrogram and playback, without moving sounds in time, but never to
  exported clips

## Motivation
- Audacity provides labelling but AFAIK these need to be manually typed 
rather than selected from a predefined dictionary / palette.
Additionally, it might be tricky in the multi file case.
Finally, with short sounds the playback render omitts the sound on some systems.
- Label Studio almost worked for me, but I couldn't quite configure it just how I wanted.

At the time, it seemed easier to make my own application rather than forking / editing existing similar tools.

## Building

You need a C++23 compiler (recent Clang, GCC or MSVC), CMake 3.22 or later,
and OpenGL. Every other dependency is a git submodule in `external/`, built
along with the app, so no package manager is needed:

```bash
git clone --recursive <repository URL>
cd audio-labeller
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/tests/audio_labeller_tests
```

In a clone made without `--recursive`, run `git submodule update --init
--recursive` first.

| Dependency          | Submodule                          | Installed (`AUDIOLAB_SYSTEM_DEPS=ON`)          |
| ------------------- | ---------------------------------- | ---------------------------------------------- |
| SDL3                | `external/SDL`                     | `find_package(SDL3)`, eg. vcpkg `sdl3`          |
| Dear ImGui, ImPlot  | `external/imgui`, `external/implot` | Always the submodules                          |
| nlohmann/json       | `external/json`                    | `find_package(nlohmann_json)`, eg. vcpkg `nlohmann-json` |
| spdlog (with fmt)   | `external/spdlog`, with its own fmt | `find_package(spdlog)`, eg. vcpkg `spdlog`     |
| dr_libs             | `external/dr_libs`                 | `dr_wav.h` on the include path, eg. vcpkg `drlibs` |
| doctest (tests)     | `external/doctest`                 | `find_package(doctest)`, eg. vcpkg `doctest`    |
| OpenGL              | The system's                       | The system's                                   |

To use installed packages instead, from vcpkg or the system's package manager,
configure with `-DAUDIOLAB_SYSTEM_DEPS=ON`, for example:

```bash
vcpkg install nlohmann-json spdlog drlibs doctest sdl3
cmake -B build -G Ninja -DAUDIOLAB_SYSTEM_DEPS=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
```

`CMakePresets.json` has a hidden `base` preset to inherit from in your own
`CMakeUserPresets.json`, where compilers and toolchain paths go. Options:
`AUDIOLAB_SYSTEM_DEPS` (off: build the dependencies from `external/`),
`AUDIOLAB_SDL_SUBMODULE` (SDL alone; follows `AUDIOLAB_SYSTEM_DEPS` unless set),
`AUDIOLAB_BUILD_TESTS` (on) and `AUDIOLAB_ENABLE_WARNINGS` (on; warnings for
the app's own code).

## Project definition and files

To try the app first, make the example's audio and open its project:

```
python3 examples/make_example_audio.py
```

then File > Open project and choose `examples/animal-farm-project.json`. The
script writes two short recordings of synthetic calls (a dog, a cat, birds,
and a yip and a tap on glass for the fox, over light background noise) to
`examples/audio/`, and two clips for the example review list to `examples/clips/`. The calls are at fixed
times, which the example labels and review list use; one bark is quiet, to try
the playback gain on. It needs only Python's standard library.

The quickest start is **File > New project from folder**: choose the folder of
recordings, where to save the project (they can be in different places, so the
data stays read only), and the classes to label, as a list of names or an
existing palette to share. This writes `<name>.json` and `<name>-palette.json`,
and saves labels to `<name>-labels.json` beside them. Nothing that already
exists is overwritten.

**File > Project settings** edits the common settings of the open project:
its audio folders, palette, default channel for multichannel files, a
high-pass filter for wind and handling noise, playback and review timings, and
export prefix. It's shown after making a project too.
It rewrites those keys of the project file and leaves the rest as they are.

**File > Open project** opens any project file, and the app opens it again at the
next start. Right-click a file in the list to show it in the system's file
manager or copy its path.

Several recorders (or microphones) at one site hear mostly the same sounds.
Once one recording is labelled, select its labels (Ctrl+A) and copy them
(Ctrl+C), open the other recording and paste them (Ctrl+V) as suggestions at the
same times. Ctrl+Shift+V pastes with a shift instead, such as the recordings'
start offset (negative if this one started later), and can paste labels rather
than suggestions. Labels that fall outside the recording are left out. The
pasted labels stay selected, so the arrow keys move them together to line them
up (whole labels keep their length at the file's ends); A snaps each start.
Then review them: Enter accepts, X drops a sound this recorder missed.

To write a project by hand:

1. Create your project's label palette according to `schemas/label-palette.schema.json`
2. Define your project's paths (data set, export path etc) according to `schemas/project.schema.json`
3. Open it with File > Open project, or set `project` in the app settings file
   (`audio-labelling-app-settings.json`, in the working directory) to its path

Note that if paths in the project .json are relative then they are taken
to be relative to the project.json location on disk rather than the app
working directory. Paths may use environment variables, as in
`"${TRAINING_DIR}/recordings"`, so that one project works on machines with
different drive letters and mount points. Labels refer to dataset roots as
written, so they're the same on every machine.

Labels are saved automatically every few seconds and on exit. Each label stores its audio file
relative to its dataset root, so a project can be moved or shared. Labels whose audio file can't be
found are kept in the label file, and are picked up again once the file is available.

The app settings file also remembers the view and editing options (label
layout, file list search, filters and order, nudge steps, playback speed and gain, spectrogram) between runs.
Its `font` is a font file, empty for a system font that covers most languages,
or `builtin` for ImGui's own pixel font (crisp, but ASCII only); `font_size`
sets the size. Both take effect at the next start.

## Exporting clips for training

"Export clips" in the file list cuts clips around the labels of every file
marked done, into a dataset directory (`dataset` next to the project file by
default). It runs in the background, so labelling can carry on; "Settings..."
edits the export settings in the project file.

- `clips/*.wav`: lossless copies of the source audio around the labels
- `metadata.jsonl`: one row per clip with its labels, loadable with pandas
  (`read_json(lines=True)`) or Hugging Face's `audiofolder`
- `labels.tsv`: one row per label, in the DCASE strong label format
- `sources.csv`: maps each clip's `source_id` back to its original recording.
  Use it to keep clips of one recording in the same train/test split.

Exporting again updates the dataset rather than replacing it, so originals can
be deleted once exported ("exported" in the file list). A file that's no
longer marked done (skipped, or to do again) has its clips removed at the next
export. See `ARCHITECTURE.md` for the format and the `export` settings in the
project file.

Only confirmed labels are exported; suggestions waiting for review aren't, and
background clips avoid them.

## Exchanging label files: annotation trees

The project's `annotations` list describes trees of per-recording label files
that mirror the dataset roots: `<root>/site1/rec01.txt` holds the labels of
`<dataset root>/site1/rec01.wav`. Each tree has a format (`audacity`, `dcase`
or `raven`) and gets Import and Export buttons in the file list.

```json
"annotations": [
    { "name": "Proposals", "root": "${PIPELINE}/proposals", "import": "suggestions", "export": false },
    { "name": "Reviewed", "root": "${PIPELINE}/annotations", "format": "audacity" }
]
```

- Imported as `suggestions`, labels arrive faded, to be accepted, rejected or
  relabelled. Labels whose text starts with `x:` are rejected candidates,
  hidden unless shown (H). Importing again adds nothing twice.
- Export writes a file for every file marked done, empty if it has no
  events, so a reader can tell "no events" from "not reviewed". Rejected labels
  are written with `x:` in front. An empty file read as labels marks its
  recording done. Once a recording is no longer done (marked skipped, or to
  do), the next export removes the file it wrote for it, unless the file was
  changed since, in which case it's listed and left in place.
- Label text is matched to palette classes by name, ignoring case. Names not
  in the palette are added for the session, in grey.

## Reviewing proposals

Press R for review mode. Tab then steps through the suggestions in every file
(lowest score first, if they have scores), opening each file, zooming to a
window around the suggestion and playing it from a short pre-roll to its stop.
Enter accepts, X rejects and 1-9 relabels, each moving on to the next. The
`review` section of the project sets the windows: `pre_roll_s` before each
label, `point_post_s` after a zero-length one, and `view_pre_s` and
`view_post_s` for what's shown. File > Project settings edits them too.

## Assisted labelling

The Assisted Labelling window runs a detector over the active file, the files
to do, or all of them, in the background, and adds what it finds as
suggestions to review (R, then Tab). Detectors are external programs, listed
in the project's `assisted-labellers`, plus a built-in loudness detector:

```json
"assisted-labellers": [
    { "name": "Bird detector",
      "command": ["python", "${MODELS}/detect.py", "{audio}", "{output}"],
      "format": "audacity", "timeout_s": 300 },
    { "name": "Loud sounds", "builtin": "loudness", "class": "Bird",
      "options": { "margin_db": 15, "snap_starts": true } }
]
```

A command runs once per recording, without a shell, and writes its
suggestions to `{output}` in the given format (or prints them, if there's no
`{output}`), exiting with 0. An optional score column orders the review. Its
output is logged, and shown if it fails. `examples/example_detector.py` is a
template. The built-in finds sounds some decibels above the background
(regions until the level falls back, optionally starting at the sharpest
rise), with its settings adjustable in the
window. Running again doesn't add the same suggestion twice.

## Review lists: hearing a pipeline's events

A review list is a CSV of events to hear and judge, eg. the ones a model gets
wrong or is unsure about, written by a training pipeline and drawing on several
datasets. Reviewing a list never changes the project's labels: its output is a
results file beside it, which the pipeline reads, and which can be imported
into the project's labels as a separate step. Open a list with File > Open
review list (the recent ones are remembered, and the last one reopens with its
project), or name one in the project's `clip-manifest` section. One row per
event:

| Column | | |
|---|---|---|
| `path` | required | The recording, relative to a dataset root, or absolute |
| `start_s` | required | Where the event starts in it, in seconds |
| `stop_s` or `duration_s` | optional | Its end; without one, it's drawn and played `region_s` (1 s) long |
| `label` | optional | The class the pipeline gave it |
| `root` | optional | The row's dataset root, as written in the project, for lists drawing on several |
| `channel` | optional | The channel of the recording to view and play, from 1 |
| `clip_path` | optional | A cut clip, shown when the recording isn't in the project |
| `clip_offset_s` | with `clip_path` | Where the clip starts in its recording; negative if the clip starts with padding before the recording's start |
| `id` | optional | A key to join the results on |

All times are in the recording's time, clip or not. Any other column (a score,
the model's prediction, a reason) is shown, and filterable and sortable like
the rest. `examples/review-list.csv` is a small list, which the example project
opens, and `examples/example-results.csv` shows what its results look like once
reviewed. A CSV with other column names
is read by naming them in the project's `clip-manifest` section (see the
schema).

The Review List window lists the rows, filterable by words or `column=value`
(`verdict=` keeps the rows not heard yet) and sortable by any column. The
verdict, note and what the recording now says come first, and stay in view with
the list's first column when scrolling sideways; paths show the file name, with
the whole path on hover; right-click the header to hide or show columns. The
filter, sort and hidden columns are remembered per list. With the
list focused, Up/Down show each row and play it; Space plays it again. Enter
marks it correct, X not an event and 1-9 relabels it; N writes a note on the
row; Ctrl+Z undoes. J opens the recording at the event.

A row is shown in its recording, zoomed to it, and drawn as a label of its own
("list: Dog"), with a line at its start, since whether the onset sits on the
sound's start is part of what's judged. It's drawn apart from the project's
labels, and grey if the palette doesn't have its class. In the waveform, the
same keys give verdicts on a selected row, dragging its edges corrects its
times, and drawing a region adds a row for a sound the list didn't have
("list+ Dog"; Delete takes it back). A row whose recording isn't in the
project shows its clip instead, at its place in the recording: the time axis
reads the recording's time, and padding before the recording's start is shaded.
The row's event starts playing before the rest of its recording is read.

Results are written beside the list, never over it: `run.csv` gives
`run.results.csv`, with every column of the list and then:

| Column | |
|---|---|
| `verdict` | `correct`, `relabel`, `not_event`, or `added` for a row the reviewer added; empty while unheard |
| `new_label` | The class it was relabelled to, or an added row's class |
| `new_start_s`, `new_stop_s` | Where it was moved to, if it was, whatever the verdict |
| `note` | The reviewer's note |

A column of the list with one of those names (say the pipeline's own `note`)
is written as `list_note`, so neither is lost. Added rows have no `id`.
`run.results.json` records which list the results belong to (its path and a
hash of its contents as it was opened), when, and the app's version.
"Changes file" also writes `run.changes.csv`, with only the rows the review
changed. Results are read back when the list is opened again, matched by `id`
(or by path, start and label), with the added rows, and the list opens at the
first row not heard yet, with its filter and sort.

File > Import review edits (or "Import edits..." in the list's window) applies
a results file to the project's labels, after a summary of what it would
change. A row acts only on a label of its own class whose span holds its
start: correct confirms it, relabel changes its class, not_event rejects it,
new times move it, and added makes a label. Where its class has no label
there, correct and relabel make one over the row's span. Rows whose recording
isn't in the project, or whose class the palette lacks, are listed, with the
choice to add those classes to the palette. It's one undo step, importing the
same results again changes nothing, and an option takes the done mark off the
recordings it changes, so they're checked again.

A project made for reviewing lists over large datasets can set
`"scan-roots": false`: the dataset roots aren't walked, and the file list holds
only the recordings the label file and the open list name. Status and export
don't apply there.

## Controls:

### Waveform
- Left click drag to define region
- Left click a label to select only it (on the spectrogram, where boxes nest,
  each click selects the next one); click empty space to clear the selection
- Ctrl+click to add a label to the selection, or remove it
- Shift+click to place the playhead there and play
- Wheel to zoom
- Right drag to zoom
- Middle double click to unzoom

Labels marking a point in time rather than a region (from older label files,
or imported from tools that write points) are drawn as narrow boxes with a
flag. Drag an edge to widen one into a region, or select them and press E.

Labels use the full height and share it only where they overlap in time, like
overlapping calendar events. A lane per class, or every label at full height,
can be chosen under Options > Display, along with the channel of a
multi-channel file, the waveform gain (in dB, up to 60), and a spectrogram (linear, log or mel
scale) shown below the waveform. The two share a time axis, the split between
them can be dragged, and labels can be selected, drawn and moved on either.

On the spectrogram, a drag also sets the label's frequency band: drag a box
around a sound and only that band is played, filtered from just that stretch of
the recording (high-pass and low-pass, or a band-pass for a narrow band; a box
reaching the top or bottom leaves out that filter). With no class selected the
box is a draft, so it can be listened to and then given a class, or deleted. A
band's edges can be dragged on the spectrogram; on the waveform it looks and
behaves as before. Labels drawn on the waveform cover every frequency.

For sounds too high to hear well, "Shift high bands down" (Options > Playback)
moves a band down to start at a chosen frequency, keeping its width and timing.
A slower speed lowers everything, so the two combine for wide or ultrasonic
bands. Bands are kept in the label file (`low_hz`, `high_hz`), but not yet
written to annotation trees or exported clips.

A file opens showing all of it, or its first few seconds if "View on opening"
is set. While playing, the view pages on to keep the playhead in sight, unless
"Follow the playhead" is off or you've scrolled away from it.

Quiet recordings can be played louder with a fixed playback gain, or with "Auto
gain", which raises quiet passages towards a comfortable level (and turns loud
ones down) as they play, up to its maximum gain. Background noise is raised
too. The output is clipped to full scale, so a sudden loud sound can't blast.

### Label Palette
The label palette uses a "paint class" style:
- Selecting a class applies that class to all selected labels / regions
- The selected class applies to regions created while its selected.
- Label palette buttons have toggle state
- Ctrl+click a class (or Ctrl+1-9) to add a label of it over each selected
  label's span, instead of relabelling. The selection and the active class
  stay, so several classes can be added in a row: Ctrl+Shift+A for a region
  over the whole recording, then Ctrl+click "wind" and "insects". Each is one
  undo step. An unclassified selection (drawn with no class active) is given
  the class instead, since it has none to keep. A span the class already has a
  label over is skipped, with a warning in the console.
- Favourites: right-click a class to pin it to the row at the top (and again to
  unpin). While any are pinned, 1-9 choose the favourites in the order they
  were pinned; otherwise the first nine classes of the palette. Each button
  shows its number key. Favourites are remembered between runs, by class name.
- Find: type in the box beside the favourites (Ctrl+F puts the keyboard there)
  to show the classes whose names contain every word typed, those starting
  with it first. Enter chooses the first match; Escape clears the box.

### Labels
The list selects the same way as the waveform:
- Single click selects only that label
- Ctrl click to add to or remove from the selection

### Hot keys:
The list is also in the app, under Options > Keyboard shortcuts.

- 1-9: choose a favourite class (or one of the first nine in the palette if
  none are pinned), which also applies it to the selected labels (confirming
  suggestions)
- Ctrl+1-9: add a label of that class over each selected label, keeping the
  selection and the active class
- Ctrl+F: find a palette class by name
- Spacebar: toggle play/stop
- Tab / Shift+Tab: select and play the next / previous label, or suggestion in
  review mode
- R: review mode
- Enter: accept the selected suggestions
- X: reject the selected labels (not an event)
- H: show / hide rejected candidates
- O: add a region from under the mouse (or the playhead) to where the sound
  there ends, estimated from its level falling back to the background
- E: widen the selected zero-length labels the same way
- A: snap the selected zero-length labels to the sharpest rise within a few
  milliseconds (Ctrl+Z undoes it). Region starts move too only if "Snap region
  starts" is set in the project settings (`snap_regions` in `review`).
- Left / Right: move the selected labels by the nudge step (10 ms by default,
  set under Options); with Alt, by the fine step (1 ms)
- Ctrl+Left / Ctrl+Right: move their start edges
- Shift+Left / Shift+Right: move their stop edges
- Delete: delete the selected labels
- Ctrl+A: select all the labels in the file (rejected ones only if shown)
- Ctrl+C: copy the selected labels (not rejected candidates)
- Ctrl+V: paste them at the same times as suggestions, eg. into another
  recording of the same scene. They stay selected, so the arrow keys move them
  together.
- Ctrl+Shift+V: paste with a shift (eg. the recordings' start offset), as
  suggestions or labels
- Ctrl+Shift+A: add a region of the active class over the whole file and
  select just it (or select the one already there)
- Escape: deselect all labels / selections and the active palette brush
- Ctrl+Z: undo label changes
- Ctrl+Y or Ctrl+Shift+Z: redo
- N / P: next / previous file in the file list (as filtered and sorted)
- Alt+N / Alt+P: select and play the next / previous label in the file of the
  classes the file list is filtered by
- D: mark the file done and open the next one to do in the file list. A done file with no
  labels is taken to contain none of the classes.
- S: mark the file skipped and open the next one to do in the file list

### Files
Each file is to do, done or skipped. The file list shows this, and the number
of labels in each file (and suggestions, with a `?`), and can be filtered by it
or to files with suggestions to review.

To find recordings, the box above the list keeps the files whose path (the
dataset root as written in the project, then the path below it) contains every
word typed, ignoring case. Beside the status filter, "By class" keeps the files
with labels of any of the chosen classes: type to add one (Enter adds the first
match), or right-click a class in the palette and choose "Filter files by it".
Confirmed labels count, and suggestions and rejected candidates too if ticked.
A column shows each file's count of them; click its header to sort by it. In the
open file, Alt+N / Alt+P step through those labels, to hear and relabel them
(1-9). The search, the filters and the order are remembered between runs, with
the other file list settings.
