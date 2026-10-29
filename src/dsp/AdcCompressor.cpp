#include "AdcCompressor.h"

namespace linen
{

namespace
{
    constexpr float makeupReferenceDb = -10.0f;   // a typical sung vocal's loud notes at the compressor
    constexpr float makeupShare = 0.7f;           // auto gain gives back most, not all, of what it takes
    constexpr int textureInterval = 16;

    // TUBE: a wide 12 dB knee and a lean (dB of reduction per dB the output is
    // over its threshold; in a feedback loop the ratio is 1 + lean) that
    // firms up by up to 40% as the level climbs, so never much past 3:1.
    constexpr float tubeKneeDb = 12.0f;
    constexpr float tubeMaxLean = 1.4f;
    constexpr float tubeLeanSpan = 1.6f;          // how soon the Ratio knob gets there
    constexpr float tubeFirmUp = 0.4f, tubeFirmDb = 12.0f;

    // OPTO: sidechain gain (dB) at 0% Peak Reduction, and its span up to 100%.
    // The cell works in dB of reduction: the panel's light as the reduction it
    // would settle to, the cell approaching it, the trapped charge holding a share.
    constexpr float optoSidechainDbAtZero = -3.0f, optoSidechainDbSpan = 48.0f;
    constexpr float panelMaxDb = 40.0f;           // the panel's brightest
    constexpr float trappedShare = 0.5f;          // the share of the reduction the slow part holds
    constexpr float litDb = 0.4f;                 // the cell counts as lit (for its memory) above this

    // The detector's phase splitter (Olli Niemitalo's public-domain allpass
    // pair, coefficients squared, chain A read one sample late): within
    // 0.1 dB of the true amplitude from 50 Hz to 20 kHz at 48 kHz.
    constexpr std::array<double, 4> splitterA { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    constexpr std::array<double, 4> splitterB { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };

    // Colour.
    constexpr float tubeCurve = 0.28f;            // push-pull grid curve at full bias
    constexpr float gridLimit = 0.8f;             // the grid's swing saturates gently past this
    constexpr float bridgeImbalance = 0.025f;     // a little 2nd from the bridge, in step with the line stage's
    constexpr float warmthDrive = 0.03f;          // tube line stage drive at Warmth 100%
    constexpr double warmthBias = 0.3;
    constexpr float textureFullAtDb = 12.0f;
    constexpr float bloomDb = 2.0f;               // low-mid bloom at full texture
    constexpr float softenDb = -3.0f;             // top shelf at full texture

    double soft (double x) noexcept { return x / std::sqrt (1.0 + x * x); }

    double softSlope (double x) noexcept
    {
        const double d = 1.0 + x * x;
        return 1.0 / (d * std::sqrt (d));
    }

    float tubeLean (float ratioKnob)
    {
        return ratioKnob <= 1.0f ? 0.0f : tubeMaxLean * (1.0f - std::exp (-(ratioKnob - 1.0f) / tubeLeanSpan));
    }

    /** dB over the threshold through the tube's wide, quadratic knee. */
    float kneeOver (float overDb)
    {
        constexpr float half = 0.5f * tubeKneeDb;
        if (overDb <= -half)
            return 0.0f;
        if (overDb >= half)
            return overDb;
        return (overDb + half) * (overDb + half) / (2.0f * tubeKneeDb);
    }

    /** The tube's reduction (dB) for an output this far over its threshold:
        the lean grows smoothly with level, as a remote-cutoff tube's does. */
    float tubeTarget (float lean, float overDb)
    {
        if (lean <= 0.0f)
            return 0.0f;
        const float over = kneeOver (overDb);
        return lean * (1.0f + tubeFirmUp * (1.0f - std::exp (-over / tubeFirmDb))) * over;
    }

    float optoSidechainGain (float reduction)
    {
        return reduction <= 0.0f ? 0.0f
                                 : juce::Decibels::decibelsToGain (optoSidechainDbAtZero + optoSidechainDbSpan * juce::jmin (1.0f, reduction));
    }

    /** The reduction (dB) the opto cell settles to under this much panel
        drive: the cell is the shunt leg of a divider, its conductance in
        step with the light, the light with the drive squared. */
    float optoSettledDb (float drive)
    {
        return juce::jmin (panelMaxDb, 8.685889638f * std::log1p (drive));
    }

    /** How strongly the tube's grid curve bends the signal at this much reduction. */
    float tubeColourFor (float reductionDb)
    {
        return reductionDb <= 0.0f ? 0.0f : tubeCurve * (1.0f - std::exp (-reductionDb / 6.0f));
    }
}

//==============================================================================
double AdcCompressor::PhaseSplitter::envelopeSquared (double x) noexcept
{
    double inPhase = x, quadrature = x;
    for (auto& s : a)
        inPhase = s.process (inPhase);
    for (auto& s : b)
        quadrature = s.process (quadrature);

    const double late = aDelayed;
    aDelayed = inPhase;
    return late * late + quadrature * quadrature;
}

void AdcCompressor::PhaseSplitter::reset() noexcept
{
    for (auto* chain : { &a, &b })
        for (size_t i = 0; i < chain->size(); ++i)
        {
            auto& s = (*chain)[i];
            s.c = chain == &a ? splitterA[i] * splitterA[i] : splitterB[i] * splitterB[i];
            s.x1 = s.x2 = s.y1 = s.y2 = 0.0;
        }
    aDelayed = 0.0;
}

//==============================================================================
float AdcCompressor::tubeRatioFor (float ratioKnob)
{
    return 1.0f + tubeLean (ratioKnob);
}

float AdcCompressor::ratioKnobFor (float tubeRatio)
{
    const float lean = juce::jlimit (0.0f, 0.999f * tubeMaxLean, tubeRatio - 1.0f);
    return juce::jlimit (1.0f, 6.0f, 1.0f - tubeLeanSpan * std::log (1.0f - lean / tubeMaxLean));
}

float AdcCompressor::staticReductionDb (const CompParams& p, float inputDb)
{
    const float lean = p.tubeOn ? tubeLean (p.ratio) : 0.0f;
    const float sc = p.optoOn ? optoSidechainGain (p.optoReduction) : 0.0f;
    if (lean <= 0.0f && sc <= 0.0f)
        return 0.0f;

    // One feedback loop: find the output level whose own reduction brings the
    // input down to it, input - output = tube (output) + opto (output).
    const auto reductionAt = [&] (float outDb)
    {
        const float y = juce::Decibels::decibelsToGain (outDb, -400.0f);
        return tubeTarget (lean, outDb - p.thresholdDb) + optoSettledDb (0.5f * sc * sc * y * y);   // a sine's mean square
    };

    float lo = inputDb - 200.0f, hi = inputDb;
    for (int i = 0; i < 50; ++i)
    {
        const float out = 0.5f * (lo + hi);
        (inputDb - out > reductionAt (out) ? lo : hi) = out;
    }
    return inputDb - 0.5f * (lo + hi);
}

float AdcCompressor::makeupDb (const CompParams& p)
{
    // Most (not all) of the reduction a typical vocal sees, so the level stays
    // close while the compressor still sounds like it is working.
    return p.autoMakeup ? makeupShare * staticReductionDb (p, makeupReferenceDb) : 0.0f;
}

//==============================================================================
void AdcCompressor::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;
    const int chans = juce::jmax (1, numChannels);
    const auto coeff = [this] (double seconds) { return (float) std::exp (-1.0 / (seconds * sampleRate)); };

    sidechainHp.c = Biquad::Coeffs::highPass (sampleRate, 100.0, 0.707);
    sidechainPresence.c = Biquad::Coeffs::peak (sampleRate, 3500.0, 0.9, 3.0);

    slowCharge = coeff (2.0);
    slowRelease = coeff (1.5);

    lightRise = coeff (0.0005);
    lightFall = coeff (0.002);
    cellAttack = coeff (0.015);
    cellRelease = coeff (0.080);
    trapCharge = coeff (1.2);
    exposureRise = coeff (4.0);
    exposureFall = coeff (8.0);

    makeup.reset (sampleRate, 0.05);
    mix.reset (sampleRate, 0.025);
    gainRamp.assign ((size_t) maxBlockSize, 1.0f);
    tubeColourRamp.assign ((size_t) maxBlockSize, 0.0f);
    reductionRamp.assign ((size_t) maxBlockSize, 1.0f);

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) chans, 1,
                                                                     juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                     true, true);
    oversampling->initProcessing ((size_t) maxBlockSize);
    latency = juce::roundToInt (oversampling->getLatencyInSamples());
    colourHoldSamples = (int) (0.1 * sampleRate);

    compressed.setSize (chans, maxBlockSize, false, false, true);
    colour.setSize (chans, maxBlockSize, false, false, true);
    dry.setSize (chans, maxBlockSize, false, false, true);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) chans };
    for (auto* d : { &alignDelay, &dryDelay })
    {
        d->prepare (spec);
        d->setMaximumDelayInSamples (latency + 1);
        d->setDelay ((float) latency);
    }

    colourDcBlock.assign ((size_t) chans, {});
    for (auto& b : colourDcBlock)
        b.c = Biquad::Coeffs::highPassFirstOrder (sampleRate, 10.0);

    texture.assign ((size_t) chans, {});
    reset();
}

void AdcCompressor::reset()
{
    sidechainHp.reset();
    sidechainPresence.reset();
    splitter.reset();
    detecting = false;
    tubeFast = tubeSlow = 0.0f;
    light = cell = trapped = exposure = 0.0f;
    tubeMeter.store (0.0f);
    optoMeter.store (0.0f);
    makeup.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (makeupDb (params)));
    mix.setCurrentAndTargetValue (params.mix);

    if (oversampling != nullptr)
        oversampling->reset();
    alignDelay.reset();
    dryDelay.reset();
    for (auto& b : colourDcBlock)
        b.reset();
    colourHold = 0;

    textureAmount = 0.0f;
    for (auto& t : texture)
        for (auto& b : t)
        {
            b.reset();
            b.c = Biquad::Coeffs::identity();
        }
}

void AdcCompressor::updateTexture (float reduction)
{
    const float amount = juce::jlimit (0.0f, 1.0f, params.warmth) * juce::jlimit (0.0f, 1.0f, reduction / textureFullAtDb);
    if (std::abs (amount - textureAmount) < 0.002f && (amount > 0.0f || textureAmount <= 0.0f))
        return;

    textureAmount = amount < 0.001f ? 0.0f : amount;
    const auto bloom = textureAmount > 0.0f ? Biquad::Coeffs::peak (sampleRate, 280.0, 0.8, bloomDb * textureAmount)
                                            : Biquad::Coeffs::identity();
    const auto softTop = textureAmount > 0.0f ? Biquad::Coeffs::highShelf (sampleRate, 9000.0, 0.6, softenDb * textureAmount)
                                              : Biquad::Coeffs::identity();
    for (auto& t : texture)
    {
        t[0].c = bloom;
        t[1].c = softTop;
    }
}

void AdcCompressor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0 || n == 0)
        return;

    const float norm = 1.0f / (float) chans;
    const auto timeCoeff = [this] (float ms) { return std::exp (-1.0f / (ms * 0.001f * (float) sampleRate)); };
    const float attack = timeCoeff (juce::jmax (0.1f, params.attackMs));
    const float fastRelease = timeCoeff (juce::jmax (5.0f, params.releaseMs));

    const float lean = params.tubeOn ? tubeLean (params.ratio) : 0.0f;
    const float sidechain = params.optoOn ? optoSidechainGain (params.optoReduction) : 0.0f;

    // The trapped charge lets go over 0.5 s after a moment of light, up to
    // 5 s after a long hold. Exposure moves over seconds, so once a block is plenty.
    const float memorySeconds = 0.5f + 4.5f * exposure * std::sqrt (exposure);
    const float trapRelease = timeCoeff (1000.0f * memorySeconds);

    makeup.setTargetValue (juce::Decibels::decibelsToGain (makeupDb (params)));
    mix.setTargetValue (juce::jlimit (0.0f, 1.0f, params.mix));

    // 1. The loop, sample by sample. Both cells hear the finished output
    //    (feedback): the input's envelope less the reduction they apply right
    //    now, which is the output's level without the sidechain filters'
    //    delay inside the loop (that delay makes a fast attack overshoot).
    for (int i = 0; i < n; ++i)
    {
        float x = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            x += buffer.getSample (ch, i);
        x *= norm;

        const float tubeDb = juce::jmax (tubeFast, tubeSlow);
        const float optoDb = juce::jmax (cell, trappedShare * trapped);
        const float reduction = juce::Decibels::decibelsToGain (-(tubeDb + optoDb));

        const bool tubeActive = lean > 0.0f || tubeDb > 0.0f;
        const bool optoActive = sidechain > 0.0f || light > 0.0f || cell > 0.0f || trapped > 0.0f;
        if (! (tubeActive || optoActive))
        {
            detecting = false;
        }
        else
        {
            if (! detecting)
            {
                sidechainHp.reset();
                sidechainPresence.reset();
                splitter.reset();
                detecting = true;
            }

            const auto inputEnvelope = (float) splitter.envelopeSquared (sidechainPresence.process (sidechainHp.process (x)));
            const float envelope = inputEnvelope * reduction * reduction;   // the output's peak, squared

            // TUBE: the bias follows the output's level over the threshold.
            // Fast capacitor: Attack / Release. Slow one: charges only while
            // the voice is held down for a while, then lets go over seconds.
            if (tubeActive)
            {
                const float levelDb = 10.0f * std::log10 (envelope + 1.0e-12f);
                const float target = tubeTarget (lean, levelDb - params.thresholdDb);
                tubeFast = target + (tubeFast - target) * (target > tubeFast ? attack : fastRelease);
                tubeSlow = tubeFast + (tubeSlow - tubeFast) * (tubeFast > tubeSlow ? slowCharge : slowRelease);
                if (target <= 0.0f && tubeFast < 1.0e-6f && tubeSlow < 1.0e-6f)
                    tubeFast = tubeSlow = 0.0f;
            }

            // OPTO: the panel lit by the sidechain, the cell following the light.
            if (optoActive)
            {
                const float glow = optoSettledDb (0.5f * sidechain * sidechain * envelope);
                light = glow + (light - glow) * (glow > light ? lightRise : lightFall);
                cell = light + (cell - light) * (light > cell ? cellAttack : cellRelease);

                // Trapped charge fills while the cell is lit; how long it has
                // been lit sets how slowly it empties (the cell's memory).
                const float lit = cell > litDb ? 1.0f : 0.0f;
                exposure = lit + (exposure - lit) * (lit > exposure ? exposureRise : exposureFall);
                trapped = cell + (trapped - cell) * (cell > trapped ? trapCharge : trapRelease);

                if (glow <= 0.0f && light < 1.0e-6f && cell < 1.0e-6f && trapped < 1.0e-6f)
                    light = cell = trapped = exposure = 0.0f;
            }
        }

        reductionRamp[(size_t) i] = reduction;
        tubeColourRamp[(size_t) i] = tubeColourFor (tubeDb);
        gainRamp[(size_t) i] = reduction * makeup.getNextValue();
    }

    tubeMeter.store (juce::jmax (tubeFast, tubeSlow), std::memory_order_relaxed);
    optoMeter.store (juce::jmax (cell, trappedShare * trapped), std::memory_order_relaxed);

    // 2. Pure gain, kept for the latency-aligned path and the colour stage.
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* c = compressed.getWritePointer (ch);
        auto* d = dry.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            c[i] = in[i] * gainRamp[(size_t) i];
            dryDelay.pushSample (ch, in[i]);
            d[i] = dryDelay.popSample (ch);
        }
    }

    // 3. Colour at 2x, as a difference: the tube's grid curve (only while it
    //    reduces gain) and the Warmth line stage. Without either it adds nothing.
    const float warmth = juce::jlimit (0.0f, 1.0f, params.warmth);
    const bool sources = warmth > 0.0f || tubeColourRamp[0] > 0.0f || tubeColourRamp[(size_t) n - 1] > 0.0f;
    if (sources)
        colourHold = colourHoldSamples;

    const bool colouring = colourHold > 0;
    if (colouring)
    {
        const double drive = warmthDrive * warmth;
        const double lineOffset = soft (warmthBias), lineNorm = drive > 0.0 ? 1.0 / (drive * softSlope (warmthBias)) : 0.0;

        juce::dsp::AudioBlock<float> in (compressed.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        auto up = oversampling->processSamplesUp (in);
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* u = up.getChannelPointer ((size_t) ch);
            for (int j = 0; j < (int) up.getNumSamples(); ++j)
            {
                const auto k = (size_t) (j / 2);
                const float curve = tubeColourRamp[k];
                float diff = 0.0f;

                // Push-pull tube: the grid swings with the ADC's input, before
                // any reduction (cosh - 1 written as 2 sinh^2, exact when small).
                if (curve > 0.0f)
                {
                    const float swing = curve * u[j] / juce::jmax (1.0e-6f, gainRamp[k]);
                    const float grid = gridLimit * std::tanh (swing / gridLimit);
                    const float s = std::sinh (0.5f * grid);
                    diff += u[j] * (2.0f * s * s - bridgeImbalance * grid);
                }

                // Warmth: a biased line stage with unity small-signal gain
                // (in double: the difference is far smaller than the signal).
                if (drive > 0.0)
                    diff += (float) ((soft (drive * u[j] + warmthBias) - lineOffset) * lineNorm - (double) u[j]);

                u[j] = diff;
            }
        }
        juce::dsp::AudioBlock<float> out (colour.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        oversampling->processSamplesDown (out);

        if (! sources)
            colourHold = juce::jmax (0, colourHold - n);
    }

    // 4. Assemble: aligned gain + colour → texture.
    for (int start = 0; start < n; start += textureInterval)
    {
        const int len = juce::jmin (textureInterval, n - start);
        updateTexture (-juce::Decibels::gainToDecibels (reductionRamp[(size_t) start], -100.0f));

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* outData = buffer.getWritePointer (ch);
            const auto* c = compressed.getReadPointer (ch);
            const auto* col = colour.getReadPointer (ch);
            auto& dc = colourDcBlock[(size_t) ch];
            auto& t = texture[(size_t) ch];

            for (int i = start; i < start + len; ++i)
            {
                alignDelay.pushSample (ch, c[i]);
                float wet = alignDelay.popSample (ch);
                if (colouring)
                    wet += dc.process (col[i]);
                outData[i] = t[1].process (t[0].process (wet));
            }
        }
    }

    // The colour has finished its tail: start clean next time.
    if (colouring && colourHold == 0)
        for (auto& b : colourDcBlock)
            b.reset();

    // 5. Parallel blend with the (equally delayed) dry voice.
    for (int i = 0; i < n; ++i)
    {
        const float m = mix.getNextValue();
        if (m >= 1.0f)
            continue;
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* outData = buffer.getWritePointer (ch);
            const float d = dry.getSample (ch, i);
            outData[i] = d + (outData[i] - d) * m;
        }
    }
}

} // namespace linen
