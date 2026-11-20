# Changelog

## Unreleased

### Review lists

- The clip manifest is now a review list in a format of the app's own: a CSV
  with `path` and `start_s`, and optionally `stop_s` or `duration_s`, `label`,
  a per-row dataset `root`, `channel`, `clip_path` and `id`. Other columns are
  shown and sortable. Cut clips are no longer needed, and older manifests
  (`source_path`, `onset_s`) are still read.
- Stepping through a list opens each event in its recording, zoomed to it with
  its label selected, and plays it through the recording view's gain, speed and
  high-pass. A row's clip is played only when its recording isn't in the
  project.
- Verdicts and a note per row (N) are written to `<list>.results.csv` beside
  the list, with every column of the list plus `verdict`, `new_label` and
  `note`, and undone with Ctrl+Z along with the label they changed.
- File > Open review list, and recent lists. The last list reopens with its
  project, at the first row not heard yet, with its filter and sort.

### Finding recordings

- A search box above the file list keeps the files whose path contains every
  word typed (case-insensitive), combined with the status filter.
- "By class" beside the status filter keeps the files with labels of the
  chosen classes (optionally counting suggestions and rejected candidates),
  with a sortable column of each file's count. Classes are chosen by name in
  the file list, or from a class's right-click menu in the palette. Alt+N /
  Alt+P step through those labels in the open file. N / P follow the list's
  order.
- The search, filters and order are remembered between runs.

### Labelling

- Copy and paste labels: Ctrl+C copies the selected labels, Ctrl+V pastes them
  as suggestions at the same times, for another recording of the same scene,
  and Ctrl+Shift+V pastes them with a shift (eg. the recordings' start offset)
  as suggestions or labels. The pasted labels stay selected, so the arrow keys
  move them together.
- A label moved whole (by dragging or the arrow keys) keeps its length at the
  ends of the file, rather than being squashed.

### Exchanging label files

- Annotation export removes the label file it wrote for a recording that's no
  longer done, so it isn't read as reviewed. A file changed since it was
  written is listed instead and left in place. The label file records what
  was written under each file's `annotation-exports`.
- Clip export likewise removes the clips and entries of a file exported
  earlier but no longer marked done, and clears its "exported" mark. Export
  records now keep the file's source ID.

### Selecting and playing

- A click on a label in the waveform or spectrogram selects only it, as in the
  Labels list; a click on empty space clears the selection. Ctrl+click adds a
  label to the selection or removes it. Before, every click added to it.
- Shift+click places the playhead and plays from there, which Ctrl+click did.
- Selecting a label in the Labels list sets the playback window to it, as Tab
  does, so Space plays it.
- Clicking the highlighted palette class (or pressing its number) applies it to
  selected labels that don't have it, rather than turning it off first.

### Building

- Every dependency is a submodule in `external/` (nlohmann/json, spdlog,
  dr_libs and doctest join SDL, Dear ImGui and ImPlot), built with the app by
  default, so a clone builds without a package manager.
  `-DAUDIOLAB_SYSTEM_DEPS=ON` uses installed packages instead, as vcpkg builds
  did before; existing vcpkg presets need it to keep doing so.

### Regions only

Labels mark a sound from where it starts to where it stops; there's no longer
a separate onset kind of label.

- Palettes: every class marks regions. `"kind": "onset"` is ignored, with a
  notice naming the classes it marked.
- Zero-length labels (from older label files, or tools that write points) are
  still read, and drawn as narrow boxes with a flag. Drag an edge to widen one,
  or press E to widen the selected ones to where the sound ends, estimated from
  the energy envelope.
- O adds a region from under the mouse (or the playhead) to where the sound
  ends, rather than a point.
- Review settings: `pre_roll_s` is played before every label, which plays to
  its stop; `point_post_s` replaces `onset_post_s` for zero-length labels.
  `onset_pre_s` and `onset_post_s` are still read when the new keys are
  missing. New defaults: 0.2 s pre-roll, and review mode shows 0.5 s either
  side of a suggestion, at least 1 s after its start.
- Snapping (A) moves region starts only if `snap_regions` is set in the review
  settings; zero-length labels always snap.
- Loudness labeller: `onsets` is now `snap_starts`, regions that start at the
  sharpest rise and end where the level falls back. `onsets` is read as
  `snap_starts` when that's missing.
- Clip manifests: a verdict edits the label whose span holds the clip's onset
  (the clip's class first, then the nearest start). A label it creates is a
  region, to the clip's stop from new optional `stop` or `duration` columns, or
  `region_s` (1 s) long.
- Clip export: in padded mode, a label inside a region longer than the maximum
  clip joins the region's clip instead of getting a clip of its own; a
  zero-length label at either end of a file is now in its clip.
