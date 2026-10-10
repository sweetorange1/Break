// 无头诊断：跑与插件完全相同的 PedalAnimator 代码 + BinaryData 资产，
// 输出油门/刹车叠加层 PNG 与不透明像素统计，定位渲染根因 / 回归验证。
#include <cstdio>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include "BinaryData.h"
#include "../PedalAnimator.h"
#include "../SegDisplay.h"
#include "../Labels.h"
#include "../SynthwaveScene.h"

static juce::Image loadFromBinary (const char* data, int size, const char* name)
{
    auto img = juce::ImageCache::getFromMemory (data, size);
    std::printf ("%s: %s %dx%d\n", name,
                 img.isNull() ? "NULL!!" : "OK",
                 img.getWidth(), img.getHeight());
    return img;
}

static int runPedal (const char* tag, PedalAnimator& animator)
{
    for (float f : { 0.0f, 0.5f, 1.0f })
    {
        juce::Image ov;
        juce::Point<int> pos;
        animator.render (f, ov, pos);

        if (ov.isNull())
        {
            std::printf ("%s f=%.2f overlay NULL!!\n", tag, f);
            return 1;
        }

        int opaque = 0, total = ov.getWidth() * ov.getHeight();
        {
            juce::Image::BitmapData d (ov, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < ov.getHeight(); ++y)
                for (int x = 0; x < ov.getWidth(); ++x)
                    if (d.getPixelColour (x, y).getAlpha() > 100)
                        ++opaque;
        }
        std::printf ("%s f=%.2f overlay %dx%d at (%d,%d) opaque=%d / %d\n",
                     tag, f, ov.getWidth(), ov.getHeight(), pos.x, pos.y, opaque, total);

        auto out = juce::File::getCurrentWorkingDirectory()
                       .getChildFile (juce::String (tag) + "_f"
                                      + juce::String (juce::roundToInt (f * 100.0f)) + ".png");
        juce::PNGImageFormat png;
        juce::FileOutputStream fo (out);
        fo.setPosition (0);
        png.writeImageToStream (ov, fo);
        fo.flush();
    }
    return 0;
}

int main()
{
    auto plate  = loadFromBinary (BinaryData::ui_plate_unlocked_png,
                                  BinaryData::ui_plate_unlocked_pngSize, "ui_plate_unlocked");
    auto gasSpr = loadFromBinary (BinaryData::gas_sprite_png,
                                  BinaryData::gas_sprite_pngSize, "gas_sprite");
    auto brakeSpr = loadFromBinary (BinaryData::brake_sprite_png,
                                    BinaryData::brake_sprite_pngSize, "brake_sprite");
    auto armSpr = loadFromBinary (BinaryData::arm_sprite_png,
                                  BinaryData::arm_sprite_pngSize, "arm_sprite");
    if (gasSpr.isNull() || brakeSpr.isNull() || armSpr.isNull())
    {
        std::printf ("FATAL: sprite null\n");
        return 1;
    }

    PedalAnimator gas (breakcal::gasCal);
    gas.setSprite (gasSpr);
    PedalAnimator brake (breakcal::brakeCal);
    brake.setSprite (brakeSpr);
    PedalAnimator brakeArm (breakcal::brakeArmCal);
    brakeArm.setSprite (armSpr);

    int rc = runPedal ("gas", gas);
    rc |= runPedal ("brake", brake);
    rc |= runPedal ("brake_arm", brakeArm);

    // ---- 整帧合成验证：底图 + 七段读数 + 双踏板动画（臂先、面后）----
    struct Case { float brake, gas; };
    const Case cases[3] = { { 0.0f, 0.0f }, { 0.5f, 0.25f }, { 1.0f, 1.0f } };
    for (int i = 0; i < 3; ++i)
    {
        auto frame = juce::Image (juce::Image::RGB,
                                  breakcal::uiWidth, breakcal::uiHeight, true);
        {
            juce::Graphics g (frame);
            g.drawImageAt (plate, 0, 0);

            // 顶部铭牌文字（与编辑器同一函数；版本串测试环境手动给）
            labels::drawTopLabels (g, "0.1.0");

            const seg7::Style st;
            seg7::drawDisplay (g, breakcal::brakeDisplay(),
                               (int) std::round (cases[i].brake * breakcal::brakeForceMaxN),
                               4, "N", st);
            seg7::drawDisplay (g, breakcal::gasDisplay(),
                               (int) std::round (cases[i].gas * breakcal::gasForceMaxN),
                               4, "N", st);

            juce::Image ov;
            juce::Point<int> pos;
            brakeArm.render (cases[i].brake, ov, pos);
            if (! ov.isNull()) g.drawImageAt (ov, pos.x, pos.y);
            brake.render (cases[i].brake, ov, pos);
            if (! ov.isNull()) g.drawImageAt (ov, pos.x, pos.y);
            gas.render (cases[i].gas, ov, pos);
            if (! ov.isNull()) g.drawImageAt (ov, pos.x, pos.y);
        }
        auto out = juce::File::getCurrentWorkingDirectory()
                       .getChildFile (juce::String ("frame_check_")
                                      + juce::String (i) + ".png");
        juce::PNGImageFormat png;
        juce::FileOutputStream fo (out);
        fo.setPosition (0);
        png.writeImageToStream (frame, fo);
        fo.flush();
    }
    std::printf ("frame_check_0/1/2.png written\n");

    // ---- synthwave 场景验证：三个相位（网格流动 / 条纹滚动 / 循环连续性）----
    for (int i = 0; i < 3; ++i)
    {
        synthwave::SceneState st;
        st.beatPhase = 3.0 + i * 0.34;      // 从 3.0 起，验证任意相位稳定
        st.wallTime  = 0.37 + i * 0.5;
        st.travel    = 1.5f + i * 1.1f;     // 前进距离错开，验证棕榈树掠过
        const auto wr = synthwave::windowRect().toNearestInt();
        auto frame = juce::Image (juce::Image::RGB, wr.getWidth(), wr.getHeight(), true);
        {
            juce::Graphics g (frame);
            synthwave::drawScene (g, frame.getBounds().toFloat(), st);
        }
        auto out = juce::File::getCurrentWorkingDirectory()
                       .getChildFile (juce::String ("synth_check_") + juce::String (i) + ".png");
        juce::PNGImageFormat png;
        juce::FileOutputStream fo (out);
        fo.setPosition (0);
        png.writeImageToStream (frame, fo);
        fo.flush();
    }
    std::printf ("synth_check_0/1/2.png written\n");
    return rc;
}
