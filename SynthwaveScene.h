#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cmath>

// ============================================================
//  SynthwaveScene —— 顶部小屏的程序化 Outrun 场景（替代 AI 静态图）
//
//  元素：渐变天空 + 闪烁星空 + 巨大条纹落日（缝隙露天空）
//        + 双层线框山脉 + 透视霓虹网格地面（向观察者流动）
//        + 地平线辉光 + CRT 扫描线
//
//  双时间轴：
//    · beatPhase（BPM 拍）：驱动太阳脉动 / 条纹（非前进运动，随宿主曲速）
//    · travel（前进距离，单元格）：驱动网格滚动与棕榈树掠过，由两个踏板
//      决定速度（满刹车=停止，满油门=加速），与 BPM 解绑。
//  相位/距离连续累加 => 速度改变时画面位置无跳变。
//
//  纯绘制函数（与 SegDisplay.h 同一约定）：无成员状态、可无头调用。
// ============================================================
namespace synthwave
{
    // 玻璃渲染区（base 设计稿坐标）。实测底图玻璃窗内沿：(66,50)..(1102,211)
    // （四角 8x 放大目检；用户半缩放实测 516x80@(35,82)×2≈1032x160@(70,50) 互证）
    inline juce::Rectangle<float> windowRect() { return { 66.0f, 50.0f, 1036.0f, 161.0f }; }

    // 玻璃窗微小圆角（base 坐标；半缩放屏显 4px）
    inline constexpr float cornerRadius = 8.0f;

    struct SceneState
    {
        double beatPhase = 0.0;   // 累计拍数（BPM 驱动：太阳脉动/条纹，连续不回绕）
        double wallTime  = 0.0;   // 墙钟秒（星星闪烁等非节奏动画）
        double travel    = 0.0;   // 前进距离（网格单元格，踏板驱动：刹车减速/油门加速）
    };

    // ---- 前进速度（网格单元格 / 秒）：与 BPM 解绑，由两个踏板驱动 ----
    // 满刹车 → 0（停止前进）；满油门 → (1 + gasSpeedGain) 倍基速。
    inline constexpr float baseSpeedCellsPerSec = 1.5f;
    inline constexpr float gasSpeedGain         = 4.0f;   // 满油门 ≈ 5× 基速（加速更明显）
    inline float forwardSpeed (float gas, float brake) noexcept
    {
        return baseSpeedCellsPerSec * (1.0f + gasSpeedGain * gas) * (1.0f - brake);
    }

    // ---- 工具：GLSL 风格确定性 hash（无状态，帧间稳定）----
    inline float frac (float x)  { return x - std::floor (x); }
    inline float hash11 (float n){ return frac (std::sin (n * 127.1f) * 43758.5453f); }

    // 太阳行颜色：t = 0(顶) .. 1(底)，黄 → 橙 → 粉
    inline juce::Colour sunRowColour (float t)
    {
        if (t < 0.45f)
            return juce::Colour (0xffffdf6b).interpolatedWith (juce::Colour (0xffff7a3c), t / 0.45f);
        return juce::Colour (0xffff7a3c).interpolatedWith (juce::Colour (0xffff2e88),
                                                           (t - 0.45f) / 0.55f);
    }

    // 山脊高度（相对地平线向上，px）：两端高、中央 V 谷露出落日 + 细节起伏
    inline float ridgeHeight (float xn, float amp, float seed, float H, float hy)
    {
        const float valley = std::pow (std::abs (xn - 0.5f) * 2.0f, 1.35f);
        const float detail = 0.5f + 0.25f * std::sin (xn * 18.0f + seed)
                                  + 0.30f * std::sin (xn * 41.0f + seed * 2.3f);
        return hy - H * (0.05f + amp * valley * (0.75f + 0.25f * detail));
    }

    inline void drawScene (juce::Graphics& g, juce::Rectangle<float> r, const SceneState& s)
    {
        juce::Graphics::ScopedSaveState ss (g);
        // 圆角玻璃裁剪（跟随底图玻璃窗形状）
        juce::Path clipPath;
        clipPath.addRoundedRectangle (r, cornerRadius);
        g.reduceClipRegion (clipPath);

        const float W = r.getWidth(), H = r.getHeight();
        const float x0  = r.getX();
        const float y0  = r.getY();
        const float bot = r.getBottom();
        const float hy  = y0 + H * 0.60f;          // 地平线
        const float vpX = x0 + W * 0.5f;           // 消失点

        // ---- 1. 天空渐变 + 地面底色 ----
        {
            juce::ColourGradient sky (juce::Colour (0xff150533), x0, y0,
                                      juce::Colour (0xffc4507e), x0, hy, false);
            sky.addColour (0.55, juce::Colour (0xff521a78));
            g.setGradientFill (sky);
            g.fillRect (x0, y0, W, hy - y0);
            g.setColour (juce::Colour (0xff160726));
            g.fillRect (x0, hy, W, bot - hy);
        }

        // ---- 2. 星空（上 60% 天空，各自频率闪烁）----
        for (int i = 0; i < 46; ++i)
        {
            const float h1 = hash11 ((float) i + 1.0f);
            const float h2 = hash11 ((float) i + 51.7f);
            const float sx = x0 + h1 * W;
            const float sy = y0 + h2 * (hy - y0) * 0.62f;
            const float tw = 0.5f + 0.5f * std::sin ((float) s.wallTime * (1.5f + 2.5f * h1)
                                                     + h2 * 6.2832f);
            g.setColour (juce::Colours::white.withAlpha (0.16f + 0.55f * tw * h2));
            g.fillRect (sx, sy, 1.6f, 1.6f);
        }

        // ---- 3. 巨大落日：逐水平行段绘制（条纹行跳过 → 真缝隙露天空）----
        const float sunR  = H * 0.46f;
        const float sunCX = vpX;
        const float sunCY = hy - sunR * 0.30f;     // 顶部完整入画，底部沉入地平线 ~30%
        // 外圈辉光（3 层半透明）
        for (int k = 3; k >= 1; --k)
        {
            const float gr = sunR * (1.0f + 0.06f * (float) k);
            g.setColour (juce::Colour (0xffff8a4c).withAlpha (0.045f));
            g.fillEllipse (sunCX - gr, sunCY - gr, gr * 2.0f, gr * 2.0f);
        }
        // 每拍轻微脉动（幅度 1.5%，视觉克制）
        const float pulse = 1.0f + 0.015f * std::sin ((float) s.beatPhase * 3.14159f);
        const float R = sunR * pulse;
        // 条纹相位：每 2 拍循环下移一次
        const float stripPhase = (float) std::fmod (s.beatPhase * 0.5, 1.0);
        const int rows = (int) (R * 2.0f);
        for (int i = 0; i < rows; ++i)
        {
            const float dy = ((float) i + 0.5f) - R;             // -R..R
            const float dy2 = dy * dy;
            if (dy2 >= R * R) continue;
            const float half = std::sqrt (R * R - dy2);
            const float t = (dy / R + 1.0f) * 0.5f;              // 0 顶 .. 1 底
            // 条纹：太阳中下部（t > 0.40），缝随深度变宽、随相位下移
            if (t > 0.40f)
            {
                const float u = (t - 0.40f) / 0.60f;             // 0..1
                if (frac (u * 5.0f - stripPhase * 5.0f) < 0.16f + 0.45f * u)
                    continue;
            }
            g.setColour (sunRowColour (t));
            g.fillRect (sunCX - half, sunCY + dy - 0.6f, half * 2.0f, 1.3f);
        }

        // ---- 4. 双层线框山脉（远山暗紫 / 近山品红描边 + 辉光）----
        auto drawRidge = [&] (float amp, float seed, juce::Colour fill,
                              juce::Colour edge, float edgeW, float glowA)
        {
            juce::Path p;
            constexpr int segs = 44;
            p.startNewSubPath (x0, ridgeHeight (0.0f, amp, seed, H, hy));
            for (int i = 1; i <= segs; ++i)
            {
                const float xn = (float) i / (float) segs;
                p.lineTo (x0 + xn * W, ridgeHeight (xn, amp, seed, H, hy));
            }
            p.lineTo (x0 + W, bot);
            p.lineTo (x0, bot);
            p.closeSubPath();
            g.setColour (fill);
            g.fillPath (p);
            if (glowA > 0.0f)   // 描边辉光（粗线低 alpha）
            {
                g.setColour (edge.withAlpha (glowA));
                g.strokePath (p, juce::PathStrokeType (edgeW + 2.6f));
            }
            g.setColour (edge);
            g.strokePath (p, juce::PathStrokeType (edgeW));
        };
        drawRidge (0.34f, 1.0f, juce::Colour (0xff2a1048), juce::Colour (0xff5a2a86), 1.0f, 0.0f);
        drawRidge (0.20f, 4.2f, juce::Colour (0xff190a30), juce::Colour (0xffff2ea6), 1.4f, 0.16f);

        // ---- 5. 透视网格地面 ----
        // 纵向放射线（品红，消失点发散）
        g.setColour (juce::Colour (0xffff3ea5).withAlpha (0.45f));
        for (int k = -7; k <= 7; ++k)
        {
            const float xb = vpX + (float) k * W * 0.13f;
            g.drawLine ({ vpX, hy, xb, bot }, 1.1f);
        }
        // 横向滚动线（青，近亮远暗）。z_i = i - scroll 允许为负（越出观察者，
        // 被 clip 裁掉）→ 循环无缝，前进距离连续时无任何跳变
        const float scroll = (float) std::fmod (s.travel, 1.0);
        for (int i = 0; i < 22; ++i)
        {
            const float z = (float) i - scroll;
            if (z < -0.05f) continue;
            const float f = 1.0f / (1.0f + z * 0.30f);           // 透视因子
            const float y  = hy + (bot - hy) * f;
            const float a  = 0.10f + 0.55f * f;
            g.setColour (juce::Colour (0xff2ee6ff).withAlpha (a));
            g.drawLine ({ x0, y, x0 + W, y }, 0.8f + 1.4f * f);
        }

        // ---- 6. 地平线辉光带（品红，上下渐隐）----
        {
            juce::ColourGradient glow (juce::Colour (0xffff4fb0).withAlpha (0.0f),
                                       x0, hy - 12.0f,
                                       juce::Colour (0xffff4fb0).withAlpha (0.0f),
                                       x0, hy + 12.0f, false);
            glow.addColour (0.5, juce::Colour (0xffff4fb0).withAlpha (0.40f));
            g.setGradientFill (glow);
            g.fillRect (x0, hy - 12.0f, W, 24.0f);
        }

        // ---- 6.5 棕榈树剪影：沿轨道两侧（独立随机节奏），从极远处缓慢逼近 ----
        const juce::Colour palmColour (0xff12041f);
        auto drawPalm = [&] (float x, float baseY, float h, float side, float alpha)
        {
            if (h < 3.0f || alpha <= 0.004f)
                return;
            const float topX   = x + side * h * 0.16f;      // 向外倾斜
            const float topY   = baseY - h * 0.70f;
            const float trunkW = h * 0.075f;
            const float frondW = h * 0.05f;

            g.setColour (palmColour.withAlpha (alpha));

            // 树干：微弯、向上略细的锥形剪影
            juce::Path trunk;
            trunk.startNewSubPath (x - trunkW, baseY);
            trunk.quadraticTo (topX - trunkW * 0.6f, baseY - h * 0.38f, topX - trunkW * 0.4f, topY);
            trunk.lineTo (topX + trunkW * 0.4f, topY);
            trunk.quadraticTo (topX + trunkW * 0.6f, baseY - h * 0.38f, x + trunkW, baseY);
            trunk.closeSubPath();
            g.fillPath (trunk);

            // 叶冠：7 片弧形叶从树顶放射（x 方向按 side 镜像）
            const float fronds[7][4] = {
                { -0.66f, -0.28f, -0.34f, -0.62f },   // 左上
                { -0.22f, -0.44f, -0.10f, -0.66f },   // 上左
                {  0.08f, -0.46f,  0.02f, -0.68f },   // 上
                {  0.42f, -0.34f,  0.22f, -0.60f },   // 上右
                {  0.70f, -0.06f,  0.48f, -0.34f },   // 右
                { -0.74f, -0.02f, -0.50f, -0.28f },   // 左
                { -0.30f,  0.12f, -0.16f, -0.06f }    // 左下（下垂）
            };
            juce::Path crown;
            for (const auto& q : fronds)
            {
                crown.startNewSubPath (topX, topY);
                crown.quadraticTo (topX + q[2] * h * side, topY + q[3] * h,
                                   topX + q[0] * h * side, topY + q[1] * h);
            }
            g.strokePath (crown, juce::PathStrokeType (frondW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        };
        {
            const float farZ    = 40.0f;          // 最远可见距离：树从极远处（近地平线、很小）生成
            const float spacing = 11.0f;          // 平均树间距（单元格，翻倍 → 树量减半）
            const float jitter  = spacing * 0.85f; // 间距抖动幅度

            // 每侧独立随机节奏：世界坐标 = k*spacing + hash 抖动。hash 只依赖
            // k 与 side（不随 travel 变化）→ 树连续移动无跳变；左右不同种子
            // → 两侧不同相，不再并排同时出现。
            auto worldZ = [&] (int k, int side) -> float
            {
                const float j = hash11 ((float) k * 7.13f + (float) side * 13.7f + 3.1f) - 0.5f;
                return (float) k * spacing + j * jitter;
            };

            for (int side : { -1, 1 })
            {
                // worldZ 随 k 严格递增（抖动幅度 < spacing），粗估 k 区间（±2 余量）
                const int kMin = (int) std::floor (s.travel / spacing) - 2;
                const int kMax = (int) std::floor ((s.travel + farZ) / spacing) + 2;

                for (int k = kMin; k <= kMax; ++k)
                {
                    const float z = worldZ (k, side) - (float) s.travel;
                    if (z < -0.4f || z > farZ)
                        continue;
                    const float f = 1.0f / (1.0f + z * 0.30f);      // 与网格同一透视
                    // 远处淡入：最远 30% 距离内从透明渐显，避免"突然出现"
                    const float fadeIn = juce::jlimit (0.0f, 1.0f, (farZ - z) / (farZ * 0.30f));
                    const float baseY = hy + (bot - hy) * f;
                    const float x     = vpX + (float) side * W * 0.46f * f;
                    const float h     = H * 2.1f * f;               // 近处超高，顶端被窗口裁掉
                    drawPalm (x, baseY, h, (float) side, fadeIn);
                }
            }
        }

        // ---- 7. CRT 扫描线（强化屏幕质感）----
        g.setColour (juce::Colours::black.withAlpha (0.055f));
        for (float sy = y0 + 1.0f; sy < bot; sy += 3.0f)
            g.fillRect (x0, sy, W, 1.0f);
    }
}
