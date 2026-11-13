#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Michael Coutlakis
"""A template for an assisted labeller: the app runs it once per recording.

In the project file:

    "assisted-labellers": [
        { "name": "Example detector",
          "command": ["python", "example_detector.py", "{audio}", "{output}", "--class", "Click"],
          "format": "audacity" }
    ]

Contract: read the recording at {audio}, write suggestions to {output} in the
labeller's format (Audacity here: start<TAB>stop<TAB>label, with an optional
fourth column for a score from 0 to 1), and exit with 0. An empty file means no
suggestions. Anything printed is kept in a log, and shown if the exit code isn't
0. Labels starting with "x:" are rejected candidates, hidden in the app unless
shown. A label that isn't a palette class is added for the session in grey.

This one marks the loudest 20 ms frames of a 16-bit PCM WAV (standard library
only); replace detect() with a real model.
"""
import argparse
import array
import math
import sys
import wave


def detect(path, frame_s=0.02, top=10):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit("This example only reads 16-bit PCM")
        rate, channels = w.getframerate(), w.getnchannels()
        samples = array.array("h", w.readframes(w.getnframes()))
    frame = max(1, int(rate * frame_s)) * channels
    levels = []
    for i in range(0, len(samples) - frame, frame):
        chunk = samples[i:i + frame]
        rms = math.sqrt(sum(x * x for x in chunk) / len(chunk)) / 32768
        levels.append((rms, i / channels / rate))
    loudest = max((r for r, _ in levels), default=0) or 1
    for rms, t in sorted(levels, reverse=True)[:top]:
        yield t, t, rms / loudest  # An onset, with a score


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("audio")
    parser.add_argument("output")
    parser.add_argument("--class", dest="cls", default="Click")
    args = parser.parse_args()

    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        for start, stop, score in sorted(detect(args.audio)):
            out.write(f"{start:.6f}\t{stop:.6f}\t{args.cls}\t{score:.3f}\n")
    print(f"Wrote suggestions for {args.audio}", file=sys.stderr)


if __name__ == "__main__":
    main()
