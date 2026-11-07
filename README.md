# AGC 2.0

A stereo leveler for Logic Pro, with a predictive sidechain ducker on the same plug-in. The main path is a slow leveler plus a 1 ms peak guard. The sidechain adds its own 1 ms, so Logic's delay compensation sees about 2 ms.

The leveler splits the signal at the crossover (default 300 Hz) and again at 3.4 kHz, and rides each band on its own. Bass is not pulled down to lift the mids. Duck at 0% leaves a loud master where it is. A quiet passage can rise, up to the Max gain knob. Music will not boost past +6 dB even if that knob is higher. The peak guard looks one millisecond ahead and only turns down when the leveler would push a peak through −0.1 dBFS. It lets go on its own, so a cut from a burst does not stay on the next song.

A classifier on the waveform picks the ballistics. Music holds an 800 ms release. Voice follows faster and freezes on a pause. Telephone rides only the middle band. A stresser — flat, jumpy, near full scale — freezes the slow leveler and lets the peak guard catch the bursts. Silence holds the leveler for about a breath, then eases it back to 0 dB, so a hole between two sources does not hand the next one a leftover cut.

The older logistic engage model is still in the tree. It is not on this signal path.

## Controls

- **Target** — level to ride toward, default −18 dB.
- **Max gain** — upward limit, 0 to +12 dB, default +6. Music stops at +6.
- **Release** — base release, 100–1500 ms, default 500. Music uses at least 800 ms.
- **VAD** — pauses under this level hold the leveler. Default −42 dB.
- **Duck** — how much of a loud band is eased down. Default 0%, so a hot master is not pulled down.
- **Lookahead** — peak-guard delay, 0.5–3 ms, default 1 ms. The sidechain ducker adds its own 1 ms.
- **Link** — sidechain channels share one detector.
- **Bypass** — fades the leveler to 0 dB and keeps the delay.

The classifier row shows Silence, Music, Voice, Telephone, or Stresser. The peak lamp lights while the guard is actually reducing.

The sidechain row ducks the main signal from a second track. In Logic, choose that track in the plug-in's sidechain menu. The sidechain is read with no delay. The main signal waits 1 ms, so the duck starts before the hit is heard. A Linkwitz-Riley split at 300 Hz leaves the bass alone and ducks the mids and highs. Depth is how far those highs fall. Threshold is the sidechain level that opens the duck. Release is how fast it lets go, default 300 ms. Sense is the noise floor: quieter sidechain audio cannot start a duck, and once the duck has returned to unity the gain holds.

- **Depth** — duck amount, 0–100%, default 50%.
- **Threshold** — sidechain level that starts the duck, default −24 dB.
- **Release** — 50–1000 ms, default 300 ms.
- **Crossover** — bass stays below this frequency, default 300 Hz.
- **Sense** — sidechain noise floor, default −45 dB.

The gain meter is the loudest band plus the peak guard, in decibels.

## Presets

The plug-in menu has two factory presets. **Default** rides toward −18 dB with up to +6 dB of boost and no downward duck. **Money for Nothing** is for a full-scale master that already sits on 0 dBFS. Boost and duck are both 0, so the song is left at its own level. The peak guard still has 1 ms in case a later passage would clip. Sidechain depth is 0.

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

Then open Logic's Settings → Plug-in Manager and reset or rescan. It is an audio effect under the manufacturer AGC, not a MIDI effect. The plug-in reports about 2 ms of latency (1 ms peak guard plus 1 ms sidechain) so Logic's delay compensation can line the track up. Validate it with:

```bash
auval -v aufx Ag20 AgcL
```

Signing and notarization are left to the Mac you build on.
