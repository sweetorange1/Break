#pragma once

#include <cmath>
#include <juce_graphics/juce_graphics.h>

// ============================================================
//  seg7 - 七段数码管读数绘制
//
//  移植自 outputs/tools/pedal_anim.py 的 draw_seven_seg（尖角八边形
//  段 + 段间隙 gg），含未点亮幽灵段，与底图七段管风格一致。
//  布局：numDigits 位右对齐补零 + 右侧单位字符，整体在玻璃区内居中，
//  单位字底与数字底对齐。玻璃底色矩形负责覆盖底图烧录的 000/127。
//  颜色采样自 ui_plate：点亮段 RGB(63,218,241)，玻璃/幽灵段 RGB(34,13,53)。
// ============================================================
namespace seg7
{
    // 0-9 点亮段表（a=顶 b=右上 c=右下 d=底 e=左下 f=左上 g=中），
    // 与 Python DIGITS 完全一致
    inline bool segOn (int digit, char seg)
    {
        static constexpr const char* table[10] = {
            "abcdef", "bc",     "abdeg",  "abcdg",  "bcfg",
            "acdfg",  "acdefg", "abc",    "abcdefg", "abcdfg"
        };
        if (digit < 0 || digit > 9)
            return false;
        for (const char* p = table[digit]; *p != '\0'; ++p)
            if (*p == seg)
                return true;
        return false;
    }

    inline void addPoly (juce::Path& p, const juce::Point<float>* pts, int n)
    {
        p.startNewSubPath (pts[0]);
        for (int i = 1; i < n; ++i)
            p.lineTo (pts[i]);
        p.closeSubPath();
    }

    // 水平段（左右尖角八边形）
    inline void hSeg (juce::Path& p, float xa, float xb, float y, float t)
    {
        const float c = t * 0.32f;
        const juce::Point<float> pts[8] = {
            { xa + c, y },     { xb - c, y },     { xb, y + c },     { xb, y + t - c },
            { xb - c, y + t }, { xa + c, y + t }, { xa, y + t - c }, { xa, y + c }
        };
        addPoly (p, pts, 8);
    }

    // 垂直段（上下尖角八边形）
    inline void vSeg (juce::Path& p, float x, float ya, float yb, float t)
    {
        const float c = t * 0.32f;
        const juce::Point<float> pts[8] = {
            { x, ya + c },     { x + c, ya },     { x + t - c, ya }, { x + t, ya + c },
            { x + t, yb - c }, { x + t - c, yb }, { x + c, yb },     { x, yb - c }
        };
        addPoly (p, pts, 8);
    }

    struct Style
    {
        juce::Colour glassColour { 0xff1e0b30 };   // 玻璃底（盖掉底图烧录读数）
        juce::Colour offColour   { 0xff2a1440 };   // 幽灵熄灭段
        juce::Colour litColour   { 0xff50dcf2 };   // 点亮段
        float heightRatio     = 0.62f;             // 数字高 / 玻璃高
        float digitAspect     = 0.62f;             // 单字宽 / 高
        float unitHeightRatio = 0.52f;             // 单位字高 / 数字高
    };

    /** 在玻璃区内画读数：numDigits 位（补零）+ 单位字符。 */
    inline void drawDisplay (juce::Graphics& g, juce::Rectangle<float> glass,
                             int value, int numDigits,
                             const juce::String& unit = "N",
                             const Style& style = {})
    {
        // 1. 玻璃底：覆盖底图烧录的 000 / 127
        g.setColour (style.glassColour);
        g.fillRoundedRectangle (glass, 3.0f);

        int maxVal = 1;
        for (int i = 0; i < numDigits; ++i)
            maxVal *= 10;
        value = juce::jlimit (0, maxVal - 1, value);

        const float h    = glass.getHeight() * style.heightRatio;
        const float w    = h * style.digitAspect;
        const float gap  = w * 0.16f;
        const float digitsW = numDigits * w + (numDigits - 1) * gap;

        // 单位字符（粗体无衬线），底部与数字底对齐
        const float unitH = h * style.unitHeightRatio;
        const juce::Font unitFont (juce::FontOptions (unitH, juce::Font::bold));
        const float unitW   = unitFont.getStringWidth (unit);
        const float unitGap = glass.getWidth() * 0.05f;

        const float totalW = digitsW + unitGap + unitW;
        const float x0 = glass.getX() + (glass.getWidth() - totalW) * 0.5f;
        const float y0 = glass.getY() + (glass.getHeight() - h) * 0.5f;
        const float y1 = y0 + h;

        auto text = juce::String (value);
        while (text.length() < numDigits)
            text = "0" + text;

        // 2. 逐位生成段路径，按点亮表分流到 lit/off 两个 Path
        const float t = h * 0.13f;
        const float gg = t * 0.55f;
        const float midY = y0 + h * 0.5f;

        juce::Path litPath, offPath;
        const char segIds[7] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g' };

        for (int i = 0; i < numDigits; ++i)
        {
            const float cx0 = x0 + i * (w + gap);
            const float cx1 = cx0 + w;
            const int digit = (int) (text[i] - '0');

            juce::Path segs[7];
            hSeg (segs[0], cx0 + gg, cx1 - gg, y0, t);
            vSeg (segs[1], cx1 - t, y0 + gg, midY - gg, t);
            vSeg (segs[2], cx1 - t, midY + gg, y1 - gg, t);
            hSeg (segs[3], cx0 + gg, cx1 - gg, y1 - t, t);
            vSeg (segs[4], cx0, midY + gg, y1 - gg, t);
            vSeg (segs[5], cx0, y0 + gg, midY - gg, t);
            hSeg (segs[6], cx0 + gg, cx1 - gg, midY - t * 0.5f, t);

            for (int s = 0; s < 7; ++s)
                (segOn (digit, segIds[s]) ? litPath : offPath).addPath (segs[s]);
        }

        // 3. 幽灵段 → 辉光描边 → 点亮段 → 单位字符
        g.setColour (style.offColour);
        g.fillPath (offPath);
        g.setColour (style.litColour.withAlpha (0.22f));
        g.strokePath (litPath, juce::PathStrokeType (t * 0.9f));
        g.setColour (style.litColour);
        g.fillPath (litPath);

        g.setFont (unitFont);
        g.drawText (unit,
                    (int) (x0 + digitsW + unitGap), (int) (y1 - unitH),
                    (int) std::ceil (unitW) + 4, (int) std::ceil (unitH),
                    juce::Justification::centredLeft);
    }
}
