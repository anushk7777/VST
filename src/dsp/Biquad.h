#pragma once

#include <cmath>

namespace linen
{

/** A transposed direct form II biquad with RBJ cookbook designs. Plain
    numbers, no allocation, so coefficients can be redesigned on the audio
    thread while a parameter glides. */
struct Biquad
{
    struct Coeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        static Coeffs identity() { return {}; }

        static Coeffs normalised (double b0, double b1, double b2, double a0, double a1, double a2)
        {
            return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
        }

        static Coeffs peak (double sampleRate, double hz, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w0 = omega (sampleRate, hz);
            const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalised (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
        }

        static Coeffs lowShelf (double sampleRate, double hz, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w0 = omega (sampleRate, hz);
            const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0), s = 2.0 * std::sqrt (A) * alpha;
            return normalised (A * ((A + 1.0) - (A - 1.0) * c + s), 2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                               A * ((A + 1.0) - (A - 1.0) * c - s), (A + 1.0) + (A - 1.0) * c + s,
                               -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - s);
        }

        static Coeffs highShelf (double sampleRate, double hz, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w0 = omega (sampleRate, hz);
            const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0), s = 2.0 * std::sqrt (A) * alpha;
            return normalised (A * ((A + 1.0) + (A - 1.0) * c + s), -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                               A * ((A + 1.0) + (A - 1.0) * c - s), (A + 1.0) - (A - 1.0) * c + s,
                               2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - s);
        }

        static Coeffs highPass (double sampleRate, double hz, double q)
        {
            const double w0 = omega (sampleRate, hz), alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalised ((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
        }

        static Coeffs lowPass (double sampleRate, double hz, double q)
        {
            const double w0 = omega (sampleRate, hz), alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
            return normalised ((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
        }

        static Coeffs highPassFirstOrder (double sampleRate, double hz)
        {
            const double k = std::tan (omega (sampleRate, hz) / 2.0);
            return { 1.0 / (1.0 + k), -1.0 / (1.0 + k), 0.0, (k - 1.0) / (k + 1.0), 0.0 };
        }

        /** Magnitude response in dB at hz (for tests and drawing). */
        double magnitudeDb (double sampleRate, double hz) const
        {
            const double w = omega (sampleRate, hz);
            const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2.0 * w), c2i = -std::sin (2.0 * w);
            const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
            const double dr = 1.0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
            return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di));
        }

        static double omega (double sampleRate, double hz)
        {
            return 2.0 * 3.14159265358979323846 * std::fmin (hz, sampleRate * 0.49) / sampleRate;
        }
    };

    float process (float x) noexcept
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept { s1 = s2 = 0.0; }

    Coeffs c;
    double s1 = 0.0, s2 = 0.0;
};

} // namespace linen
