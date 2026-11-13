# Audio Labeller application

This is an application for strong labelling of audio. 

Features:
- Multi audio file projects export to a single label file
- Define the temporal location of the sound in addition to the class: regions
  with a start and stop, or onsets (a point in time) for transients
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

## Motivation
- Audacity provides labelling but AFAIK these need to be manually typed 
rather than selected from a predefined dictionary / palette.
Additionally, it might be tricky in the multi file case.
Finally, with short sounds the playback render omitts the sound on some systems.
- Label Studio almost worked for me, but I couldn't quite configure it just how I wanted.

At the time, it seemed easier to make my own application rather than forking / editing existing similar tools.

## Building

Dependencies:

| Dependency          | How it's found                                       |
| ------------------- | ---------------------------------------------------- |
| SDL3                | `external/SDL` submodule, or installed with `-DAUDIOLAB_SDL_SUBMODULE=OFF` |
| Dear ImGui, ImPlot  | `external/imgui`, `external/implot` submodules       |
| nlohmann/json       | `find_package(nlohmann_json)`, eg. vcpkg `nlohmann-json` |
| spdlog (with fmt)   | `find_package(spdlog)`, eg. vcpkg `spdlog`           |
| dr_libs             | `dr_wav.h`, `dr_flac.h`, `dr_mp3.h` on the include path, eg. vcpkg `drlibs` |
| doctest             | `find_package(doctest)`, for the tests only          |
| OpenGL              | The system's                                         |

A C++23 compiler (recent Clang, GCC or MSVC) and CMake 3.22 or later.

```bash
git submodule update --init --recursive
vcpkg install nlohmann-json spdlog drlibs doctest
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
build/tests/audio_labeller_tests
```

`CMakePresets.json` has a hidden `base` preset to inherit from in your own
`CMakeUserPresets.json`, where compilers and toolchain paths go. Options:
`AUDIOLAB_BUILD_TESTS` (on), `AUDIOLAB_ENABLE_WARNINGS` (on; warnings for the
app's own code) and `AUDIOLAB_SDL_SUBMODULE` (on).

## Project definition and files

1. Create your project's label palette according to `schemas/label-palette.schema.json`
2. Define your project's paths (data set, export path etc) according to `schemas/project.schema.json`
3. Run the app, this should produce an app settings .json file in the working directory.
4. In the app settings, set the project to point to your project .json

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
layout, file filter, nudge steps, playback speed and gain, spectrogram) between runs.
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
be deleted once exported ("exported" in the file list). See `ARCHITECTURE.md`
for the format and the `export` settings in the project file.

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
  recording done.
- Label text is matched to palette classes by name, ignoring case. Names not
  in the palette are added for the session, in grey.

## Reviewing proposals

Press R for review mode. Tab then steps through the suggestions in every file
(lowest score first, if they have scores), opening each file, zooming to a
window around the suggestion and playing it with a short pre-roll. Enter
accepts, X rejects and 1-9 relabels, each moving on to the next. The `review`
section of the project sets the windows.

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
      "options": { "margin_db": 15, "onsets": true } }
]
```

A command runs once per recording, without a shell, and writes its
suggestions to `{output}` in the given format (or prints them, if there's no
`{output}`), exiting with 0. An optional score column orders the review. Its
output is logged, and shown if it fails. `examples/example_detector.py` is a
template. The built-in finds sounds some decibels above the background
(regions, or onsets snapped to their rise), with its settings adjustable in the
window. Running again doesn't add the same suggestion twice.

## Checking cut clips

A `clip-manifest` section in the project points at a CSV of clips cut by
another tool, with each clip's source recording (relative to a dataset root),
onset in it and label. The Clip Manifest window lists them, filterable by words
or `column=value` and sortable by any column. With the list focused, Up/Down
play each clip, Enter marks it correct, X not an event and 1-9 relabels it, and
J opens the recording at that time. Verdicts change the label at that onset in
the source recording, so they land in the annotations and survive the next
re-cut.

## Controls:

### Waveform
- Left click drag to define region (an onset, for classes marked as onsets)
- Ctrl left click to set playhead cursor
- Left click to toggle label selection
- Wheel to zoom
- Right drag to zoom
- Middle double click to unzoom

Labels use the full height and share it only where they overlap in time, like
overlapping calendar events. A lane per class, or every label at full height,
can be chosen under Options > Display, along with the channel of a
multi-channel file, the waveform gain, and a spectrogram (linear, log or mel
scale) shown below the waveform. The two share a time axis, the split between
them can be dragged, and labels can be selected, drawn and moved on either.

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
- Classes marked `"kind": "onset"` in the palette are shown with a bar

### Labels
- Ctrl click to select multiple
- Single click selects only that label

### Hot keys:
The list is also in the app, under Options > Keyboard shortcuts.

- 1-9: choose a palette class, which also applies it to the selected labels
  (confirming suggestions)
- Spacebar: toggle play/stop
- Tab / Shift+Tab: select and play the next / previous label, or suggestion in
  review mode
- R: review mode
- Enter: accept the selected suggestions
- X: reject the selected labels (not an event)
- H: show / hide rejected candidates
- O: add an onset under the mouse, or at the playhead
- A: snap the selected onsets to the sharpest rise within a few milliseconds
  (Ctrl+Z undoes it)
- Left / Right: move the selected labels by the nudge step (10 ms by default,
  set under Options); with Alt, by the fine step (1 ms)
- Ctrl+Left / Ctrl+Right: move their start edges
- Shift+Left / Shift+Right: move their stop edges
- Delete: delete the selected labels
- Escape: deselect all labels / selections and the active palette brush
- Ctrl+Z: undo label changes
- Ctrl+Y or Ctrl+Shift+Z: redo
- N / P: next / previous file in the file list (as filtered)
- D: mark the file done and open the next one to do. A done file with no
  labels is taken to contain none of the classes.
- S: mark the file skipped and open the next one to do

### Files
Each file is to do, done or skipped. The file list shows this, and the number
of labels in each file (and suggestions, with a `?`), and can be filtered by it
or to files with suggestions to review.
