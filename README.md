# Audio Labeller application

This is an application for strong labelling of audio. 

Features:
- Multi audio file projects export to a single label file
- Define the temporal location of the sound in addition to the class
- Playback of the audio and labels.

## Motivation
- Audacity provides labelling but AFAIK these need to be manually typed 
rather than selected from a predefined dictionary / palette.
Additionally, it might be tricky in the multi file case.
Finally, with short sounds the playback render omitts the sound on some systems.
- Label Studio almost worked for me, but I couldn't quite configure it just how I wanted.

At the time, it seemed easier to make my own application rather than forking / editing existing similar tools.

## Controls:

### Waveform
- Left click drag to define region
- Ctrl left click to set playhead cursor
- Left click to toggle label selection
- Wheel to zoom
- Right drag to zoom
- Middle double click to unzoom

### Label Palette
The label palette uses a "paint class" style:
- Selecting a class applies that class to all selected labels / regions
- The selected class applies to regions created while its selected.
- Label palette buttons have toggle state

### Labels
- Ctrl click to select multiple
- Single click selects only that label

### Hot keys:
- Escape: deselect all labels / selections
- Delete: delete all selections
- Spacebar: toggle play/stop