# Linen: working notes

- The plan and its phases are in `docs/PLAN.md`. Keep the README status table up to date when a phase lands.
- Target: Windows 11 + FL Studio, VST3. Windows binaries come from GitHub Actions (`.github/workflows/build.yml`), not local builds.
- Never decompile or copy oeksound/Neve code. Build behaviour from public descriptions. Keep "Soothe" and "Neve" out of product, parameter and preset names ("1073-style" in docs only).
- DSP lives in `src/dsp/` with no plugin/GUI dependency. The plugin, `tests/` and `tools/render.cpp` all run `linen::Engine`.
- Audio thread: no allocation, no locks, no parameter lookups by name. Read cached handles (`params::Handles`).
- Any module that adds latency must report it through `Engine::getLatencySamples()`, so the dry path and host delay compensation stay aligned. The "50% Mix impulse" test guards this.
- Parameter IDs in `src/Parameters.h` are permanent once released; add new ones, never rename.
- Every DSP feature gets a measurable unit test (see `docs/PLAN.md` → Verification).
- Neutral is bit-exact. The Clean preset (every parameter at its default) and any switched-off section must give back the input to the last bit, delayed by the latency. A new stage must be an exact pass-through at its neutral setting. `tests/CleanTests.cpp` and `LinenCheck` (on the built VST3, in CI) enforce this.
- Harmonics are budgeted, measured on vocal-level sines:
  - house drive (30%) at or below -55 dB THD on a -6 dBFS note;
  - every non-Pain preset at or below -45 dB (LinenCheck).

  Re-measure after touching any nonlinear stage.
- GUI: palette in `src/ui/Palette.h`; no bitmap assets; nothing heavy per frame (cache textures, draw animations with `juce::VBlankAttachment`).

## Local build (Linux container)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE -DFETCHCONTENT_SOURCE_DIR_MELATONIN_BLUR=/path/to/melatonin_blur
cmake --build build
./build/LinenTests_artefacts/Release/LinenTests
```

To check the built plugin the way a DAW loads it (null test plus distortion by preset):
`build/LinenCheck_artefacts/Release/LinenCheck build/Linen_artefacts/Release/VST3/Linen.vst3 --plot null.png`.

To see the window without a screen, configure with `-DLINEN_BUILD_SNAPSHOT=ON` and run under Xvfb (`--preset N` loads a factory preset):
`DISPLAY=:99 build/LinenSnapshot_artefacts/Release/LinenSnapshot shot.png [vocal.wav] --scale 1 --param deharsh_depth=8`.
It plays a synthetic (or given) vocal through the real processor at real-time pace, so the display animates as in a host.
