# AGC 2.0

A stereo leveler for Logic Pro, with a predictive sidechain ducker on the same plug-in. The leveler adds 1.5 ms and the sidechain adds 1.0 ms, so Logic's delay compensation sees about 2.5 ms.

The learned part is a logistic model. It sees the six level features plus the live knobs, and it is trained on every lookahead from 1.5 ms to 40 ms. It only decides when the leveler should engage. Duck at 0% and Max boost at 0 dB keep the gain at 0 dB. A cut is allowed only when the lookahead is long enough for the 15 ms attack to finish before that audio is heard, so a short hit is not pumped. The gain amount is a deterministic rider: target minus the measured peak, through a 6 dB knee, clamped by max boost and max cut. A 1.5 ms delay lets the 0.5 ms engage attack land before the audio is heard. While the latch is off, gain holds, so a steady phrase does not pump.

A pause under the noise floor freezes the envelope and the gain. The release filter does not lift room tone between words. At the default speed the release is 400 ms. Boost cannot exceed +6 dB.

Downward moves are a duck, not a clamp. The default Duck of 40% removes only part of the overshoot, through a wide knee, and the cut itself takes about 15 ms. A transient gets through. Turning Duck up makes the rider firmer. At 100% it takes the whole step down to the cut limit.

Speed does not slow the attack. That would miss the 1.5 ms window. Speed 50% is a 400 ms release, and faster still stops at 200 ms.

## Controls

- **Target** — level to ride toward, default −18 dB.
- **Max boost / Max cut** — gain limits. Boost is capped at +6 dB. Cut defaults to −24 dB.
- **Noise floor** — pauses under this level hold the current gain. Default −42 dB.
- **Duck** — how much of a loud peak is eased down. Default 40%.
- **Speed** — release and hang. 50% is a 400 ms release.
- **Lookahead** — delay on the main path, 1.5–40 ms, default 1.5 ms. The sidechain ducker adds its own 1 ms. At 40 ms a loud call can be eased down before it is heard.
- **Link** — one detector and one gain for every channel.
- **Bypass** — fades the leveler to 0 dB and keeps its delay.

The sidechain row ducks the main signal from a second track. In Logic, choose that track in the plug-in's sidechain menu. The sidechain is read with no delay. The main signal waits 1 ms, so the duck starts before the hit is heard. A Linkwitz-Riley split at 300 Hz leaves the bass alone and ducks the mids and highs. Depth is how far those highs fall. Threshold is the sidechain level that opens the duck. Release is how fast it lets go, default 300 ms. Sense is the noise floor: quieter sidechain audio cannot start a duck, and once the duck has returned to unity the gain holds.

- **Depth** — duck amount, 0–100%, default 50%.
- **Threshold** — sidechain level that starts the duck, default −24 dB.
- **Release** — 50–1000 ms, default 300 ms.
- **Crossover** — bass stays below this frequency, default 300 Hz.
- **Sense** — sidechain noise floor, default −45 dB.

The gain meter is the current rider in decibels. The engage lamp follows the latch, with brightness from the model probability.

## Presets

The plug-in menu has two factory presets. **Default** is the voice leveler. **Money for Nothing** is for a full-scale master such as the 1996 Brothers in Arms remaster, which already peaks at 0 dBFS. Its peak follower sits near −13 dB, so a −18 dB target ducks the riff and pumps on the snare. The preset puts the target at −6 dB, allows no boost, ducks 15% of only those hottest hits, and uses the slowest release. Sidechain depth is 0, so the highs are left alone. On that master the rider moves by less than 0.2 dB and the song stays at its own level.

## Build

Run these from the project directory, the folder that contains `CMakeLists.txt`. They do not work from your home folder.

The DSP and `agc_tests` need a C++17 compiler. The plugin needs [JUCE](https://github.com/juce-framework/JUCE) 8.0.8, which CMake fetches, and a Linux or macOS toolchain. On a Mac, install CMake and the Xcode command line tools first. Xcode 15.0 is accepted: the build uses Apple's classic linker, which avoids the broken linker in that release. Xcode 15.1 or newer uses the normal linker.

```bash
cd /path/to/this/project
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux this builds the Standalone app and the test binary. An Audio Unit is what Logic Pro loads, and that format is built only on macOS:

```bash
cd /path/to/this/project
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target AGC2_AU
```

The Standalone target is built alongside it.

## Logic Pro

From that same project directory, copy the built component into the user plug-in folder and rescan:

```bash
cp -R "build/AGC2_artefacts/Release/AU/AGC 2.0.component" \
  ~/Library/Audio/Plug-Ins/Components/
```

Logic only lists the plug-in after the bundle is signed. The Mac build signs it. If a copy is already installed, sign that one too:

```bash
codesign --force --deep --sign - \
  "$HOME/Library/Audio/Plug-Ins/Components/AGC 2.0.component"
xattr -cr "$HOME/Library/Audio/Plug-Ins/Components/AGC 2.0.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
rm -rf "$HOME/Library/Caches/AudioUnitCache"
auval -v aufx Ag20 AgcL
```

Then open Logic's Settings → Plug-in Manager and reset or rescan. It is an audio effect under the manufacturer AGC, not a MIDI effect. The plug-in reports 1.5 ms of latency so Logic's delay compensation can line the track up. Validate it with:

```bash
auval -v aufx Ag20 AgcL
```

Signing and notarization are left to the Mac you build on.
