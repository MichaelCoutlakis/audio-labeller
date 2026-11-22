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
finer one on Alt, view and editing options remembered between runs,
stacked labels that share the height only where they overlap (class lanes and
full height remain as options), and adding classes over a selection (Ctrl+click
a class or Ctrl+1-9, with Ctrl+Shift+A for a region over the whole file).

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
- [x] Export leaves a stale file behind when a recording stops being done
      (marked skipped or to do after an earlier export): a reader takes it as
      that recording's reviewed labels. Export now records each file it writes
      (path and a hash of its contents, per tree) and removes it once the
      recording is no longer done, or lists it if someone changed it since.

Review lists (the clip manifest as a listening list):

The use case: a training pipeline writes a list of events worth hearing (the
ones a model gets wrong or is unsure about, with a reason, its prediction and
a score per row), and someone listens to each and gives a verdict. The events
come from many recordings in several datasets, and many are quiet field
recordings that need gain to be heard. Most of those datasets keep their labels
outside the labeller (their own metadata or CSV files), and only some
recordings have labels in the project's label file. So a review produces an
output of its own, the results file, applied the same way for every dataset;
applying it to the project's labels is a separate, explicit step.

The list has a format of our own: a CSV, as pandas, R or a spreadsheet writes
it, needing nothing of the project to write and nothing of the label file to
read the results. Another tool's CSV is read through the `columns` mapping in
the project. The same list is the label queue of "Reviewing existing labels" in
Phase 7. A review always happens in a project, even one made just for it, for
its palette, high-pass and channel settings and its dataset roots.

Done: the format (`path`, `start_s`, optional `stop_s` or `duration_s`,
`label`, `root`, `channel`, `clip_path`, `id`, other columns passed through),
`<list>.results.csv` beside the list with `verdict`, `new_label` and `note` and
undo, stepping through the rows in the recording view with its gain, speed and
high-pass, File > Open review list with recent lists, and carrying on at the
first row not heard, with the filter and sort remembered per list.

The plan, in order:

- [x] File list speed, for every project: with tens of thousands of files the
      app is slow to use, because the filtered file list is rebuilt from every
      file twice a frame (the file list view, and the keyboard handler before
      it knows whether N or P was pressed), with map lookups by path per file,
      `to_portable` per file and root when searching, and the done count
      looping over every file again. Keep the filtered list and its counts
      until the labels, statuses, file list or filter settings change (the
      project's revision counts changes), build it for N/P only when they're
      pressed, and keep each file's searchable path from the walk.
- [x] Reviewing never touches the label file: verdicts go only to the results
      file, and undo restores only the row. No labels are created for rows
      without one. The "Now in the recording" column stays, to compare a row
      with the label the project has there.
- [x] Results as a diff of the list, with every row: every input column, then
      `verdict` (`correct`, `relabel`, `not_event`, or `added` for a row the
      reviewer added; empty while unheard), `new_label`, `new_start_s` and
      `new_stop_s` (set whenever the times were changed, whatever the
      verdict, so a sound can be both relabelled and moved) and `note`. Added
      rows have an empty `id` and their own `path`, `root`, times and label,
      and are read back as rows when the list is opened again. The full file
      is always written, since it's also where the progress is kept; an
      option writes `<list>.changes.csv` beside it with only the changed rows.
      A sidecar `<list>.results.json` records the list's path, a hash of its
      contents, the time and the app version, so a pipeline can tell results
      that belong to an older version of a regenerated list (a comment line
      in the CSV would trip up most CSV readers). Rows are matched on `id`,
      else on path, start and label.
- [x] Clips carry their place: a row with a `clip_path` must have a
      `clip_offset_s`, where the clip starts in its recording (our clip
      export's `source_offset_s` can be mapped to it with the project's
      `columns`). A list with clips but no offset column, or a clip row
      without a number there, isn't opened, and the error names the column or
      row. All times stay in the recording's time; the event in the clip is
      `start_s - clip_offset_s` to `stop_s - clip_offset_s`. A negative offset
      is a clip that starts with padding before its recording's start (eg.
      clips of one length cut around an event near the start): the clip is
      drawn from that negative time, with the padding shaded.
- [x] Draw a label for each row: today stepping to a row shows only what the
      label file already has there, so a row from a dataset whose labels live
      elsewhere shows nothing, and where the pipeline put the event can't be
      seen. Instead, each row is drawn as a label from `start_s` to its stop,
      with its class name and a line at the start, since whether the onset
      sits on the sound's start is part of what's judged. It's styled apart
      from the label file's labels, so what the list claims and what the
      project has can be told apart where both are drawn. A row's class
      needn't be in the palette: it's drawn grey with its name, and
      relabelled with 1-9 to a palette class. Enter, X and 1-9 give the
      verdict; dragging its edges corrects its times (`new_start_s`,
      `new_stop_s`); drawing a region while a row is shown adds a row
      (`added`) for a sound the list didn't have. None of it goes into the
      label file. (How: the rows are held as labels in `app_state`, so the
      existing drawing, selecting, dragging and nudging work on them.)
- [x] Rows with only a clip: a row whose recording isn't in the project shows
      its clip in the waveform and spectrogram instead, with the row's label
      drawn where the event falls in it, and plays it there with the gain,
      speed and high-pass. The time axis shows the recording's time (a clip
      cut from 120 s starts at 120 s), so the label's times read the same as
      in the list and the results, and a clip looks like its recording apart
      from how much of it there is; the clip is placed at `clip_offset_s`, as
      band playback places an excerpt in its recording. Dragging and adding
      rows work as on a recording. What a clip can't take is the project's
      own labels: the label file keys labels by recording, and the clip's
      recording isn't in the project, so drawing on a clip adds a row, never
      a label. An event only partly inside its clip is marked on its row. A
      row's `channel` applies to the recording, not the clip, which plays as
      it is.
- [x] Import review edits (File > Import review edits...): read a
      `<list>.results.csv` and apply its verdicts to the labels of recordings
      in the project, as one undo step. A row changes only a label of its own
      class whose span holds its start (not another class's, which may be an
      unrelated label from another dataset); rows with no class change
      nothing. `correct` leaves the label, `relabel` changes its class,
      `not_event` rejects it (`x:`), new times move it, `added` makes a
      label (unless the same one is there). Classes not in the palette are
      listed, with the choice to add them. First a summary: labels changed,
      added and rejected, and rows that can't be applied (recording not in
      the project, no label of the class there). Importing the same results
      again changes nothing. It never marks a recording done; an option,
      on by default, takes the done mark off recordings it changed, so
      they're checked again.
- [x] Review a list without walking the datasets: `"scan-roots": false` in a
      project, as a project made for reviewing lists would have. The
      recordings a list names are found by path under the roots, and the file
      list holds just those and the label file's. Status and export are
      hidden there; the results file is the output.
- [ ] For ordinary projects, walk the dataset roots in the background with the
      file list filling in, and keep an index of the last walk, so a project
      on a slow drive opens at once.
- [x] Start playing sooner in another recording: stepping to a row in a
      recording not open yet reads the whole file, high-passes it and builds
      the waveform pyramid before playing (about 1.7 s for a 10 minute,
      4-channel, 24-bit file in a debug build). Read and play the event's
      stretch first, then load the rest (see "Load audio off the UI thread" in
      Phase 4). Playback doesn't wait for drawing: the waveform draws on the
      next frame and the spectrogram tiles on worker threads.
- [x] Documents for all of the above: README (review lists: the format with
      `clip_offset_s`, the results and changes files and sidecar, a label
      drawn for each row, grey classes, clips shown in the recording's time,
      import review edits),
      ARCHITECTURE (review lists produce results and never edit the label
      file; import as the one way into it; rows as labels in `app_state`;
      clips placed at their offset in the recording's time; file list
      caching), the schema
      (`clip_offset_s`, the walk setting for dataset roots),
      `examples/review-list.csv` (a row with a clip and offset, and an
      example results file), CHANGELOG, and tests for each.
- [ ] Later: play on. Rows play one after another with a short gap; Space
      stops, and Shift+Enter marks every row played since the last verdict
      correct, so a run of right ones costs one key. Filtering on
      `label=<class> verdict=` gives the queue for one class.

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

- [x] Drop the `"kind"` key from the palette (and `class_kind`): every class is
      a region class. Ignore the key when reading old palettes, with a notice.
      Update the palette schema, README and ARCHITECTURE.
- [x] Zero-length labels: keep reading and drawing them, but give a quick way
      to make one a region (drag an edge, or a key that extends it to an end
      estimated from the energy envelope). Decide whether O, which places one,
      stays.
- [x] Clip manifest verdicts: find a clip's label as the one whose span holds
      the clip's onset (nearest start if several), not by its start alone
      (`m_match_s`): a region's start can be well before the onset a pipeline
      finds in it. A verdict that creates a label should make a region (the
      clip's span) rather than a zero-length label.
- [x] Review settings: `onset_pre_s` / `onset_post_s` apply only to zero-length
      labels now; regions are played with `pre_roll_s` to their stop. Rename or
      fold them together, keep reading the old keys, and retune the defaults
      for regions.
- [x] Snapping (A): apply it to a region's start only as an option, since the
      start no longer has to be an onset.
- [x] Loudness assisted labeller: drop its `onsets` option, or have it write
      regions from the snapped rise to where the level falls back.
- [x] Clip export: check the padded and fixed modes with long regions (a
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
- [x] Labels from another recording of the same scene: several recorders (or
      microphones) at one site hear mostly the same sounds, so once one file
      is labelled, its labels are copied (Ctrl+C) and pasted into another as
      suggestions (Ctrl+V), or with a shift for the recordings' start offset
      (Ctrl+Shift+V). They stay selected, so the arrow keys nudge the whole
      set by hand; snapping (A) aligns each start. It's a review,
      not a copy: Enter accepts, X drops a sound the second recorder missed.
      Sounds only the second recorder heard still need the assisted labellers
      or a listen through.
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

- [x] A text box above the file list that keeps the files whose path contains
      all of its words (case-insensitive), combined with the status filter.
- [x] Filter by class, a built-in file filter (as the loudness labeller is a
      built-in assisted labeller), listed in the same combo as the external
      file filters below:
      - Choose one or more classes with a search box like the palette's
        (Ctrl+F style), or from a class's right-click menu in the palette
        (Ctrl+click now adds a class over the selection).
      - [ ] A group class takes in its subclasses, once the palette has groups.
      - Keeps the files with at least one confirmed label of the chosen
        classes; an option includes suggestions (and `x:` candidates).
      - Adds a column with each file's count of them, sortable.
      - Combines with the status filter and the text box, so "done files with
        `human` labels" is one choice; remembered between runs.
      - In the open file, N / P with Alt (or similar) jump to the next or
        previous label of the chosen classes, so they can be heard and
        relabelled (1-9) without the full review mode below.
      - Needs no external command, so it works in any project: the first
        thing to build in this phase, and the class choice the label review
        queue below reuses.

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
      labels rather than files, as a review list (the format in Phase 3b:
      `path`, `start_s`, `stop_s` and optionally `label` and a `reason`), so a
      project's own script decides what needs another look: labels of a
      class, labels an earlier model disagrees with, labels from one session.
      It is stepped like any review list, each row matched to the label whose
      span holds it.
- [ ] Progress through the queue survives closing the app, as a review list's
      does (Phase 3b), so a long relabel pass can be done over several
      sittings.
- [ ] Flag a label to come back to: a sound that's hard to place gets its best
      coarse class now, plus a flag (a key, and a mark drawn on the label) and
      an optional free-text note ("like a bag of heavy items put down on
      wood"). Flagged labels are a review queue of their own, and a filter in
      the file list. Store both in the label file; write them where a format
      has room (a Raven column, a DCASE extra column) and leave them out where
      it doesn't (Audacity), saying so in the export notes. The flag is about
      the labelling, not the sound, so it's never a class.

## Lower priority

Done: FLAC and MP3 support, and case-insensitive extension matching.

- [ ] Export options to downmix and resample clips, to save space.

## Build and housekeeping

Done: every dependency as a submodule in `external/` by default, so a clone
builds without a package manager, or installed ones with `AUDIOLAB_SYSTEM_DEPS`
(`AUDIOLAB_SDL_SUBMODULE` for SDL alone), README build steps and dependency list, C++23,
`AUDIOLAB_ENABLE_WARNINGS` (warnings for the app's own code, which builds
clean with Clang), UTF-8 project paths and a system font with wide coverage.

- [ ] Add a `vcpkg.json` manifest and CI. Note that a `vcpkg.json` switches
      existing vcpkg toolchain builds into manifest mode.
- [ ] Remove the backward compatibility kept so far (see CONTRIBUTING.md):
      the review settings' older keys (`onset_pre_s`, `onset_post_s`), the
      palette's ignored `"kind"` key, and any other reader of an older
      format. Check each against the files actually in use first, eg. label
      files of an older version.
- [x] Provide example audio: `*.wav` is git-ignored, so the example project
      couldn't run from a fresh clone. `examples/make_example_audio.py` makes
      it: synthetic animal calls at the times the example labels and review
      list use, with two clips (one padded before its recording's start).
- [ ] Label scores are kept as 32-bit floats, so a saved label file writes
      0.41 as 0.4099999964237213. Keep them as doubles, or round when
      writing.
