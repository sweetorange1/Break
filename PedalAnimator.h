#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

// ============================================================
//  踏板动画标定常量
//
//  所有坐标为 1168x864 全图 UI 像素。油门标定与 outputs/tools/make_assets.py
//  对应；刹车标定与 outputs/tools/make_brake_assets.py 对应。
//  改这里必须同步改脚本，反之亦然。
// ============================================================
namespace breakcal
{
    constexpr int uiWidth  = 1168;
    constexpr int uiHeight = 864;

    // 单块踏板的标定：REST/FULL 姿态踏板面外框四角（TL, TR, BR, BL）
    // 与精灵裁剪原点。REST/FULL 之间用 4 点单应插值。
    struct PedalCalibration
    {
        float restQuad[4][2];
        float fullQuad[4][2];
        int   spriteX;
        int   spriteY;
    };

    // 油门：风琴式（底部铰链）。底边 y=745 两姿态共用（铰链固定）。
    inline constexpr PedalCalibration gasCal
    {
        { { 733.0f, 444.0f }, { 851.0f, 441.0f }, { 885.0f, 745.0f }, { 738.0f, 745.0f } },
        { { 735.0f, 545.0f }, { 852.0f, 544.0f }, { 885.0f, 745.0f }, { 738.0f, 745.0f } },
        723, 434
    };

    // 刹车：吊挂式（顶部转轴）。标定来源：make_brake_assets.py
    // （亮段成对识别 + 对角极值，REST/FULL 叠加目检贴合包边外框）。
    inline constexpr PedalCalibration brakeCal
    {
        { { 190.0f, 610.0f }, { 519.0f, 601.0f }, { 528.0f, 729.0f }, { 183.0f, 735.0f } },
        { { 228.0f, 619.0f }, { 485.0f, 616.0f }, { 470.0f, 714.0f }, { 227.0f, 712.0f } },
        173, 591
    };

    // 刹车臂（连杆）：吊挂式运动组件（转轴→面顶的金属带）。空腔图把它
    // 扣掉了，必须独立成层。臂顶两角在两姿态钉死（转轴不动）；臂底两角
    // 藏入面顶后，被面覆盖形成无缝连接。渲染顺序：臂先、面后。
    // 透视方向（踩下 = 远离使用者 → 变小）：FULL 底边由 REST 的 52px
    // 收窄到 ~38px（收窄比 ~73%，与踏板面的 78%/70% 同档——用户反馈
    // 收窄到 28px 时臂比面变形剧烈，削弱后两者运动统一），左移仅 8px
    // 跟随面顶中心。标定来源：make_arm_sprite.py，改必须同步。
    inline constexpr PedalCalibration brakeArmCal
    {
        { { 352.0f, 428.0f }, { 408.0f, 426.0f }, { 390.0f, 614.0f }, { 338.0f, 616.0f } },
        { { 352.0f, 428.0f }, { 408.0f, 426.0f }, { 375.0f, 623.0f }, { 337.0f, 625.0f } },
        300, 416
    };

    // 施力热区（正面凹槽）
    inline juce::Rectangle<float> gasHitArea()   { return { 655.0f, 395.0f, 345.0f, 395.0f }; }  // x 655..1000
    inline juce::Rectangle<float> brakeHitArea() { return { 140.0f, 400.0f, 405.0f, 390.0f }; }  // x 140..545

    // 凹槽裁剪框（接触阴影只画在腔内不越框；与 make_brake_assets.py 的
    // GAS_RECESS / BRAKE_RECESS 相同数值）
    inline juce::Rectangle<float> gasRecess()   { return { 555.0f, 420.0f, 465.0f, 400.0f }; }  // 555,420..1020,820
    inline juce::Rectangle<float> brakeRecess() { return { 135.0f, 425.0f, 415.0f, 390.0f }; }  // 135,425..550,815

    // 力度→牛顿映射（显示用线性标定；真实硬件映射未定，勿当物理量）
    inline constexpr float brakeForceMaxN = 2000.0f;
    inline constexpr float gasForceMaxN   = 1000.0f;

    // 双数码屏玻璃区（程序绘制读数的覆盖区；烧录的 000/127 会被玻璃底
    // 色矩形盖掉。来源：_display_bbox.py 暗区检测 + 目检微调）
    inline juce::Rectangle<float> brakeDisplay() { return { 250.0f, 258.0f, 244.0f, 66.0f }; }  // 250,258..494,324
    inline juce::Rectangle<float> gasDisplay()   { return { 662.0f, 258.0f, 244.0f, 66.0f }; }  // 662,258..906,324

    // 锁定拨杆（位于两块数码屏之间）：点击切换"锁定踏板"模式。
    // 蒙版需覆盖整根拨杆 + 指示灯（窄而高），太宽会露锁定时其它 AI 差异。
    inline juce::Rectangle<float> lockSwitchHitArea() { return { 536.0f, 210.0f, 96.0f, 150.0f }; }  // 536,210..632,360
}

// ============================================================
//  PedalAnimator - 踏板单应插值渲染
//
//  模型：踏板面是平面，REST/FULL 两姿态之间用 4 点单应矩阵联系。
//  任意力度 f 的姿态 = 四角线性插值后的中间四边形；
//  渲染 = 求 H(rest->cur) 的逆映射，对精灵逐像素双线性采样。
//  f=0 与 f=1 精确复现两端姿态，中间帧无漂移。
//  油门/刹车各持一个实例（不同标定与精灵）。
// ============================================================
class PedalAnimator
{
public:
    explicit PedalAnimator (const breakcal::PedalCalibration& c) : cal (c) {}

    /** 注入 REST 姿态抠出的 RGBA 精灵（含 alpha 蒙版）。 */
    void setSprite (juce::Image spriteImage) { sprite = std::move (spriteImage); }

    /** 踩下暗化系数 0..1：f=1 时精灵亮度乘 (1-darkening)。0 = 关闭。
     *  预乘 ARGB 下只缩 RGB，语义正确（预乘值等比变暗）。 */
    void setDarkening (float amount01) { darkening = juce::jlimit (0.0f, 1.0f, amount01); }

    /** 当前力度的面四角插值（TL,TR,BR,BL），与 render 同一套插值，
     *  供编辑器定位接触阴影。 */
    void currentQuad (float force01, juce::Point<float> outQuad[4]) const;

    /** 计算指定力度的叠加层。outOverlay 为 ARGB；绘制位置为 outPos。 */
    void render (float force01, juce::Image& outOverlay, juce::Point<int>& outPos);

private:
    breakcal::PedalCalibration cal;
    juce::Image sprite;
    float darkening = 0.0f;

    JUCE_DECLARE_NON_COPYABLE (PedalAnimator)
};
