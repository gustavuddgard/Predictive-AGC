# AGC 2.0

A stereo leveler for Logic Pro. Reported latency is **1.5 ms**, under a hard cap of 2 ms.

The learned part is a four-unit network with 33 stored weights. It only decides when the leveler should engage. The gain amount is a deterministic rider: target minus the measured peak, through a 6 dB knee, clamped by max boost and max cut. A 1.5 ms delay lets the 0.5 ms engage attack land before the audio is heard. While the latch is off, gain holds, so a steady phrase does not pump. Below about −60 dB the latch is forced off and gain eases back to unity.

Speed does not slow the attack. That would miss the 1.5 ms window. Speed scales release from about 400 ms down to 30 ms, and a short hang.

## Controls

- **Target** — level to ride toward, default −18 dB.
- **Max boost / Max cut** — gain limits, default +12 dB and −24 dB.
- **Speed** — release and hang. Faster means a shorter release.
- **Link** — one detector and one gain for every channel.
- **Bypass** — fades gain to 0 dB and keeps the 1.5 ms delay, so latency does not jump.

The gain meter is the current rider in decibels. The engage lamp follows the latch, with brightness from the model probability.

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

In Logic, open Settings → Plug-in Manager and reset or rescan. The plug-in reports 1.5 ms of latency so Logic's delay compensation can line the track up. Validate it with:

```bash
auval -v aufx Ag20 AgcL
```

Signing and notarization are left to the Mac you build on.
