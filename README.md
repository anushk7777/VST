# Linen

*silk for the voice* · by **Anushk Dua**

Linen is a vocal plugin (VST3) built to make vocals smooth and expensive-sounding. It combines:

- Tone shelves that shape the voice before the preamp;
- a warm 1073-style preamp and EQ;
- a de-harsher that removes harsh resonances as they happen;
- an invisible, gentle compressor;
- an Air stage that adds breath without bringing back the esses.

**Clean by default.** A fresh Linen does nothing to your audio: the output is your input, bit for bit (just delayed, which FL Studio compensates). Every knob then works on its own from there. This is checked on the built plugin in every build (see [Clean, and the phase test](#clean-and-the-phase-test)).

The full plan and the status of each phase are in [docs/PLAN.md](docs/PLAN.md).

> Linen is an original design. It is not affiliated with oeksound (Soothe) or AMS Neve; "1073-style" describes the sound it aims for.

## Status

| Phase | What | State |
|---|---|---|
| 1 | Scaffold, CI, Input/Mix/Output, latency-matched dry path, the Linen look | done |
| 2 | De-harsh (resonance suppressor) and its spectrum display | done |
| 3 | 1073-style preamp and EQ | done |
| 4 | Compressor and Air (now Dua Wox, 0.3) | done |
| 5 | Silk macro and presets | done |
| 5b | Pain (vocal distress engine) | done |
| 6 | AI Listen button | done |
| 6b | Premium UI: linen + cobalt lights, jewel lamps, halos, backlit display | done |
| 5c | Clean by default (bit-exact null, checked on the built VST3), clean drive calibration, Tone shelves before the preamp, section power switches, Null button | done |
| 0.3 | Dua Wox compressor (TUBE + OPTO), boutique console UI (knurled knobs, LED rings, bat toggles, needle VU), warmth report | done |
| 0.4 | Signature circuits (Weave, Silk loom, Cotton iron, Breath gate), Archivo type | done |
| 7 | Neural preamp (needs real 1073 recordings) | planned |

## Install in FL Studio (Windows 11)

1. On GitHub, open **Actions**, then the latest green **Build** run, and download **Linen-VST3-Windows** under *Artifacts*.
2. Unzip it. You get a folder called `Linen.vst3`.
3. Copy the whole `Linen.vst3` folder to `C:\Program Files\Common Files\VST3`.
4. In FL Studio, open **Options → Manage plugins**, then click **Find installed plugins**.
5. In a Mixer insert, open an empty slot and pick **Linen**.

**Linen-Standalone-Windows** is the same thing as a standalone app, for a quick look without FL Studio.

The version number is at the bottom right of the window (this one is **v0.4.0**).

## Clean, and the phase test

Linen opens on the **Clean** preset: every section is in, but every setting is neutral (Drive 0, Depth 0, Ratio 1:1, Peak red. 0, Warmth 0, Air 0, De-ess 0, all EQ and shelf gains 0 dB, Silk 50%, Pain 0). The output is then exactly the input, delayed by Linen's latency.

**Prove it yourself in FL Studio:**
- **The NULL button** (Output section) plays Linen's output minus your untouched vocal, sample-aligned. On Clean your meters read silence (-inf). On any other setting you hear exactly what Linen adds or takes away, distortion included.
- **The classic two-track test:**
  1. Put the vocal on two mixer tracks: one with Linen on Clean, one without.
  2. Flip the polarity of one track (the "reverse polarity" button in the mixer track's properties).
  3. Route both to the master. FL Studio's delay compensation lines them up, and the master meter stays at -inf.

**How every build is checked:** `LinenCheck` loads the built `Linen.vst3` the way a DAW does. It plays noise, a sung vowel, a full-scale sweep and silence through it:
- at 44.1, 48 and 96 kHz;
- in stereo and mono;
- in irregular block sizes.

It then subtracts the input, lined up by the latency the plugin reports. Clean must come back **bit for bit**, and so must every section switched off with every knob cranked. GitHub's Windows build runs this on the very `Linen.vst3` you download, and also saves the picture (artifact **Linen-null-test**).

It also measures every preset's distortion. Presets without Pain must stay under -45 dB THD on a -6 dBFS note.

## Use one knob on its own

- Start from **Clean** and turn just the knob you want (Drive, Depth, a Tone shelf, Pain…): only that section changes the sound. The tests check this bit for bit for every section.
- Each section has a **power button** before its title. Off is an exact bypass: your settings stay, but nothing they do is heard. Use it to hear one section of a preset on its own, or to switch one out.
- **Pain** on its own also brings its own small preamp push, the way it was designed. Switch the Preamp off to hear Pain alone.

## AI Listen

Press **AI LISTEN** (under the preset box), then play your vocal. The lamp ring pulses cobalt and fills as it listens. After about 6 seconds of actual singing, it compares the vocal with the balance of a smooth, finished pop vocal and sets Linen up for it:
- input gain;
- the Tone low shelf (just under the sung notes, or up in the mud for a muddy voice);
- preamp drive, and the low, mid and 12k EQ;
- De-harsh depth;
- Dua Wox threshold, ratio, Peak red. and Warmth;
- Air and De-ess.

The display then tells you in plain words what it heard and what it changed. Silk and Pain stay where you put them, and the sections it sets are switched on. Everything runs locally; click the button again to stop.

## Pain

**Pain** (top right, beside Silk) is Silk's opposite: raw ache that only bites when the singer pushes.
- A "nervous system" watches the 1.8–4.2 kHz strain band, with a fast reflex for belts and a slow fatigue for long loud lines.
- Under stress the virtual plate starves: the rail sags and the bias shifts, so pushed notes crack with harmonics while quiet lines stay fragile.
- Quiet fry and throat tension between words get pulled up.
- A faint tremor trembles in only when the voice strains.
- Pain also pushes the preamp a little and makes De-harsh stricter above 5.5 kHz, while leaving the 1.5–3.2 kHz ache band alone. Past 50% it darkens Air like old tape.
- The level stays steady. At 0 it is completely bypassed.

The knob burns crimson and the silk veil trembles as stress rises. The presets **Velvet Ache** and **Torn Linen** show it off.

## Lights

- A **jewel lamp** beside each section title glows cobalt as that module works:
  - Preamp: drive;
  - De-harsh: cuts;
  - Dua Wox: gain reduction (and a lamp per TUBE and OPTO cell);
  - Air: air, flaring on every S;
  - Output: signal.
- Each knob's LED ring shows its setting. Pills carry a small cobalt LED when on; each section's chrome bat-handle toggle points up while it is in.
- Silk's halo breathes with the vocal.
- The display is backlit, and turns cobalt while AI Listen is listening.

## Silk and presets

**Silk** (top right) is one knob for the whole chain. At 50% your settings are exactly as you set them.
- **Turning it up** adds the following together, each on its own curve in the order an engineer would reach for them, while an automatic trim keeps the level within 0.5 dB:
  - preamp drive first;
  - De-harsh depth across the whole travel;
  - compression (threshold, ratio, Peak red.), Warmth and Weave in the middle;
  - Air last.
- **Turning it down** pulls everything back toward transparent. From Clean there is nothing to pull back, so below 50% it changes nothing.
- Sections that are switched off are left alone.

**Presets** (the box at the top, also listed in FL Studio's preset menu):

| Preset | Character | Distortion on a -6 dBFS note |
|---|---|---|
| Clean | Untouched: bit for bit. The default. | none |
| House Silk | Warm, smooth, gently held. | about -49 dB |
| Velvet Pop Lead | Polished, silky, forward modern pop lead. Its Tone shelves take a little weight out below 450 Hz and add a little presence from 3 kHz (the Pro-Q moves it was first paired with). | about -47 dB |
| Breathy Intimate | Close and airy; the breaths open up, the esses stay soft. | about -50 dB |
| Warm & Close | More preamp and Warmth: round, thick, cushioned. | about -46 dB |
| Transparent Polish | Barely there: de-harshed, lightly held, a touch of air. | about -64 dB |
| De-harsh Only | Just the resonance suppressor. | none |
| Velvet Ache | Warm, intimate R&B/pop with emotional bite (Silk 65%, Pain 35%). | cracks when pushed (Pain) |
| Torn Linen | Raw, overdriven, heart-on-sleeve chorus vocal (Silk 40%, Pain 80%). | cracks when pushed (Pain) |

The distortion figures are measured by `LinenCheck` on the built plugin. -50 dB is about 0.3%: felt as warmth, not heard as distortion.

## Using Tone

**Tone** shapes the voice before it reaches the preamp, like Pro-Q shelves in front of a channel strip. Taking a little weight out of the lows (instead of a high-pass) keeps the preamp clean and the voice full. A little lift from 3 kHz opens it up.

| Control | What it does |
|---|---|
| **Low shelf** | ±12 dB below its frequency (30 Hz–1 kHz, default 250 Hz). Half the gain at the frequency, all of it a few octaves below. |
| **High shelf** | ±12 dB above its frequency (1–16 kHz, default 3 kHz). |

The shelves are gentle and never overshoot. Drag a frequency pill up or down (Shift for fine steps), scroll on it, or double-click to reset. The display draws the EQ shape (Tone plus the console EQ) as a thin cobalt line. At 0 dB the shelves leave the sound untouched.

## Using the Preamp and EQ

The preamp is modelled on the behaviour of a classic 1073-style class-A channel: an input transformer, a biased class-A gain stage and an output transformer, all run at 4× oversampling.

| Control | What it does |
|---|---|
| **Drive** | Gain-stages the preamp the way a console is set up for a normally recorded vocal. On a belted -6 dBFS note: at 30% (the house sound) about -55 dB of 2nd harmonic, felt rather than heard; at 100% about -30 dB, warm but never fuzz. Higher settings also thicken low notes (transformer iron), and the bloom grows on loud lines and relaxes on soft ones. Drive changes colour, not loudness. At 0 the preamp is fully bypassed. |
| **Low** | Low shelf at 35, 60, 110 or 220 Hz, ±16 dB, with the gentle bump these shelves are known for. |
| **Mid** | Bell at 360 Hz, 700 Hz, 1.6, 3.2, 4.8 or 7.2 kHz, ±18 dB. It is broad at small settings and narrows as you push it. |
| **High 12k** | High shelf at 12 kHz, ±16 dB, for air and sheen. |

Click a frequency selector for its list, or scroll/drag over it to step.

## Using Dua Wox and Air

**Dua Wox** is Linen's compressor: two soft cells, built from how the classic variable-mu, diode-bridge and optical units behave (no copied circuits), in one feedback loop. Both hear the finished output, so they share the work instead of stacking up into a squash.

- **TUBE (the catch):** a 12 dB-wide knee and a ratio that firms up gently on the loudest notes, never past about 3:1. Its auto release lets a single peak go in about 0.16 s and a long loud line over a few seconds.
- **OPTO (the leveller):** a light-cell model. About 10 ms attack; half lets go in about 60 ms, the rest over 0.5–5 s, slower after long held phrases (the cell's memory). Natural knee, never past 3:1.
- **Warmth:** a tube line stage (mostly 2nd harmonic, at hardware levels: House Silk is about 0.3% on a loud note), plus a touch of low-mid bloom and a softer top while it holds the voice.
- **Weave:** the longer the singer pushes, the further the Warmth stage leans, so the colour deepens a little over seconds and eases back when the line relaxes.
- **Breath gate:** breaths and consonants hold both cells where they are, so breaths stay intimate instead of being squashed or pulled up.
- A 100 Hz sidechain high-pass, so plosives and boom don't pump it, and a ripple-free detector, so phrase onsets aren't over-compressed.

| Control | What it does |
|---|---|
| **TUBE / OPTO switches** | Run either cell alone. Out is an exact bypass. |
| **Thresh / Ratio** | Where the TUBE leans in and how firmly (the readout is the real ratio, 1:1 to about 2.3:1). |
| **Attack / Release** | The TUBE's attack and its fast recovery. |
| **Peak red.** | How hard the OPTO levels. 0 = off. |
| **Warmth** | The line stage's colour. 0 = clean. |
| **Blend** | Parallel compression: mix in the uncompressed voice. |
| **Auto gain** | Gives back most of what Dua Wox takes. |

The needle VU shows the total gain reduction; the two LED bars under it show how the TUBE and OPTO share it. With Ratio 1:1, Peak red. 0 and Warmth 0, Dua Wox passes the voice bit for bit.

**Air** opens a 10 kHz shelf with a little extra lift on quiet breaths, and adds a soft even-harmonic sparkle (run at 2× oversampling so it never aliases). Its **Silk loom** listens to De-harsh: where harshness was cut from the top, the sparkle grows, so the voice stays open without the fizz coming back. A sibilance detector compares the top band with the vowel band. When an S or T makes the top outweigh the vowel, Air closes, and **De-ess** turns it into a gentle cut. The small light next to "Air" glows on every S.

## Using De-harsh

De-harsh listens for frequencies that suddenly poke out above the rest of the voice (nasal ring, glassy 3 kHz glare, sharp sibilance) and turns down only those, only while they poke out. The display shows the vocal as a soft line; the copper **silk veil** hanging from the top is what is being removed right now.

| Control | What it does |
|---|---|
| **Depth** | How much of each harsh peak is removed. 0 is off (an exact pass-through). Start around 4–6. |
| **Detail** | Low = broad, gentle cuts; high = narrow, picky cuts that only catch sharp rings. |
| **Attack / Release** | How quickly cuts close in and let go. Slower release sounds smoother. |
| **Max cut** | The deepest any single frequency can be cut, so loud notes never lose their body. |
| **Soft / Hard** | Soft adapts to the singer's natural tone and is the most transparent; Hard catches more. |
| **Glare** | Leans the detection toward 2.5–5.5 kHz, where harshness hurts most. |
| **Body** | Tracks the sung note and never cuts its fundamental, so the vocal stays full. |
| **Delta** | Plays only what is being removed. Use it to check that nothing musical is being taken. |
| **Key** | Detect from the sidechain input instead (e.g. duck a beat's harsh bits where the vocal sits). |
| **Focus handles** | Drag the two gold handles on the display to limit where De-harsh works. Double-click resets. |

Linen adds about 2290 samples of latency at 44.1/48 kHz (about 48 ms). FL Studio compensates for it automatically.

## Build it yourself

You need CMake 3.22+ and a C++20 compiler (Visual Studio 2022 on Windows). JUCE 8 and melatonin_blur are downloaded automatically.

```sh
cmake -B build                       # add -G Ninja -DCMAKE_BUILD_TYPE=Release on Linux/macOS
cmake --build build --config Release
build/LinenTests_artefacts/Release/LinenTests      # unit tests
```

**Offline render** runs the exact plugin engine on a WAV file, so DSP changes can be measured without a DAW:

```sh
build/LinenRender_artefacts/Release/LinenRender in.wav out.wav --depth 6 --detail 0.5 --mix 1
```

**Plugin check** loads the built VST3 like a DAW. It runs the null test and measures each preset's distortion; add `--wav vocal.wav` to null-test your own recording too:

```sh
build/LinenCheck_artefacts/Release/LinenCheck build/Linen_artefacts/Release/VST3/Linen.vst3 --plot null.png
```

## Layout

| Path | Contents |
|---|---|
| `src/dsp/` | The signal chain (`LinenEngine`). No plugin or GUI code, so tests and the render tool run the same engine. |
| `src/Parameters.h` | Every parameter, in one place. Parameter IDs are permanent. |
| `src/PluginProcessor.*`, `src/PluginEditor.*` | The JUCE plugin wrapper and window. |
| `src/ui/` | Look and feel: linen, champagne and espresso, with cobalt light (`Palette.h`); linen backdrop, cards, jewel lamps (`Lights.*`), display. |
| `tests/` | DSP unit tests. |
| `tools/render.cpp` | Offline render CLI. |
| `tools/check.cpp` | Hosts the built VST3: bit-exact null test of Clean, distortion by preset, `--warmth` report against older builds (runs in CI). |
| `tools/snapshot.cpp` | Dev tool: renders the window to PNG while a test vocal plays (`-DLINEN_BUILD_SNAPSHOT=ON`). |
| `assets/fonts/` | Archivo (Expanded for titles, SemiCondensed for labels), cut from its variable font; SIL Open Font License (text included). |
