// Offline check of the Tempest engine: gain staging against the circuit's
// part values, frequency response, the limiter's static curve and timing,
// pickup loading, bypass, numerical sanity, and a rendered audition file.
//
//   clang++ -std=c++20 -O2 tests/tempest_test.cpp -o build/tempest_test && build/tempest_test [out.wav]

#include "../Source/TempestDSP.h"

#include <complex>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

// The plugin runs the engine at 4x; this is 4x 48 kHz.
static constexpr double kFs = 192000.0;
static constexpr double kPi = tempest::kPi;

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok)
        ++failures;
}

static double dB (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

static tempest::Params neutral()
{
    tempest::Params p;
    p.gain = 1.0;
    p.limiter = 0.0;
    p.boost = 1.0;
    p.pickup = 0;
    p.supply = 0;
    return p;
}

static std::vector<float> run (const tempest::Params& p, const std::vector<float>& in)
{
    tempest::Engine e;
    e.setParams (p);
    e.prepare (kFs);
    std::vector<float> buf = in;
    float* chans[1] = { buf.data() };
    for (size_t i = 0; i < buf.size(); i += 256)
    {
        chans[0] = buf.data() + i;
        e.process (chans, 1, (int) std::min<size_t> (256, buf.size() - i));
    }
    return buf;
}

// Steady-state gain of a sine, by projection onto sin/cos over whole cycles.
static double toneGain (const tempest::Params& p, double hz, double amplitude)
{
    const size_t n = (size_t) (kFs * 1.5);
    std::vector<float> in (n);
    for (size_t i = 0; i < n; ++i)
        in[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / kFs));
    const auto out = run (p, in);

    const size_t cycles = (size_t) std::floor (0.5 * hz);
    const size_t len = (size_t) std::lround ((double) cycles * kFs / hz);
    const size_t start = n - len;
    double s = 0.0, c = 0.0;
    for (size_t i = start; i < n; ++i)
    {
        const double ph = 2.0 * kPi * hz * (double) i / kFs;
        s += out[i] * std::sin (ph);
        c += out[i] * std::cos (ph);
    }
    return 2.0 * std::sqrt (s * s + c * c) / (double) len / amplitude;
}

// Gain reduction over time for a tone burst, read from the engine's meter.
struct Timing
{
    double attack90 = 0, steadyDb = 0, release3 = 0, release1 = 0;
};

static Timing limiterTiming (tempest::Params p, double amplitude)
{
    tempest::Engine e;
    e.setParams (p);
    e.prepare (kFs);

    const int block = 64;
    const double on = 1.0, off = 2.0, end = 5.0;
    std::vector<float> buf (block);
    std::vector<double> t, gr;
    size_t n = 0;
    while ((double) n / kFs < end)
    {
        for (int i = 0; i < block; ++i, ++n)
        {
            const double time = (double) n / kFs;
            const double a = (time >= on && time < off) ? amplitude : amplitude * 0.001;
            buf[(size_t) i] = (float) (a * std::sin (2.0 * kPi * 220.0 * time));
        }
        float* chans[1] = { buf.data() };
        e.process (chans, 1, block);
        t.push_back ((double) n / kFs);
        gr.push_back (e.getGainReductionDb());
    }

    Timing r;
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] > off - 0.1 && t[i] < off)
            r.steadyDb = std::max (r.steadyDb, gr[i]);
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] >= on && gr[i] >= 0.9 * r.steadyDb) { r.attack90 = t[i] - on; break; }
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] >= off && gr[i] <= 3.0) { r.release3 = t[i] - off; break; }
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] >= off && gr[i] <= 1.0) { r.release1 = t[i] - off; break; }
    return r;
}

static void writeWav (const char* path, const std::vector<float>& mono)
{
    FILE* f = std::fopen (path, "wb");
    if (f == nullptr)
        return;

    const uint32_t rate = 48000, dataBytes = (uint32_t) mono.size() * 2;
    const uint32_t riff = 36 + dataBytes, fmtLen = 16, byteRate = rate * 2;
    const uint16_t pcm = 1, chans = 1, align = 2, bits = 16;

    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f);
    std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&fmtLen, 4, 1, f);
    std::fwrite (&pcm, 2, 1, f); std::fwrite (&chans, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);

    for (float s : mono)
    {
        const int16_t v = (int16_t) std::lrint (std::clamp (s, -1.0f, 1.0f) * 32767.0f);
        std::fwrite (&v, 2, 1, f);
    }

    std::fclose (f);
}

// Plucked strings (Karplus-Strong), as a stand-in for a guitar DI. Power
// chords with open, ringing hits and choked ones, peaking around 0.5 V like
// a humbucker.
static std::vector<float> riff (double seconds)
{
    std::vector<float> out ((size_t) (seconds * kFs), 0.0f);
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    struct Hit { double at, root; double ring; };
    const double A = 110.0, D = 146.83, G = 98.0, E = 82.41;
    const Hit hits[] = { { 0.0, A, 1.4 }, { 1.5, A, 0.2 }, { 1.75, A, 0.2 }, { 2.0, D, 0.9 },
                         { 3.0, G, 0.9 }, { 4.0, A, 1.4 }, { 5.5, E, 0.2 }, { 5.75, E, 0.2 },
                         { 6.0, G, 0.9 }, { 7.0, D, 0.9 } };

    for (const auto& h : hits)
        for (double ratio : { 1.0, 1.4983, 2.0 })
        {
            const double f = h.root * ratio;
            const size_t len = (size_t) (kFs / f);
            std::vector<float> line (len);
            for (auto& v : line)
                v = noise (rng);
            for (int pass = 0; pass < 10; ++pass)
                for (size_t i = 1; i < len; ++i)
                    line[i] = 0.5f * (line[i] + line[i - 1]);

            size_t idx = 0;
            const double strum = ratio == 1.0 ? 0.0 : ratio < 1.9 ? 0.012 : 0.024;
            const size_t start = (size_t) ((h.at + strum) * kFs);
            const size_t stop = std::min (out.size(), start + (size_t) (h.ring * kFs));
            for (size_t n = start; n < stop; ++n)
            {
                const size_t next = (idx + 1) % len;
                const double fade = std::min (1.0, (double) (stop - n) / (0.01 * kFs));
                out[n] += (float) (0.4 * fade * line[idx]);
                line[idx] = 0.49975f * (line[idx] + line[next]);
                idx = next;
            }
        }

    float peak = 0.0f;
    for (float v : out)
        peak = std::max (peak, std::abs (v));
    for (auto& v : out)
        v *= 0.5f / peak;
    return out;
}

// 4:1 decimation with a windowed-sinc low-pass at 20 kHz.
static std::vector<float> decimate (const std::vector<float>& in)
{
    const int taps = 255, half = taps / 2;
    std::vector<double> h ((size_t) taps);
    const double fc = 20000.0 / kFs;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i)
    {
        const int m = i - half;
        const double sinc = m == 0 ? 2.0 * fc : std::sin (2.0 * kPi * fc * m) / (kPi * m);
        const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * i / (taps - 1)) + 0.08 * std::cos (4.0 * kPi * i / (taps - 1));
        h[(size_t) i] = sinc * w;
        sum += h[(size_t) i];
    }
    std::vector<float> out;
    for (size_t n = 0; n < in.size(); n += 4)
    {
        double acc = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const long k = (long) n + i - half;
            if (k >= 0 && k < (long) in.size())
                acc += h[(size_t) i] * in[(size_t) k];
        }
        out.push_back ((float) (acc / sum));
    }
    return out;
}

int main (int argc, char** argv)
{
    std::printf ("Gain staging (1 kHz, 1 mV, limiter off, GAIN and BOOST full)\n");
    {
        const double expected = dB (tempest::gainDivider (1.0) * tempest::kDarkGain
                                    * tempest::padRatio() * tempest::parts::lm386Gain);
        const double measured = dB (toneGain (neutral(), 1000.0, 0.001));
        std::printf ("  circuit %.2f dB, engine %.2f dB\n", expected, measured);
        check (std::abs (expected - measured) < 0.3, "maximum gain matches the part values");
        check (measured > 25.0 && measured < 31.0, "maximum gain sits between the manual's 25 dB and the brochure's 31 dB");

        auto p = neutral();
        p.boost = 0.25;   // about 9 o'clock on a 300-degree knob... roughly
        std::printf ("  BOOST at 0.25 of travel: %.1f dB\n", dB (toneGain (p, 1000.0, 0.001)));
        p.boost = 1.0;
        p.gain = 0.5;
        std::printf ("  GAIN at noon:            %.1f dB\n", dB (toneGain (p, 1000.0, 0.001)));
    }

    std::printf ("\nFrequency response, relative to 1 kHz (small signal)\n");
    {
        using namespace tempest::parts;
        // The circuit: IC1A's ground leg, then three coupling caps.
        auto circuit = [] (double f)
        {
            using C = std::complex<double>;
            const C s (0.0, 2.0 * kPi * f);
            auto hp = [&] (double tau) { return s * tau / (1.0 + s * tau); };
            const C stage1 = 1.0 + rFeedback / rGround * hp (rGround * cGround);
            return std::abs (stage1 * hp (cCouple1 * (rPad1 + rPad2 + rPad3))
                             * hp (cLm386In * rLm386In) * hp (cOut * (rOut + potBoost)));
        };
        const double ref = toneGain (neutral(), 1000.0, 0.001), cref = circuit (1000.0);
        const double freqs[] = { 20, 30, 40, 60, 100, 200, 5000, 10000, 15000, 20000 };
        double worst = 0.0;
        std::printf ("        Hz   circuit   engine\n");
        for (double f : freqs)
        {
            const double g = dB (toneGain (neutral(), f, 0.001) / ref), c = dB (circuit (f) / cref);
            std::printf ("  %8.0f  %+7.2f  %+7.2f\n", f, c, g);
            worst = std::max (worst, std::abs (g - c));
        }
        check (worst < 0.2, "matches the circuit's coupling-cap response within 0.2 dB");
    }

    std::printf ("\nPickup loading: humbucker and single coil into 100k, against 1M (dB)\n");
    {
        auto p = neutral();
        p.gain = 0.5;
        const double freqs[] = { 200, 1000, 2000, 2500, 3000, 4000, 6000, 10000 };
        double worstHum = 0.0, worstSingle = 0.0;
        std::printf ("        Hz   humbucker  single\n");
        for (double f : freqs)
        {
            p.pickup = 0;
            const double flat = toneGain (p, f, 0.001);
            p.pickup = 2;
            const double hum = dB (toneGain (p, f, 0.001) / flat);
            p.pickup = 1;
            const double single = dB (toneGain (p, f, 0.001) / flat);
            std::printf ("  %8.0f  %+9.2f  %+6.2f\n", f, hum, single);
            worstHum = std::min (worstHum, hum);
            worstSingle = std::min (worstSingle, single);
        }
        check (worstHum < -3.0 && worstSingle < -3.0, "100k load takes the pickup's resonance down by several dB");
    }

    std::printf ("\nStatic curve (220 Hz, GAIN full, BOOST full): output dBV / gain reduction dB\n");
    {
        std::printf ("    in dBV |    LIMITER 0      | LIMITER 0.5      | LIMITER 1\n");
        double grLow[3] = {}, grHigh[3] = {}, grMid[3] = {};
        for (double inDb = -60.0; inDb <= -10.0; inDb += 5.0)
        {
            std::printf ("  %8.0f |", inDb);
            for (int l = 0; l < 3; ++l)
            {
                auto p = neutral();
                p.limiter = 0.5 * l;
                const double amp = std::pow (10.0, inDb / 20.0) * std::sqrt (2.0);
                const auto t = limiterTiming (p, amp);
                const double g = toneGain (p, 220.0, amp);
                std::printf (" %+6.1f / %5.1f   |", inDb + dB (g), t.steadyDb);
                if (inDb == -60.0) grLow[l] = t.steadyDb;
                if (inDb == -10.0) grHigh[l] = t.steadyDb;
                if (inDb == -25.0) grMid[l] = t.steadyDb;
            }
            std::printf ("\n");
        }
        check (grLow[2] < 0.5, "no gain reduction on quiet signals");
        check (grMid[0] < 0.5, "limiter at zero does nothing at playing levels");
        check (grHigh[0] > 1.0, "...but, as in the circuit, a hot input still lights the LED near clipping");
        check (grHigh[2] > 10.0 && grHigh[2] < 19.0, "limiter full gives deep reduction, within the LDR's ~18 dB range");
        check (grHigh[1] > grHigh[0] && grHigh[2] > grHigh[1], "more LIMITER, more reduction");
    }

    std::printf ("\nLimiter timing (220 Hz burst at -20 dBV, LIMITER full)\n");
    {
        auto p = neutral();
        p.limiter = 1.0;
        const double amp = std::pow (10.0, -20.0 / 20.0) * std::sqrt (2.0);
        const auto t = limiterTiming (p, amp);
        std::printf ("  steady %.1f dB, 90%% attack %.1f ms, release to 3 dB %.0f ms, to 1 dB %.0f ms\n",
                     t.steadyDb, t.attack90 * 1000.0, t.release3 * 1000.0, t.release1 * 1000.0);
        check (t.attack90 < 0.3, "attack within a few hundred ms (most of it in the first few ms)");
        check (t.release3 > 0.03 && t.release3 < 0.6, "release is medium: tens to hundreds of ms");

        p.isolator = true;
        const auto s = limiterTiming (p, amp);
        std::printf ("  isolator cap fitted: steady %.1f dB, 90%% attack %.1f ms, release to 3 dB %.0f ms\n",
                     s.steadyDb, s.attack90 * 1000.0, s.release3 * 1000.0);

        // Ripple: on a low E the unsmoothed LED flickers at 165 Hz and the
        // fast layer of the LDR follows some of it.
        auto ripple = [&] (bool iso)
        {
            auto q = neutral();
            q.limiter = 1.0;
            q.isolator = iso;
            tempest::Engine e;
            e.setParams (q);
            e.prepare (kFs);
            std::vector<float> buf (16);
            double lo = 1e9, hi = 0.0;
            for (size_t n = 0; n < (size_t) (kFs * 1.5); n += buf.size())
            {
                for (size_t i = 0; i < buf.size(); ++i)
                    buf[i] = (float) (amp * std::sin (2.0 * kPi * 82.41 * (double) (n + i) / kFs));
                float* chans[1] = { buf.data() };
                e.process (chans, 1, (int) buf.size());
                if (n > (size_t) kFs)
                {
                    lo = std::min (lo, (double) e.getGainReductionDb());
                    hi = std::max (hi, (double) e.getGainReductionDb());
                }
            }
            return hi - lo;
        };
        const double bare = ripple (false), smoothed = ripple (true);
        std::printf ("  gain-reduction ripple on low E: %.2f dB bare, %.2f dB with the cap\n", bare, smoothed);
        check (smoothed < 0.5 * bare, "the isolator cap smooths the LED drive");
    }

    std::printf ("\nSupply\n");
    {
        auto p = neutral();
        const double amp = 0.5;
        p.supply = 0;
        const double g9 = dB (toneGain (p, 220.0, amp));
        p.supply = 1;
        const double g12 = dB (toneGain (p, 220.0, amp));
        std::printf ("  0.5 V peak in, fundamental gain: 9 V %.1f dB, 12 V %.1f dB\n", g9, g12);
        check (g12 > g9 + 2.0, "12 V gives more headroom");
    }

    std::printf ("\nBypass and sanity\n");
    {
        auto p = neutral();
        p.on = false;
        std::vector<float> in ((size_t) kFs);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = (float) (0.3 * std::sin (0.01 * (double) i));
        const auto out = run (p, in);
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - in[i]));
        check (worst < 1.0e-6f, "off returns the input exactly");

        tempest::Engine e;
        e.prepare (kFs);
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        bool finite = true;
        double worstVolts = 0.0;
        std::vector<float> buf (128);
        for (int b = 0; b < 6000; ++b)
        {
            tempest::Params q;
            q.gain = u (rng); q.limiter = u (rng); q.boost = u (rng);
            q.on = u (rng) > 0.1; q.supply = u (rng) > 0.5; q.isolator = u (rng) > 0.5;
            q.pickup = (int) (u (rng) * 3.0); q.inputVolts = 0.25 + 3.75 * u (rng);
            e.setParams (q);
            for (size_t i = 0; i < buf.size(); ++i)
                buf[i] = (float) (b % 3 == 0 ? (u (rng) * 2.0 - 1.0)
                                  : b % 3 == 1 ? ((i / 16) % 2 ? 1.0 : -1.0) : 0.0);
            float* chans[1] = { buf.data() };
            e.process (chans, 1, (int) buf.size());
            for (float v : buf)
            {
                finite = finite && std::isfinite (v);
                worstVolts = std::max (worstVolts, std::abs ((double) v) * 4.0);
            }
        }
        check (finite, "finite under noise and square waves with every control jumping");
        std::printf ("  worst output %.2f V-equivalent\n", worstVolts);
        check (worstVolts < 30.0, "bounded");
    }

    if (argc > 1)
    {
        const auto di = riff (8.5);
        struct Setting { const char* name; double gain, limiter, boost; bool on; };
        const Setting settings[] = {
            { "DI, pedal off",                       0.0, 0.0, 0.0, false },
            { "clean boost (GAIN 0.4, LIM 0)",       0.4, 0.0, 0.6, true },
            { "rhythm (GAIN 0.7, LIM 0.5)",          0.7, 0.5, 0.6, true },
            { "lead (GAIN 1, LIM 0.8)",              1.0, 0.8, 0.6, true },
            { "full, no limiter (GAIN 1, LIM 0)",    1.0, 0.0, 0.6, true },
        };

        std::vector<float> all;
        std::printf ("\nAudition: %s (each section peak-normalised to -3 dBFS)\n", argv[1]);
        for (const auto& s : settings)
        {
            tempest::Params p;
            p.gain = s.gain; p.limiter = s.limiter; p.boost = s.boost; p.on = s.on;
            const auto out = decimate (run (p, di));
            float peak = 0.0f;
            double rms = 0.0;
            for (float v : out) { peak = std::max (peak, std::abs (v)); rms += (double) v * v; }
            rms = std::sqrt (rms / (double) out.size());
            std::printf ("  %-36s peak %+5.1f dBV, rms %+5.1f dBV, crest %.1f dB\n",
                         s.name, dB (peak), dB (rms), dB (peak / rms));
            for (float v : out)
                all.push_back (v * 0.708f / peak);
        }
        writeWav (argv[1], all);
    }

    std::printf ("\n%s\n", failures == 0 ? "All checks passed" : "CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
