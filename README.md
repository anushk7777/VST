# Linen

*silk for the voice*

Linen is a vocal plugin (VST3) built to make vocals smooth and expensive-sounding. It combines:

- a warm 1073-style preamp and EQ;
- a de-harsher that removes harsh resonances as they happen;
- an invisible, gentle compressor;
- an Air stage that adds breath without bringing back the esses.

The full plan and the status of each phase are in [docs/PLAN.md](docs/PLAN.md).

> Linen is an original design. It is not affiliated with oeksound (Soothe) or AMS Neve; "1073-style" describes the sound it aims for.

## Status

| Phase | What | State |
|---|---|---|
| 1 | Scaffold, CI, Input/Mix/Output, latency-matched dry path, the Linen look | done |
| 2 | De-harsh (resonance suppressor) and its spectrum display | next |
| 3 | 1073-style preamp and EQ | planned |
| 4 | Silk compressor and Air | planned |
| 5 | Silk macro, presets, GUI polish | planned |
| 6 | Auto-assist "Listen" button | planned |
| 7 | Neural preamp (needs real 1073 recordings) | planned |

## Install in FL Studio (Windows 11)

1. On GitHub, open **Actions**, then the latest green **Build** run, and download **Linen-VST3-Windows** under *Artifacts*.
2. Unzip it. You get a folder called `Linen.vst3`.
3. Copy the whole `Linen.vst3` folder to `C:\Program Files\Common Files\VST3`.
4. In FL Studio, open **Options → Manage plugins**, then click **Find installed plugins**.
5. In a Mixer insert, open an empty slot and pick **Linen**.

**Linen-Standalone-Windows** is the same thing as a standalone app, for a quick look without FL Studio.

## Build it yourself

You need CMake 3.22+ and a C++20 compiler (Visual Studio 2022 on Windows). JUCE 8 and melatonin_blur are downloaded automatically.

```sh
cmake -B build                       # add -G Ninja -DCMAKE_BUILD_TYPE=Release on Linux/macOS
cmake --build build --config Release
build/LinenTests_artefacts/Release/LinenTests      # unit tests
```

**Offline render** runs the exact plugin engine on a WAV file, so DSP changes can be measured without a DAW:

```sh
build/LinenRender_artefacts/Release/linen-render in.wav out.wav --input 0 --output 0 --mix 1
```

## Layout

| Path | Contents |
|---|---|
| `src/dsp/` | The signal chain (`LinenEngine`). No plugin or GUI code, so tests and the render tool run the same engine. |
| `src/Parameters.h` | Every parameter, in one place. Parameter IDs are permanent. |
| `src/PluginProcessor.*`, `src/PluginEditor.*` | The JUCE plugin wrapper and window. |
| `src/ui/` | Look and feel: palette, linen weave background, champagne knobs. |
| `tests/` | DSP unit tests. |
| `tools/render.cpp` | Offline render CLI. |
| `assets/fonts/` | Cormorant Garamond and Inter, both under the SIL Open Font License (texts included). |
