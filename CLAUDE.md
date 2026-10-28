# Linen: working notes

- The plan and its phases are in `docs/PLAN.md`. Keep the README status table up to date when a phase lands.
- Target: Windows 11 + FL Studio, VST3. Windows binaries come from GitHub Actions (`.github/workflows/build.yml`), not local builds.
- Never decompile or copy oeksound/Neve code. Build behaviour from public descriptions. Keep "Soothe" and "Neve" out of product, parameter and preset names ("1073-style" in docs only).
- DSP lives in `src/dsp/` with no plugin/GUI dependency. The plugin, `tests/` and `tools/render.cpp` all run `linen::Engine`.
- Audio thread: no allocation, no locks, no parameter lookups by name. Read cached handles (`params::Handles`).
- Any module that adds latency must report it through `Engine::getLatencySamples()`, so the dry path and host delay compensation stay aligned. The "50% Mix impulse" test guards this.
- Parameter IDs in `src/Parameters.h` are permanent once released; add new ones, never rename.
- Every DSP feature gets a measurable unit test (see `docs/PLAN.md` → Verification).
- GUI: palette in `src/ui/Palette.h`; no bitmap assets; nothing heavy per frame (cache textures, draw animations with `juce::VBlankAttachment`).

## Local build (Linux container)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE -DFETCHCONTENT_SOURCE_DIR_MELATONIN_BLUR=/path/to/melatonin_blur
cmake --build build
./build/LinenTests_artefacts/Release/LinenTests
```

To see the window without a screen: run `build/Linen_artefacts/Release/Standalone/Linen` under `Xvfb` and capture it with `import -window root shot.png`.
