#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Michael Coutlakis
"""Make the example project's audio: synthetic calls over light background noise, a dog's bark,
a cat's meow, birdsong, and for the fox, a yip or a tap on glass.

Writes examples/audio/farm.wav and meadow.wav (the project's recordings), two clips in
examples/clips/, cut from a recording that isn't in the project, for the example review list,
and examples/calls.wav, each call once, to compare them.
The calls are at fixed times, which the example labels and review list use, and the noise is
seeded, so every run writes the same files. Standard library only.

    python3 examples/make_example_audio.py
"""

import math
import random
import struct
import wave
from pathlib import Path

RATE = 22050
HERE = Path(__file__).resolve().parent


def silence(seconds):
    return [0.0] * int(seconds * RATE)


def mix(buffer, sound, at_s, gain=1.0):
    """Add a sound into a buffer, starting at a time in seconds"""
    start = int(at_s * RATE)
    for i, v in enumerate(sound):
        if 0 <= start + i < len(buffer):
            buffer[start + i] += v * gain


def envelope(n, attack_s, release_s):
    """Rise over the attack, fall over the release, both smooth"""
    a = max(1, int(attack_s * RATE))
    r = max(1, int(release_s * RATE))
    out = []
    for i in range(n):
        e = 1.0
        if i < a:
            e = math.sin(0.5 * math.pi * i / a) ** 2
        if i > n - r:
            e *= math.sin(0.5 * math.pi * (n - i) / r) ** 2
        out.append(e)
    return out


def voiced(f0_at, seconds, formants, harmonics=12, attack=0.01, release=0.08, breath=0.0, rng=None):
    """A voiced call: harmonics of a moving pitch, shaped by formants (centre Hz, width Hz)

    f0_at(t) gives the pitch at a fraction t of the way through, and formants(t) the formants.
    """
    n = int(seconds * RATE)
    env = envelope(n, attack, release)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / n
        f0 = f0_at(t)
        phase += 2.0 * math.pi * f0 / RATE
        v = 0.0
        for k in range(1, harmonics + 1):
            f = k * f0
            if f > RATE / 2 - 500:
                break
            weight = sum(math.exp(-(((f - c) / w) ** 2)) for c, w in formants(t))
            v += weight * math.sin(k * phase) / k**0.5
        if breath and rng:
            v += breath * rng.uniform(-1.0, 1.0)
        out.append(v * env[i])
    peak = max(abs(v) for v in out) or 1.0
    return [v / peak for v in out]


class bandpass:
    """A resonance: a two-pole band-pass filter (RBJ biquad, constant peak gain)"""

    def __init__(self, centre_hz, q):
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0
        self.tune(centre_hz, q)

    def tune(self, centre_hz, q):
        """Move the resonance, keeping the filter's state, eg. as a mouth changes shape"""
        w = 2.0 * math.pi * centre_hz / RATE
        alpha = math.sin(w) / (2.0 * q)
        a0 = 1.0 + alpha
        self.b0, self.b2 = alpha / a0, -alpha / a0
        self.a1, self.a2 = -2.0 * math.cos(w) / a0, (1.0 - alpha) / a0

    def __call__(self, x):
        y = self.b0 * x + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def woof(rng):
    """A dog's bark, 'wuff': made as a voice is, a buzz (a rounded, rough pulse at a pitch that
    rises then falls) with breath noise, through the mouth's resonances, which open from 'w' to
    'u' and close again. It swells in over a few hundredths of a second and tapers away."""
    seconds = 0.22
    n = int(seconds * RATE)
    # Resonances (Hz) through the bark: closed 'w', open 'u', then closing a little (time, F1, F2)
    shape = [(0.0, 380.0, 850.0), (0.06, 750.0, 1300.0), (0.22, 620.0, 1150.0)]

    def at(t_s, k):
        for (t0, *v0), (t1, *v1) in zip(shape, shape[1:]):
            if t_s <= t1:
                return v0[k] + (v1[k] - v0[k]) * (t_s - t0) / (t1 - t0)
        return shape[-1][k + 1]

    f1, f2, f3 = bandpass(380.0, 4.0), bandpass(850.0, 5.0), bandpass(2600.0, 7.0)
    phase, jitter, low = 0.0, 1.0, 0.0
    out = []
    for i in range(n):
        t_s = i / RATE
        if i % 32 == 0:
            f1.tune(at(t_s, 0), 4.0)
            f2.tune(at(t_s, 1), 5.0)
        # Up quickly to the bark's pitch, then easing down only a little
        f0 = (250.0 + 130.0 * math.sin(0.5 * math.pi * min(1.0, t_s / 0.05)) - 300.0 * max(0.0, t_s - 0.05)) * jitter
        phase += max(60.0, f0) / RATE
        if phase >= 1.0:
            phase -= 1.0
            jitter = rng.uniform(0.94, 1.06)  # Each period a little off: a rough voice
        # A sawtooth, rounded off as a voice's pulses are, giving way to breath as it trails off
        low += (2.0 * phase - 1.0 - low) * 0.25
        voicing = 1.0 - 0.4 * min(1.0, max(0.0, (t_s - 0.1) / 0.1))
        source = voicing * low + rng.uniform(-1.0, 1.0) * (0.35 + 0.3 * (1.0 - voicing))
        v = f1(source) + 0.7 * f2(source) + 0.25 * f3(source)
        # Swells in over 25 ms, holds, then stops short: a quick fall, and a few milliseconds' fade
        # so it ends without a click
        rise = math.sin(0.5 * math.pi * min(1.0, t_s / 0.025)) ** 2
        fall = math.exp(-max(0.0, t_s - 0.11) / 0.03)
        end = min(1.0, (seconds - t_s) / 0.006)
        out.append(v * rise * fall * end)
    # Scaled by its average level rather than its peaks, then softly clipped, for some grit
    rms = math.sqrt(sum(v * v for v in out) / n) or 1.0
    return [math.tanh(v * 0.5 / rms) for v in out]


def meow(rng):
    """A cat's call: rising then falling, the vowel opening from 'ee' to 'ow'"""

    def pitch(t):
        return 480.0 + 260.0 * math.sin(math.pi * min(1.0, t * 1.4)) + 8.0 * math.sin(2 * math.pi * 6 * t)

    def formants(t):
        return [(2200.0 - 1300.0 * t, 450.0), (900.0 + 300.0 * math.sin(math.pi * t), 300.0)]

    return voiced(pitch, 0.7, formants, harmonics=10, attack=0.06, release=0.18, breath=0.05, rng=rng)


def tweet(rising=True):
    """A bird's chirp: a quick, clean sweep, high up"""
    n = int(0.09 * RATE)
    env = envelope(n, 0.015, 0.04)
    f_from, f_to = (3200.0, 5600.0) if rising else (5400.0, 3400.0)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / n
        phase += 2.0 * math.pi * (f_from + (f_to - f_from) * t * t) / RATE
        out.append(math.sin(phase) * env[i])
    return out


def song():
    """Three chirps, the last falling"""
    out = silence(0.42)
    for at, rising in ((0.0, True), (0.14, True), (0.30, False)):
        mix(out, tweet(rising), at)
    return out


def yip(rng):
    """A fox's yip: short, high and harsh, with a hoarse edge"""
    return voiced(
        lambda t: 820.0 + 700.0 * math.sin(math.pi * t),
        0.2,
        lambda t: [(1600.0, 500.0), (3000.0, 800.0)],
        harmonics=8,
        attack=0.01,
        release=0.07,
        breath=0.6,
        rng=rng,
    )


def tap():
    """Glass, or a cowbell, tapped with something metal: a bright click, then a few inharmonic,
    slightly beating partials that ring briefly"""
    seconds = 0.7
    n = int(seconds * RATE)
    f0 = 1150.0
    # (ratio to f0, level, decay time in seconds); close pairs beat, as struck metal does
    partials = [(1.0, 1.0, 0.32), (1.004, 0.6, 0.30), (1.48, 0.55, 0.22), (2.12, 0.45, 0.15),
                (2.67, 0.3, 0.1), (3.41, 0.25, 0.07), (4.36, 0.15, 0.05)]
    rng = random.Random(11)
    out = []
    prev = 0.0
    for i in range(n):
        t_s = i / RATE
        v = sum(level * math.exp(-t_s / decay) * math.sin(2.0 * math.pi * f0 * ratio * t_s)
                for ratio, level, decay in partials)
        if t_s < 0.003:
            noise = rng.uniform(-1.0, 1.0)
            v += 1.5 * (noise - prev) * (1.0 - t_s / 0.003)  # The click: bright, metal on metal
            prev = noise
        attack = min(1.0, i / (0.0005 * RATE))
        release = min(1.0, (n - i) / (0.05 * RATE))
        out.append(v * attack * release)
    peak = max(abs(v) for v in out) or 1.0
    return [v / peak for v in out]


def ding():
    """A small bell, struck: inharmonic partials ringing down, the higher ones faster, after a
    short metallic tick"""
    seconds = 1.4
    n = int(seconds * RATE)
    f0 = 880.0
    # Partials of a bell (ratio to the strike note, level, decay time in seconds)
    partials = [(0.5, 0.5, 1.2), (1.0, 1.0, 0.9), (1.19, 0.5, 0.7), (1.5, 0.35, 0.6), (2.0, 0.45, 0.45),
                (2.52, 0.25, 0.3), (3.0, 0.15, 0.2)]
    rng = random.Random(7)
    out = []
    for i in range(n):
        t_s = i / RATE
        v = sum(level * math.exp(-t_s / decay) * math.sin(2.0 * math.pi * f0 * ratio * t_s)
                for ratio, level, decay in partials)
        if t_s < 0.004:
            v += rng.uniform(-1.0, 1.0) * (1.0 - t_s / 0.004)  # The strike
        attack = min(1.0, i / (0.002 * RATE))
        release = min(1.0, (n - i) / (0.05 * RATE))
        out.append(v * attack * release)
    peak = max(abs(v) for v in out) or 1.0
    return [v / peak for v in out]


def background(seconds, rng, level=0.01):
    """Soft noise, darker than white, with a slow swell like a breeze"""
    n = int(seconds * RATE)
    out = []
    low = 0.0
    for i in range(n):
        low += (rng.uniform(-1.0, 1.0) - low) * 0.08
        swell = 0.7 + 0.3 * math.sin(2.0 * math.pi * i / (RATE * 7.0))
        out.append(level * swell * (low * 2.5 + 0.15 * rng.uniform(-1.0, 1.0)))
    return out


def write(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(
            b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32767)) for v in samples)
        )
    print(f"Wrote {path.relative_to(HERE.parent)} ({len(samples) / RATE:.1f} s)")


def recording(seconds, calls, rng, level=0.5):
    """Background noise with calls at their times: (time in seconds, sound, gain)"""
    buffer = background(seconds, rng)
    for at, sound, gain in calls:
        mix(buffer, sound, at, gain * level)
    return buffer


def main():
    rng = random.Random(20261122)

    # The labels in animal-labels.json and the rows in review-list.csv are at these times
    farm = recording(
        20.0,
        [
            (1.00, woof(rng), 1.0),
            (1.45, woof(rng), 0.9),  # A second bark, close after the first
            (4.00, meow(rng), 0.8),
            (7.00, song(), 0.6),
            (10.00, woof(rng), 0.06),  # Far off and quiet: raise the playback gain to hear it
            (12.50, meow(rng), 0.7),
            (15.20, tap(), 0.5),  # The fox, as a tap on glass
            (17.00, woof(rng), 1.0),
        ],
        rng,
    )
    write(HERE / "audio" / "farm.wav", farm)

    meadow = recording(
        15.0,
        [
            (1.00, song(), 0.7),
            (3.20, tweet(), 0.6),
            (3.55, tweet(False), 0.5),
            (6.00, yip(rng), 0.9),  # The fox, as a yip
            (7.20, tap(), 0.35),
            (9.00, meow(rng), 0.5),
            (12.00, song(), 0.5),
        ],
        rng,
    )
    write(HERE / "audio" / "meadow.wav", meadow)

    # Two clips cut from a recording that isn't in the project, as a pipeline would cut them:
    # two seconds each, around an event. Their offsets (where each starts in the recording) are
    # in review-list.csv.
    field = recording(130.0, [(0.20, meow(rng), 0.8), (120.40, woof(rng), 0.9)], rng)
    write(HERE / "clips" / "field-120.wav", field[int(120.0 * RATE) : int(122.0 * RATE)])
    # The second event is near the recording's start, so its clip starts half a second before
    # it, padded with silence: an offset of -0.5
    write(HERE / "clips" / "field-0.wav", silence(0.5) + field[: int(1.5 * RATE)])

    # Each call once, a second apart, to compare them: bark, meow, song, yip, tap, bell
    calls = silence(0.5)
    for sound in (woof(rng), meow(rng), song(), yip(rng), tap(), ding()):
        calls += [v * 0.5 for v in sound] + silence(1.0)
    write(HERE / "calls.wav", calls)


if __name__ == "__main__":
    main()
