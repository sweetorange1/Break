#pragma once

#include <juce_graphics/juce_graphics.h>
#include "BinaryData.h"

// 顶部两块紫色铭牌的文字层。左上厂牌（iisaacbeats.cn）已直接烧录进新版
// 底图（ui_plate_unlocked/locked.png），不再程序绘制；这里只绘制右上角
// 版本号（自动跟随 CMake 的 VERSION，经 JucePlugin_VersionString，改版本
// 无需重新生成图片资产）。brandRect() 仅用于命中检测（点击跳转官网）。
// 字体用官网同款 VT323（像素终端字），与原 AI 烧录字的等宽像素感一致。
namespace labels
{
    // 铭牌可绘制区（抹字多边形内缩后的安全框，base 坐标）
    inline juce::Rectangle<int> brandRect()   { return { 70, 15, 156, 28 }; }   // 左铭牌
    inline juce::Rectangle<int> versionRect() { return { 912, 20, 178, 20 }; }  // 右铭牌

    inline juce::FontOptions fontOptions (float size)
    {
        static auto typeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::vt323latin_ttf, BinaryData::vt323latin_ttfSize);
        return juce::FontOptions (typeface).withHeight (size);
    }

    /** 在底图之上绘制铭牌文字。versionText 由调用方传入
        （编辑器传 JucePlugin_VersionString，无头测试传任意串）。
        左上角厂牌文字已直接烧录进新底图，这里只绘制右上角版本号。 */
    inline void drawTopLabels (juce::Graphics& g, const juce::String& versionText)
    {
        g.setColour (juce::Colour (0xffb899d4));
        g.setFont (fontOptions (24.0f));
        g.drawText ("v" + versionText, versionRect(), juce::Justification::centred);
    }
}
