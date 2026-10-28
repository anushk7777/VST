# Plan: **LINEN**, a silky vocal VST3 plugin (anushk7777/VST)

## Context
The owner wants one vocal plugin that makes harsh vocals smooth, gently compressed and expensive-sounding ("Bieber-style"). It combines a Soothe-style de-harsher, a 1073-style preamp/EQ, an invisible warm compressor, an Air stage and two "AI" features. Target: **Windows 11, FL Studio, VST3**. The repo `anushk7777/VST` is empty, so this is a fresh project.

We do **not** decompile Soothe. We rebuild its *behaviour* from public info with our own algorithm. No trademarked names in the product ("1073-style" in docs only). Name: **Linen**, with a white-linen "quiet luxury" look.

### What Soothe 3 does (public info, May 2026 release)
- It finds frequencies that poke out above their neighbours moment to moment, and cuts only those, only while they poke out.
- **Depth** sets the amount. **Detail** merges Soothe 2's Sharpness and Selectivity. **Attack/Release** each have low/high **Tilt**. **Max Cut** sets a dB cap. **Soft/Hard** modes: Soft uses an adaptive threshold.
- It also has band shaping, mid/side, sidechain, delta listen, low-latency and linear-phase modes.

## Signal chain
`Input → Preamp (1073-style) → 1073-style EQ → De-harsh → Silk Comp → Air → Mix/Output`, with a latency-matched dry path into Mix.
Preamp goes first so De-harsh cleans up any glare its harmonics add.

## DSP modules (`src/dsp/`), each with `prepare/process/reset/getLatencySamples`
1. **ResonanceSuppressor**
   - STFT: 2048 FFT at 48k (scaled with sample rate), hop 512. **sqrt-Hann on analysis AND synthesis**, so the product is Hann and stays COLA at 75% overlap; normalised overlap-add. This prevents ~94 Hz frame buzz.
   - Detector per frame:
     - magnitude dB, then a log-frequency smoothed envelope (width set by Detail);
     - the excess above the envelope, weighted by a **perceptual curve** (ear-canal bell, about +4 dB near 3.5 kHz; "Vocal focus", on by default);
     - Soft mode uses an adaptive threshold from a running mean; Hard mode uses a fixed threshold.
   - Gain reduction: `GR = min(Depth·excess, MaxCut)` × the band-shape weight. Each bin gets attack/release with low/high tilt, then smoothing across frequency.
   - **Body protect (f0 lock):** a YIN pitch tracker on the detector signal. When confident, a smooth mask (about ±1/3 oct) protects f0 in 80–500 Hz from cuts, so the voice never hollows out. I chose this over the pasted "lowest peak above −30 dBFS, 1 octave" rule: that rule depends on level and can also protect the 2nd harmonic.
   - Also: mid/side, external sidechain and Delta.
   - Latency is reported to the host. FL Studio's plugin delay compensation (PDC) handles it.
2. **Preamp1073**
   - 4× oversampling with FIR filters, so latency is a whole number of samples.
   - Transformer iron: leaky integrator → soft saturator → matching differentiator. Flux follows the integral of voltage, so low notes saturate first and the mids stay clean.
   - Class-A stage: an asymmetric waveshaper whose bias moves with an envelope follower (2 ms attack / 90 ms release), followed by a DC blocker. Loud belts bloom with more 2nd harmonic; quiet tails relax.
3. **Eq1073**
   - HPF at 50/80/160/300 Hz (18 dB/oct).
   - Low shelf at 35/60/110/220 Hz.
   - Mid bell at 360/700/1.6k/3.2k/4.8k/7.2k, with Q that changes with gain.
   - High shelf at 12 kHz.
   - Stepped switches.
4. **SilkCompressor** (updated after studying the 2254/33609 diode bridge and the LA-2A's T4 cell): two-stage optical release with memory (fast half at Release, slow half 0.4–5 s depending on how long it has held), presence-weighted detection, and the **Cotton** texture: diode-bridge-style 2nd/3rd harmonics at 2x plus GR-coupled low-mid warmth and soft top, active only while compressing. Original spec:: feed-forward RMS, about 10 dB soft knee, ratio 1.5–4. Two-stage opto-style release that follows the material. Sidechain HPF around 100 Hz, auto makeup and Mix.
5. **Air**: above 10 kHz, an even-harmonic exciter plus an upward micro-compressor that lifts breath. A **differential sibilance detector** compares HF (6–14 kHz) with vowel energy (200 Hz–2.5 kHz). When an S or T spikes the ratio, the shelf smoothly turns into a gentle cut; on vowels and breaths it opens.
6. **Mix / latency**
   - The dry path runs through a delay line equal to `ResonanceSuppressor + Preamp1073` latency. It is updated in `prepareToPlay`, and `setLatencySamples` is set to match.
   - Every continuous parameter uses `juce::SmoothedValue` (20–30 ms) so nothing zippers.
7. **Silk macro**: a non-linear S-curve per target.
   - Drive comes in over 0–50%.
   - Depth moves across 0–100%.
   - Comp comes in over 25–85%.
   - Air comes in over 40–100%.
   - A calibrated inverse output-gain curve keeps loudness roughly constant, with an optional LUFS-match toggle.
8. **AutoAssist ("AI" Listen button)**: analyses 5–10 s of the vocal: long-term spectrum compared with the smooth-vocal target, 2–6 kHz harshness, sibilance and crest factor. It then sets Depth, EQ, comp threshold and Air. Plain DSP, runs locally.
9. **Neural preamp ("AI" option 2)**: a small GRU model run through RTNeural, as a "Neural" mode of Preamp1073. The training script goes in `training/` (NAM-style input/output WAV pairs). **This is blocked on data:** the owner must supply recordings of a test signal (which I'll provide) played through a real 1073 at a few drive settings. The DSP preamp stays the default until then.

## GUI (JUCE 8)

**Current look (owner's choice, Oct 2026): glossy white and electric cobalt.** Cool-white backdrop with a soft gloss sheen. Knobs are white porcelain; the main knob of each section (Silk, Drive, Cotton, Depth) is cobalt enamel. Switches use glossy cobalt; the veil is cobalt. The palette lives in `src/ui/Palette.h`. The original champagne/linen spec below is kept for reference.
- **Palette:**
  - Alabaster `#F6F3EC` base;
  - Cashmere `#EBE4D8` for recessed wells;
  - Espresso Ink `#221E1C` for text;
  - Champagne `#C8A96E→#E6D3A7` metallic gradient for knobs;
  - Terracotta `#C47D68` for the gain-reduction veil.
- **Surface:**
  - a procedural linen weave (fine crosshatch at about 1.8% opacity with a soft top-lit vignette), drawn once per resize into a cached tile, with no PNGs;
  - layered inner and drop shadows via `sudara/melatonin_blur`.
- **Type:** Cormorant Garamond for the wordmark and section headers, Inter (tracked +0.08em) for readouts. Both are OFL fonts, embedded through BinaryData.
- **Spectrum:**
  - 1/6-octave control points joined by Catmull-Rom splines, never raw FFT bins;
  - the gain-reduction "silk veil" hangs down from the top with a champagne→terracotta fill and 15 ms / 350 ms ballistics;
  - driven by `juce::VBlankAttachment`, with no `setBufferedToImage` on animated parts (Direct2D on Windows 11).
- **Micro-interactions:**
  - the Silk knob glow breathes with gain reduction plus drive;
  - Delta, or Alt-dragging the band handles, dims the other panels to 35% (180 ms ease-out, `juce_animation`).

## Repo layout
- `CMakeLists.txt`: JUCE 8 (pinned stable tag) and melatonin_blur via FetchContent; `juce_add_plugin` builds VST3 plus Standalone.
- `src/PluginProcessor.*`, `src/PluginEditor.*`, `src/Parameters.h` (the whole parameter layout in one place), `src/dsp/*`, `src/ui/*` (LookAndFeel, weave, spectrum, knobs).
- `tools/render.cpp`: offline CLI that runs the chain on a WAV or test signal.
- `tests/`: DSP unit tests (JUCE `UnitTest` console app).
- `presets/`: Silky Vocal, Gentle Polish, Breathy Pop, Transparent.
- `.github/workflows/build.yml`: a `windows-latest` job that builds the VST3, runs tests and pluginval, and uploads the `.vst3` as a download artifact.
- `README.md`: FL Studio install steps. Copy the `.vst3` to `C:\Program Files\Common Files\VST3`, then Options → Manage plugins → Find plugins.

## Phases
Each phase is pushed with a Windows build artifact for the owner to try in FL Studio.
1. Scaffold, CI, parameter smoothing, dry-delay Mix framework and the Linen look-and-feel shell.
2. De-harsh module, plus the spectrum and silk-veil display.
3. Preamp and 1073-style EQ.
4. Silk compressor and Air.
5. Silk macro with loudness compensation, presets and GUI polish.
6. AutoAssist Listen button.
7. Neural preamp, once the owner provides hardware captures.
8. Later: low-latency mode and linear phase.

## Verification
- Unit tests via `tools/render`:
  - **Latency null:** with Depth=0, Drive=0 and all modules neutral, the output equals the input delayed by the reported latency, and Mix=50% is comb-free (impulse response check).
  - **Resonance cut:** a resonant peak on pink noise is cut by the expected amount while neighbours move less than 1 dB. MaxCut caps the cut.
  - **Body protect:** a synthetic vocal (harmonic series, f0 200 Hz) loses less than 1 dB at f0 at maximum Depth.
  - **No frame buzz:** constant gain reduction leaves no energy at the hop rate.
  - **EQ:** the response is within ±0.5 dB at the 1073 frequency points.
  - **Preamp:** 2nd harmonic is louder than 3rd at moderate drive, and aliasing is below −90 dB.
  - **Compressor:** the static curve matches ratio and knee.
  - **Silk macro:** loudness drift across 0–100% stays within ±1 LU.
- pluginval (strictness 5) runs in Windows CI.
- Owner check each phase on a real vocal in FL Studio on Windows 11. I can't listen in the container, so the owner's ears drive the final tuning.

## Notes
- Pushing needs `add_repo` with push access to `anushk7777/VST`. The repo is empty, so the first push creates `main`.
- The container builds the headless DSP tests on Linux; Windows binaries come from GitHub Actions.
