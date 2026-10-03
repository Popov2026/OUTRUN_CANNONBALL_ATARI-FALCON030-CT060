#!/usr/bin/env python3
"""Checks a WAV recorded from Hatari during DMTEST: prints the strongest frequencies of each
channel over the middle of the recording and whether the fx tones are present: 1000 Hz on the
left, 1500 Hz on the right (the module's note, near 440-466 Hz, is reported but not required:
its voice is panned left). Exit code 0 if both fx tones are found."""
import sys, wave
import numpy as np

w = wave.open(sys.argv[1])
rate, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
raw = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
data = raw.reshape(-1, ch) if ch > 1 else np.stack([raw, raw], axis=1)
print("rate", rate, "channels", ch, "seconds", round(n / rate, 2), "peak", int(np.abs(data).max()))
mid = data[len(data) // 4: len(data) * 3 // 4]
if len(mid) < rate:
    print("recording too short"); sys.exit(2)
ok = True
for name, idx, want in (("left", 0, (1000,)), ("right", 1, (1500,))):
    x = mid[:, idx] * np.hanning(len(mid))
    spec = np.abs(np.fft.rfft(x))
    freqs = np.fft.rfftfreq(len(x), 1 / rate)
    top = sorted(zip(spec, freqs), reverse=True)
    peaks = []
    for s, f in top:
        if all(abs(f - p) > 30 for p in peaks):
            peaks.append(f)
        if len(peaks) == 5: break
    print(name, "strongest:", [int(round(p)) for p in peaks])
    floor = np.median(spec)
    for f in want:
        band = spec[(freqs > f - 15) & (freqs < f + 15)]
        level = band.max() / floor if len(band) else 0
        found = level > 20
        ok &= found
        print("  %4d Hz: %s (%.0f x median)" % (f, "present" if found else "MISSING", level))
sys.exit(0 if ok else 1)
