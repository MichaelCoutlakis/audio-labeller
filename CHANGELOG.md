# Changelog

## Unreleased

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
