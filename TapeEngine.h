#pragma once

// ============================================================
//  TapeEngine —— Break 的走带机构 DSP（纯 C++，不依赖 JUCE）
//
//  语义：整台机器就是一台磁带机，两个踏板 = 走带控制，没有任何其他控件。
//    · 油门 gas   ：走带加速。速度 >1 → 音高上扬 + 变亮 + 磁带饱和推进。
//                  加速时读头追写头，回卷按宿主 BPM 对齐（片段重复落在节拍上）。
//    · 刹车 brake ：走带减速直至 tape stop。速度 →0.04 → 音高下滑、
//                  wow/flutter 加剧、高频丢失（磁头损耗）、摩擦噪声，
//                  最终淡出到静音（磁带停转）。
//    · 两踏板都松开：直接旁通（输出 = 输入，0 延迟），读头空转待命。
//
//  变速实现：真·varispeed（读写分离 + 分数延迟线插值），刹车时时长是真的
//  被拉长、油门时是真的被压缩。读头需要回卷（循环）时做交叉淡化"接头"。
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tape
{
    inline constexpr float pi = 3.14159265358979323846f;

    // ---------------- 踏板 → 走带 ----------------
    // 速度按 2^八度 映射：踏板行程 = 音高线性，符合听感
    inline constexpr float gasOctave     =  0.50f;   // 满油门 +0.5 八度
    inline constexpr float brakeOctave   = -4.60f;   // 满刹车 -4.6 八度（speed ≈ 0.041）
    inline constexpr float minSpeed      =  0.041f;
    inline constexpr float maxSpeed      =  1.60f;

    // ---------------- 走带缓冲 / 循环 ----------------
    inline constexpr float bufferSeconds =  4.00f;   // 缓冲长度（向上取到 2 的幂）
    inline constexpr int   beatsPerLoop  =  1;       // 一个循环 = 多少个 beat（重复长度）
    inline constexpr int   minLoopLen    =  512;     // loop 长度下限（防退化）
    inline constexpr int   minGuard      =  128;     // 读写头最小间距（须 > 插值前视）
    inline constexpr int   maxGuardBack  =  256;     // 最大间距 = N - maxGuardBack
    inline constexpr float spliceFadeSec =  0.040f;  // 接头交叉淡化时长
    inline constexpr float minBpm        =  30.0f;
    inline constexpr float maxBpm        =  400.0f;

    // ---------------- 平滑时间常数（秒） ----------------
    inline constexpr float gasTauSec   = 0.090f;   // 油门：踩下去有惯性
    inline constexpr float brakeTauSec = 0.045f;   // 刹车：更"咬手"
    inline constexpr float speedUpTau  = 0.080f;   // 走带加速
    inline constexpr float speedDnTau  = 0.035f;   // 走带制动（比加速快）
    inline constexpr float wetTauSec   = 0.030f;   // 旁通↔走带 交叉淡化

    // ---------------- wow / flutter ----------------
    inline constexpr float wowRateHz     = 0.55f;   // 主导轴偏心
    inline constexpr float flutterRateHz = 7.30f;   // 压带轮抖动

    // ---------------- 停止闸门 ----------------
    inline constexpr float gateOpenSpeed = 0.16f;   // 高于此速度完全开通
    inline constexpr float gateShutSpeed = 0.055f;  // 低于此速度完全静音（磁带停转）

    inline float clamp (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

    inline float smoothstep01 (float x) noexcept
    {
        const float t = clamp (x, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // TPT 一阶低通系数（Andy Simper 形式）：0 < G < 1
    inline float tptG (float fc, float fs) noexcept
    {
        const float f = clamp (fc, 10.0f, fs * 0.45f);
        const float g = std::tan (pi * f / fs);
        return g / (1.0f + g);
    }

    // 一阶 TPT 低通单元
    struct OnePole
    {
        float s = 0.0f;
        inline float lp (float x, float G) noexcept
        {
            const float v = (x - s) * G;
            const float y = v + s;
            s = y + v;
            return y;
        }
        inline float hp (float x, float G) noexcept { return x - lp (x, G); }
    };

    // ---------------- 音色系数（每块计算一次，块内线性插值） ----------------
    struct Tone
    {
        float lpG         = 0.9f;    // 磁头低通（两阶串联）
        float lpMix       = 0.0f;    // 低通并联量 = 刹车量（0 时完全不介入）
        float hpG         = 0.0f;    // 高通（油门去隆隆声）
        float hpMix       = 0.0f;    // 高通并联量 = 油门量
        float tiltG       = 0.1f;    // tilt 的低通核
        float tiltA       = 1.0f;    // 高频增益 A
        float tiltB       = 0.0f;    // (1/A - A)，用于 y = A*x + tiltB*lp
        float hissLpG     = 0.5f;
        float hissLevel   = 0.0f;
        float scrapeF     = 0.2f;    // Chamberlin SVF 频率系数
        float scrapeQ     = 0.7f;    // SVF 阻尼
        float scrapeLevel = 0.0f;
        float driveG      = 1.0f;
        float driveTrim   = 1.0f;
        float driveMix    = 0.0f;
    };

    inline Tone makeTone (float gas, float brake, float fs) noexcept
    {
        Tone c;

        // 磁头低通：brake 0 → 17kHz，brake 1 → 260Hz（磁带慢下来高频全丢）
        // 用并联混合量控制介入深度：brake=0 时 lpMix=0 → 完全旁通，保证透明
        c.lpG   = tptG (17000.0f * std::pow (260.0f / 17000.0f, brake), fs);
        c.lpMix = brake;

        // 高通：油门抬起时去掉隆隆声。同理用并联量，gas=0 时完全旁通
        c.hpG   = tptG (10.0f * std::pow (190.0f / 10.0f, gas), fs);
        c.hpMix = gas;

        // Tilt EQ（pivot 1kHz）：油门提亮，刹车压暗
        const float tiltDb = 5.5f * gas - 7.5f * brake;
        const float A = std::pow (10.0f, tiltDb / 40.0f);
        c.tiltG = tptG (1000.0f, fs);
        c.tiltA = A;
        c.tiltB = 1.0f / A - A;

        // 磁带底噪（写入前注入 → 会随走带一起变速）
        c.hissLpG   = tptG (6500.0f, fs);
        c.hissLevel = 0.0006f + 0.012f * brake + 0.0020f * gas;

        // 机械摩擦/制动啸叫（走带后注入 → 不随变速移动）
        const float sf = 1500.0f + 1200.0f * brake;
        c.scrapeF     = 2.0f * std::sin (pi * clamp (sf, 50.0f, fs * 0.2f) / fs);
        c.scrapeQ     = 1.0f / 1.6f;
        c.scrapeLevel = 0.016f * brake * brake;

        // 磁带饱和
        c.driveG    = 1.0f + 3.0f * gas;
        c.driveTrim = 1.0f / (1.0f + 1.05f * gas);
        c.driveMix  = gas;

        return c;
    }

    inline Tone lerpTone (const Tone& a, const Tone& b, float t) noexcept
    {
        Tone o;
        o.lpG         = lerp (a.lpG,         b.lpG,         t);
        o.lpMix       = lerp (a.lpMix,       b.lpMix,       t);
        o.hpG         = lerp (a.hpG,         b.hpG,         t);
        o.hpMix       = lerp (a.hpMix,       b.hpMix,       t);
        o.tiltG       = lerp (a.tiltG,       b.tiltG,       t);
        o.tiltA       = lerp (a.tiltA,       b.tiltA,       t);
        o.tiltB       = lerp (a.tiltB,       b.tiltB,       t);
        o.hissLpG     = lerp (a.hissLpG,     b.hissLpG,     t);
        o.hissLevel   = lerp (a.hissLevel,   b.hissLevel,   t);
        o.scrapeF     = lerp (a.scrapeF,     b.scrapeF,     t);
        o.scrapeQ     = lerp (a.scrapeQ,     b.scrapeQ,     t);
        o.scrapeLevel = lerp (a.scrapeLevel, b.scrapeLevel, t);
        o.driveG      = lerp (a.driveG,      b.driveG,      t);
        o.driveTrim   = lerp (a.driveTrim,   b.driveTrim,   t);
        o.driveMix    = lerp (a.driveMix,    b.driveMix,    t);
        return o;
    }

    // ---------------- 单通道走带机构（读写分离 + 分数插值 + 交叉淡化回卷） ----------------
    class Transport
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            int want = (int) std::ceil (sampleRate * (double) bufferSeconds);
            N = 1;
            while (N < want) N <<= 1;
            mask = N - 1;
            buf.assign ((size_t) N, 0.0f);
            fadeLen = jmaxInt (8, (int) std::round (spliceFadeSec * fs));
            reset();
        }

        void reset()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            w = 0;
            rA = rB = 0.0f;
            fadeIdx = 0;
            fading = false;
        }

        // 立即把读头摆到 delay = d（旁通态/初始化用；无输出 → 无咔哒）
        void setDelay (int d) noexcept
        {
            rA = wrapPos ((float) (w - d));
            rB = rA;
            fadeIdx = 0;
            fading = false;
        }

        int bufferSize() const noexcept { return N; }

        // 读头滞后（样点数）
        int getDelaySamples() const noexcept
        {
            int d = w - (int) rA;
            if (d < 0) d += N;
            return d;
        }

        // 外部请求一次"回卷到目标延迟"（交叉淡化，正在淡化时拒绝防重叠）
        // beginSplice(delta) 里读头位置 += delta，新 delay = d - delta，
        // 所以要把 delay 搬到 targetDelay，需要 delta = d - targetDelay。
        void requestSpliceTo (int targetDelay) noexcept
        {
            if (fading) return;
            const int d = getDelaySamples();
            beginSplice (d - targetDelay);
        }

        int  getSpliceCount() const noexcept { return spliceCount; }   // 诊断：累计回卷次数
        bool isFading() const noexcept { return fading; }              // 淡化中（读头正在回卷过渡）

        // 写 + 读（不自动回卷；Engine 负责调度，这里只留"读坏数据"的极值兜底）
        inline float processSample (float x, float speed) noexcept
        {
            buf[(size_t) w] = x;
            w = (w + 1) & mask;

            float y;

            if (fading)
            {
                // 等功率交叉淡化：回卷时不留咔哒
                const float t  = (float) fadeIdx / (float) fadeLen;
                const float gA = std::cos (0.5f * pi * t);
                const float gB = std::sin (0.5f * pi * t);
                y = read (rA) * gA + read (rB) * gB;

                rA = advance (rA, speed);
                rB = advance (rB, speed);

                if (++fadeIdx >= fadeLen)
                {
                    fading = false;
                    rA = rB;
                }
            }
            else
            {
                y = read (rA);
                rA = advance (rA, speed);
            }

            // 极值兜底（正常路径不该到）：读头追上写头 / 落到缓冲底
            const int d = getDelaySamples();
            if (d < 2)
                beginSplice (-(N >> 1));          // delay 太小 → 读头往后跳半圈（增大 delay）
            else if (d > N - maxGuardBack)
                beginSplice (N >> 1);             // delay 太大 → 读头往前跳半圈（减小 delay）

            return y;
        }

    private:
        static int jmaxInt (int a, int b) noexcept { return a > b ? a : b; }

        inline void beginSplice (int delta) noexcept
        {
            rB      = wrapPos (rA + (float) delta);
            fadeIdx = 0;
            fading  = true;
            ++spliceCount;
        }

        inline float wrapPos (float p) const noexcept
        {
            while (p < 0.0f)      p += (float) N;
            while (p >= (float) N) p -= (float) N;
            return p;
        }

        inline float advance (float p, float speed) const noexcept
        {
            p += speed;
            while (p >= (float) N) p -= (float) N;
            while (p < 0.0f)       p += (float) N;
            return p;
        }

        // 4 点 Catmull-Rom（三次 Hermite）分数插值
        inline float read (float p) const noexcept
        {
            const int   i = (int) p;
            const float f = p - (float) i;
            const float y0 = buf[(size_t) ((i - 1) & mask)];
            const float y1 = buf[(size_t) ( i      & mask)];
            const float y2 = buf[(size_t) ((i + 1) & mask)];
            const float y3 = buf[(size_t) ((i + 2) & mask)];

            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = 0.5f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3);
            const float c3 = 0.5f * (-y0 + 3.0f * y1 - 3.0f * y2 + y3);
            return ((c3 * f + c2) * f + c1) * f + c0;
        }

        std::vector<float> buf;
        int   N = 1, mask = 0, w = 0;
        int   fadeLen = 64, fadeIdx = 0, spliceCount = 0;
        float rA = 0.0f, rB = 0.0f;
        bool  fading = false;
        float fs = 48000.0f;
    };

    // ---------------- 单声道完整信号链 ----------------
    class Voice
    {
    public:
        void prepare (double sampleRate, int channelIndex)
        {
            transport.prepare (sampleRate);
            fs = (float) sampleRate;
            // 两路走带机构相位错开 → 立体声宽度（真实磁带两轨不同步）
            wowPh  = (channelIndex & 1) ? 0.50f : 0.0f;
            flutPh = (channelIndex & 1) ? 0.31f : 0.0f;
            rng    = 0x9E3779B9u + (uint32_t) channelIndex * 2654435761u;
            reset();
        }

        void reset()
        {
            transport.reset();
            lp1.s = lp2.s = tiltLp.s = hpLp.s = hissLp.s = 0.0f;
            svfLow = svfBand = 0.0f;
        }

        void setDelay (int d) noexcept { transport.setDelay (d); }
        int  getDelaySamples() const noexcept { return transport.getDelaySamples(); }
        int  bufferSize() const noexcept { return transport.bufferSize(); }
        int  getSpliceCount() const noexcept { return transport.getSpliceCount(); }
        bool isFading() const noexcept { return transport.isFading(); }
        void requestSpliceTo (int d) noexcept { transport.requestSpliceTo (d); }

        // 旁通态：只推进写头 + 读头空转（保持 delay），返回输入本身（不走滤波链）
        inline float processBypass (float x, float speed) noexcept
        {
            transport.processSample (x, speed);
            return x;
        }

        inline float processSample (float x, float speed, float gate,
                                    float wowDepth, float flutDepth,
                                    const Tone& c) noexcept
        {
            // 1) 磁带饱和（写在磁带上 → 会跟着一起变速）
            const float d = std::tanh (c.driveG * x) * c.driveTrim;
            float w = x + c.driveMix * (d - x);

            // 2) 磁带底噪（同样写在磁带上）
            const float nHiss = white() * 2.0f - 1.0f;
            w += (nHiss - hissLp.lp (nHiss, c.hissLpG)) * c.hissLevel;

            // 3) wow / flutter：直接调制走带速度
            wowPh  += wowRateHz / fs;    if (wowPh  >= 1.0f) wowPh  -= 1.0f;
            flutPh += flutterRateHz / fs; if (flutPh >= 1.0f) flutPh -= 1.0f;
            const float mod = 1.0f
                            + wowDepth  * std::sin (2.0f * pi * wowPh)
                            + flutDepth * std::sin (2.0f * pi * flutPh);

            // 4) 走带
            float y = transport.processSample (w, clamp (speed * mod, minSpeed, maxSpeed + 0.30f));
            y *= gate;

            // 5) 高通（油门去隆隆声）：y -= hpMix * lp(y)，hpMix=0 时零介入
            y -= c.hpMix * hpLp.lp (y, c.hpG);

            // 6) Tilt EQ：y = A*x + (1/A - A)*lp
            y = c.tiltA * y + c.tiltB * tiltLp.lp (y, c.tiltG);

            // 7) 磁头低通（两阶 12dB/oct）：并联混合，brake=0 时零介入
            y += c.lpMix * (lp2.lp (lp1.lp (y, c.lpG), c.lpG) - y);

            // 8) 机械摩擦噪声：带通白噪（走带后，不变速）
            if (c.scrapeLevel > 0.0f)
            {
                const float n = white() * 2.0f - 1.0f;
                svfLow  += c.scrapeF * svfBand;
                const float high = n - svfLow - c.scrapeQ * svfBand;
                svfBand += c.scrapeF * high;
                y += svfBand * c.scrapeLevel;
            }

            return softClip (y);
        }

    private:
        inline uint32_t xorshift() noexcept
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return rng;
        }
        inline float white() noexcept
        {
            return (float) (xorshift() >> 8) * (1.0f / 16777216.0f);   // 0..1
        }
        static inline float softClip (float v) noexcept
        {
            // 0.7 以下线性，之上软限制到 ~0.95
            const float t = 0.7f;
            if (v > t)  return t + (1.0f - t) * std::tanh ((v - t) / (1.0f - t));
            if (v < -t) return -t - (1.0f - t) * std::tanh ((-v - t) / (1.0f - t));
            return v;
        }

        Transport transport;
        OnePole   lp1, lp2, tiltLp, hpLp, hissLp;
        float     svfLow = 0.0f, svfBand = 0.0f;
        float     wowPh = 0.0f, flutPh = 0.0f;
        float     fs = 48000.0f;
        uint32_t  rng = 0x9E3779B9u;
    };

    // ---------------- 立体声（或更多声道）引擎 ----------------
    class Engine
    {
    public:
        void prepare (double sampleRate, int numChannels)
        {
            fs = (float) sampleRate;
            const int n = std::max (2, numChannels);
            voices.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
                voices[(size_t) i].prepare (sampleRate, i);

            gasSm = brkSm = 0.0f;
            speedSm = 1.0f;
            wetAmount = 0.0f;
            lastTone = makeTone (0.0f, 0.0f, fs);

            cg   = 1.0f - std::exp (-1.0f / (gasTauSec   * fs));
            cb   = 1.0f - std::exp (-1.0f / (brakeTauSec * fs));
            csUp = 1.0f - std::exp (-1.0f / (speedUpTau  * fs));
            csDn = 1.0f - std::exp (-1.0f / (speedDnTau  * fs));
            cw   = 1.0f - std::exp (-1.0f / (wetTauSec   * fs));

            // 缓冲上限：loop 最长不能顶到缓冲底
            maxLoopLen = voices[0].bufferSize() - maxGuardBack - 1024;

            // 用默认 BPM 初始化 loop / 读头位置
            beatLen = (int) std::lround (fs * 60.0 / 120.0);
            loopLen = clamp (beatsPerLoop * beatLen, minLoopLen, maxLoopLen);
            beatCountdown = beatLen;
            for (auto& v : voices) v.setDelay (loopLen);
        }

        void reset()
        {
            for (auto& v : voices) v.reset();
            gasSm = brkSm = 0.0f;
            speedSm = 1.0f;
            wetAmount = 0.0f;
            beatCountdown = beatLen;
            pendingWrap = false;
        }

        float getSpeed() const noexcept { return speedSm; }
        int   getSpliceCount() const noexcept { return voices.empty() ? 0 : voices[0].getSpliceCount(); }
        int   getLoopLen() const noexcept { return loopLen; }
        int   getBeatLen() const noexcept { return beatLen; }
        int   getDelaySamples() const noexcept { return voices.empty() ? 0 : voices[0].getDelaySamples(); }

        // gas / brake ∈ 0..1；bpm 为宿主曲速；samplesToNextBeat 为"当前块起点
        // 到下一个节拍边界的样点数"，< 0 表示无宿主节拍信息（引擎自由运行）。
        void process (float* const* channels, int numChannels, int numSamples,
                      float gasTarget, float brakeTarget,
                      double bpm, int samplesToNextBeat) noexcept
        {
            if (numSamples <= 0 || numChannels <= 0)
                return;

            // 防御：prepareToPlay 尚未调用时 voices 为空，下面多处直接访问
            // voices[0]，这里必须先拦截，避免越界崩溃。
            if (voices.empty())
                return;

            const int nCh = std::min (numChannels, (int) voices.size());

            // ---- 油门+刹车同踩 → 刹车优先（油门视为 0，不再叠加）----
            if (brakeTarget > 1e-4f)
                gasTarget = 0.0f;

            // ---- 每块：更新 loop 长度（跟随 BPM）----
            const double bpmC = clamp ((float) bpm, minBpm, maxBpm);
            beatLen = (int) std::lround (fs * 60.0 / bpmC);
            loopLen = clamp (beatsPerLoop * beatLen, minLoopLen, maxLoopLen);

            // ---- 松手瞬间：读头立即重置 ----
            // 踏板从按下（>0）回弹到 0 的下降沿，直接把读头摆回 loopLen、
            // 速度回到 1.0。这样音高立即恢复正常，不再有"追带"的加速尾巴。
            const bool pedalDown = (gasTarget > 1e-4f) || (brakeTarget > 1e-4f);
            if (wasPedalDown && !pedalDown)
            {
                for (auto& v : voices) v.setDelay (loopLen);
                speedSm = 1.0f;
                gasSm = brkSm = 0.0f;
                pendingWrap = false;
            }
            wasPedalDown = pedalDown;

            if (samplesToNextBeat >= 0)
                beatCountdown = samplesToNextBeat;   // 从宿主对齐节拍相位

            const Tone t0 = lastTone;
            const Tone t1 = makeTone (gasSm, brkSm, fs);
            lastTone = t1;
            const float invN = 1.0f / (float) numSamples;

            for (int i = 0; i < numSamples; ++i)
            {
                gasSm += (gasTarget   - gasSm) * cg;
                brkSm += (brakeTarget - brkSm) * cb;

                // ---- 节拍时钟（块内自由递减；每块起点由宿主对齐）----
                if (--beatCountdown < 0)
                    beatCountdown += beatLen;

                // ---- 目标走带速度（指数映射 = 音高线性）----
                float target = std::exp2 (gasSm * gasOctave + brkSm * brakeOctave);

                // ---- 松踏板后弹簧回位：把延迟拉回 loopLen（= "机器转回正常速度"）----
                const float idle = 1.0f - smoothstep01 (std::max (gasSm, brkSm) * 5.0f);
                if (idle > 0.0f)
                {
                    const float errSec = (float) (voices[0].getDelaySamples() - loopLen) / fs;
                    target += clamp (errSec * 5.0f, -0.25f, 0.28f) * idle;
                }
                target = clamp (target, minSpeed, maxSpeed + 0.30f);
                speedSm += (target - speedSm) * (target > speedSm ? csUp : csDn);

                // ---- 加速回卷：调度在"下一个节拍边界"，回卷量 = loopLen（整拍）----
                // 只在真正踩油门（activity>0）时调度；松手追带（speed>1 但 activity=0）
                // 目标是回位到 loopLen，不会撞写头，不需这里处理。
                const float activity = std::max (gasSm, brkSm);
                if (speedSm > 1.0f && activity > 1e-4f && !voices[0].isFading())
                {
                    const int   d = voices[0].getDelaySamples();
                    const float crossIn = (float) (d - minGuard) / (speedSm - 1.0f);  // 撞写头前的样点数
                    if (!pendingWrap && crossIn <= (float) beatLen)
                    {
                        pendingWrap = true;
                        pendingWrapWait = beatCountdown;
                    }
                }
                if (pendingWrap)
                {
                    if (--pendingWrapWait <= 0)
                    {
                        for (auto& v : voices) v.requestSpliceTo (loopLen);
                        pendingWrap = false;
                    }
                }

                // ---- 旁通 ↔ 走带 交叉淡化 ----
                // 静止且"已回位"（speed≈1、delay≈loopLen）→ 淡出到纯旁通（0 延迟）。
                const bool settled = std::fabs (speedSm - 1.0f) < 0.002f
                                     && std::abs (voices[0].getDelaySamples() - loopLen) < 16;
                const float wetTarget = (activity > 1e-4f || !settled) ? 1.0f : 0.0f;
                wetAmount += (wetTarget - wetAmount) * cw;
                const float wetGain = wetAmount;
                const float dryGain = std::sqrt (std::max (0.0f, 1.0f - wetAmount * wetAmount));

                // ---- 停止闸门：磁带几乎停转时淡出到静音 ----
                float gate = (speedSm - gateShutSpeed) / (gateOpenSpeed - gateShutSpeed);
                gate = clamp (gate, 0.0f, 1.0f);
                gate = gate * gate * (3.0f - 2.0f * gate);

                // wow/flutter 深度由踏板驱动（留常数项会让静止不再透明）
                const float wowDepth  = 0.0550f * brkSm + 0.0140f * gasSm;
                const float flutDepth = 0.0130f * brkSm + 0.0040f * gasSm;

                const Tone c = lerpTone (t0, t1, (float) (i + 1) * invN);

                for (int ch = 0; ch < nCh; ++ch)
                {
                    const float x = channels[ch][i];

                    if (wetGain < 1e-4f)
                    {
                        // 完全旁通：只维护读头（空转保持 delay），信号直通
                        channels[ch][i] = voices[(size_t) ch].processBypass (x, speedSm);
                    }
                    else
                    {
                        const float wet = voices[(size_t) ch].processSample (x, speedSm, gate, wowDepth, flutDepth, c);
                        channels[ch][i] = wet * wetGain + x * dryGain;
                    }
                }
            }

            // 多余声道静音，避免残留垃圾
            for (int ch = nCh; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                    channels[ch][i] = 0.0f;
        }

    private:
        std::vector<Voice> voices;
        Tone  lastTone;
        float fs = 48000.0f;
        float gasSm = 0.0f, brkSm = 0.0f, speedSm = 1.0f;
        float wetAmount = 0.0f;
        float cg = 0.1f, cb = 0.1f, csUp = 0.1f, csDn = 0.1f, cw = 0.1f;

        int   beatLen = 0, loopLen = 0, maxLoopLen = 0, beatCountdown = 0;
        bool  pendingWrap = false;
        int   pendingWrapWait = 0;
        bool  wasPedalDown = false;   // 上一块踏板是否按下（用于检测"松手"下降沿）
    };

} // namespace tape
