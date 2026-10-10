// ============================================================
//  Break DSP 冒烟测试（纯 C++，不需要 JUCE）
//
//  验证五件事：
//   1. 两踏板松开时直接旁通（输出 == 输入，0 延迟）
//   2. 满刹车真的 tape stop：音高下滑后淡出到静音
//   3. 满油门有输出且不失控
//   4. 随机踏板运动下不得出现 NaN / Inf / 电平失控
//   5. 踏板 → 音高方向（油门升高 / 刹车降低）
// ============================================================
#include "../TapeEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check (bool ok, const char* what, double value = 0.0)
{
    std::printf ("  [%s] %s", ok ? "PASS" : "FAIL", what);
    std::printf ("  (%.6g)", value);
    std::printf ("\n");
    if (! ok) ++failures;
}

static uint32_t rnd (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static float    rndF (uint32_t& s) { return (float) (rnd (s) >> 8) * (1.0f / 16777216.0f); }

struct Rig
{
    static constexpr double fs    = 48000.0;
    static constexpr int    block = 128;
    static constexpr int    chans = 2;

    tape::Engine engine;
    std::vector<float> ch[chans];
    float* ptrs[chans];
    double ph = 0.0;
    double bpm = 120.0;

    Rig()
    {
        engine.prepare (fs, chans);
        for (int c = 0; c < chans; ++c)
        {
            ch[c].assign ((size_t) block, 0.0f);
            ptrs[c] = ch[c].data();
        }
    }

    // 送 numBlocks 块（踏板恒定），把 ch0 输出按时间顺序接到 out
    void run (int numBlocks, float gas, float brake, std::vector<float>& out, double freqHz = 440.0)
    {
        out.reserve (out.size() + (size_t) numBlocks * block);
        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = (float) (0.5 * std::sin (2.0 * 3.14159265358979323846 * freqHz * ph / fs));
                ph += 1.0;
                ch[0][(size_t) i] = s;
                ch[1][(size_t) i] = s * 0.9f;
            }
            engine.process (ptrs, chans, block, gas, brake, bpm, -1);   // 自由运行节拍时钟
            for (int i = 0; i < block; ++i)
                out.push_back (ch[0][(size_t) i]);
        }
    }

    // 让磁带先跑满（静止旁通态，写头照写）
    void warmup (int numBlocks)
    {
        std::vector<float> junk;
        run (numBlocks, 0.0f, 0.0f, junk);
    }
};

static bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x)) return false;
    return true;
}

static double rms (const std::vector<float>& v, int from, int to)
{
    const int a = std::max (0, from);
    const int b = std::min ((int) v.size(), to);
    if (b <= a) return 0.0;
    double s = 0.0;
    for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (s / (b - a));
}

static double maxAbs (const std::vector<float>& v, int from)
{
    double m = 0.0;
    for (size_t i = (size_t) from; i < v.size(); ++i)
        m = std::max (m, (double) std::fabs (v[i]));
    return m;
}

int main()
{
    std::printf ("Break DSP smoke test\n\n");

    // ---- 1. 静止旁通（0 延迟直通） ----
    {
        std::printf ("1) 静止（两踏板 0）应旁通：输出 == 输入\n");
        Rig rig;
        const int nBlocks = 64;
        const int n = nBlocks * Rig::block;

        std::vector<float> in ((size_t) n), out;
        uint32_t s = 0x1234567u;
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = (rndF (s) * 2.0f - 1.0f) * 0.4f;

        out.reserve ((size_t) n);
        for (int b = 0; b < nBlocks; ++b)
        {
            for (int i = 0; i < Rig::block; ++i)
            {
                rig.ch[0][(size_t) i] = in[(size_t) (b * Rig::block + i)];
                rig.ch[1][(size_t) i] = in[(size_t) (b * Rig::block + i)];
            }
            rig.engine.process (rig.ptrs, Rig::chans, Rig::block, 0.0f, 0.0f, rig.bpm, -1);
            for (int i = 0; i < Rig::block; ++i)
                out.push_back (rig.ch[0][(size_t) i]);
        }

        double err = 0.0;
        for (int i = 0; i < n; ++i)
            err = std::max (err, (double) std::fabs (out[(size_t) i] - in[(size_t) i]));
        check (err < 1e-6, "旁通逐样点一致", err);
        check (allFinite (out), "数值有限");
    }

    // ---- 2. 满刹车 = tape stop ----
    {
        std::printf ("\n2) 满刹车应减速并淡出到静音\n");
        Rig rig;
        rig.warmup (120);
        std::vector<float> out;
        rig.run (250, 0.0f, 1.0f, out);

        const double early = rms (out, 128, 2300);
        const double late  = rms (out, (int) out.size() - 4000, (int) out.size());
        check (early > 0.01, "初期仍有声音（下滑过程）", early);
        check (late  < 0.010, "末段已停转（仅剩机械摩擦噪声）", late);
        check (late  < early * 0.25, "能量显著衰减", late / std::max (early, 1e-9));
        check (allFinite (out), "数值有限");
    }

    // ---- 3. 满油门 ----
    {
        std::printf ("\n3) 满油门应有输出且不失控\n");
        Rig rig;
        rig.warmup (120);
        std::vector<float> out;
        rig.run (200, 1.0f, 0.0f, out);

        check (maxAbs (out, 4096) > 0.05 && maxAbs (out, 4096) < 1.2, "峰值在合理范围", maxAbs (out, 4096));
        check (rms (out, 4096, (int) out.size()) > 0.02, "有持续输出", rms (out, 4096, (int) out.size()));
        check (allFinite (out), "数值有限");
    }

    // ---- 4. 随机踏板运动：数值稳定 ----
    {
        std::printf ("\n4) 随机踏板运动下的数值稳定性\n");
        Rig rig;
        uint32_t s = 12345u;
        double peak = 0.0;
        bool finite = true;
        for (int b = 0; b < 600; ++b)
        {
            const float gas   = rndF (s);
            const float brake = rndF (s);
            for (int i = 0; i < Rig::block; ++i)
            {
                const float v = (float) (0.6 * std::sin (2.0 * 3.14159265358979323846 * 220.0 * rig.ph / Rig::fs));
                rig.ph += 1.0;
                rig.ch[0][(size_t) i] = v;
                rig.ch[1][(size_t) i] = v;
            }
            rig.engine.process (rig.ptrs, Rig::chans, Rig::block, gas, brake, rig.bpm, -1);
            for (int i = 0; i < Rig::block; ++i)
            {
                const float x = rig.ch[0][(size_t) i];
                if (! std::isfinite (x)) finite = false;
                peak = std::max (peak, (double) std::fabs (x));
            }
        }
        check (finite, "全程无 NaN / Inf");
        check (peak < 1.5, "峰值未失控", peak);
    }

    // ---- 5. 音高方向：油门升高 / 刹车降低 ----
    {
        std::printf ("\n5) 踏板 → 走带音高方向\n");

        auto period = [] (const std::vector<float>& v, int from, int len) -> int
        {
            int bestLag = 0;
            double best = -1e9;
            for (int lag = 20; lag < len / 2; ++lag)
            {
                double s = 0.0, e0 = 0.0, e1 = 0.0;
                for (int i = 0; i + lag < len; ++i)
                {
                    const double a = v[(size_t) (from + i)], b = v[(size_t) (from + i + lag)];
                    s += a * b; e0 += a * a; e1 += b * b;
                }
                const double c = s / (std::sqrt (e0 * e1) + 1e-12);
                if (c > best) { best = c; bestLag = lag; }
            }
            return bestLag;
        };

        const int    win = 4096;
        const double refPeriod = Rig::fs / 440.0;   // 109.09

        struct Case { const char* name; float gas, brake; double expect; };
        const Case cases[] = {
            { "油门 0.6 → 2^(0.5*0.6)",  0.6f, 0.0f, std::exp2 ( 0.50 * 0.6) },
            { "刹车 0.3 → 2^(-4.6*0.3)", 0.0f, 0.3f, std::exp2 (-4.60 * 0.3) },
        };

        for (const auto& c : cases)
        {
            Rig rig;
            rig.warmup (120);
            std::vector<float> out;
            rig.run (100, c.gas, c.brake, out);          // 0.27 s 建立
            std::vector<float> more;
            rig.run (64, c.gas, c.brake, more);          // 再 0.17 s
            out.insert (out.end(), more.begin(), more.end());

            const int from = (int) out.size() - win;
            const int p = period (out, from, win);
            const double ratio = refPeriod / (double) p;
            std::printf ("     %s: 期望 %.4f, 实测 %.4f\n", c.name, c.expect, ratio);
            check (std::fabs (ratio - c.expect) < c.expect * 0.10, c.name,
                   std::fabs (ratio - c.expect) / c.expect);
        }
    }

    // ---- 6. 加速回卷对齐节拍（BPM 同步） ----
    {
        std::printf ("\n6) 加速时回卷（重复播放）应对齐宿主节拍\n");

        const double fs   = 48000.0;
        const double bpm  = 120.0;
        const int    beatLen = (int) std::lround (fs * 60.0 / bpm);   // 24000

        tape::Engine eng;
        eng.prepare (fs, 2);
        std::vector<float> b0 (1), b1 (1);
        float* p[2] = { b0.data(), b1.data() };
        double ph = 0.0;

        auto feed = [&] (int n, float gas, float brake)
        {
            for (int i = 0; i < n; ++i)
            {
                const float x = (float) (0.5 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * ph / fs));
                ph += 1.0;
                b0[0] = x; b1[0] = x;
                eng.process (p, 2, 1, gas, brake, bpm, -1);
            }
        };

        feed ((int) (1.0 * fs), 0.0f, 0.0f);            // 旁通暖机 1s（写头填缓冲）

        int prev = eng.getSpliceCount();
        std::vector<int> spliceAt;

        // 满油门跑 5s，逐样点记录每次回卷发生的时刻
        for (int i = 0; i < (int) (5.0 * fs); ++i)
        {
            const float x = (float) (0.5 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * ph / fs));
            ph += 1.0;
            b0[0] = x; b1[0] = x;
            eng.process (p, 2, 1, 1.0f, 0.0f, bpm, -1);

            const int c = eng.getSpliceCount();
            if (c != prev)
            {
                spliceAt.push_back (i);
                prev = c;
            }
        }

        check ((int) spliceAt.size() >= 2, "满油门 5s 内至少发生 2 次回卷", (double) spliceAt.size());

        if (! spliceAt.empty())
        {
            std::printf ("     回卷时刻（样本）: ");
            for (size_t k = 0; k < spliceAt.size(); ++k) std::printf ("%d ", spliceAt[k]);
            std::printf ("\n");

            // 所有回卷都应落在同一节拍相位（间隔是 beatLen 的整数倍）
            int maxDrift = 0;
            for (size_t k = 1; k < spliceAt.size(); ++k)
            {
                const int dt = spliceAt[k] - spliceAt[0];
                const int drift = dt % beatLen;
                // 取最近的节拍相位偏差（允许 ±1 的 off-by-one）
                const int nearer = std::min (drift, beatLen - drift);
                maxDrift = std::max (maxDrift, nearer);
            }
            check (maxDrift <= 2, "所有回卷对齐同一节拍相位", (double) maxDrift);
        }
    }

    // ---- 7. 松手瞬间读头重置（无追带尾巴） ----
    {
        std::printf ("\n7) 松手瞬间读头重置，音高立即恢复\n");

        const double fs = 48000.0;
        tape::Engine eng;
        eng.prepare (fs, 2);
        std::vector<float> b0 (1), b1 (1);
        float* p[2] = { b0.data(), b1.data() };
        double ph = 0.0;

        auto step = [&] (float gas, float brake) -> float
        {
            const float x = (float) (0.5 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * ph / fs));
            ph += 1.0;
            b0[0] = x; b1[0] = x;
            eng.process (p, 2, 1, gas, brake, 120.0, -1);
            return x;
        };

        for (int i = 0; i < 96000; ++i) step (0.0f, 0.0f);   // 旁通暖机 2s
        for (int i = 0; i < 24000; ++i) step (0.0f, 1.0f);   // 满刹车 0.5s（降调）

        // 松手后：应很快恢复旁通（输出 == 输入），而不是长时间追带加速
        double err = 0.0;
        for (int i = 0; i < 19200; ++i)                      // 0.4s
        {
            const float x = step (0.0f, 0.0f);
            if (i > 14400)                                   // 看最后 0.1s
                err = std::max (err, (double) std::fabs (b0[0] - x));
        }
        check (err < 1e-4, "松手后 0.4s 内完全恢复旁通（输出==输入）", err);
    }

    std::printf ("\n%s (%d 项失败)\n", failures == 0 ? "ALL_OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
