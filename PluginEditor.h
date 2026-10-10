#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PedalAnimator.h"
#include "SynthwaveScene.h"
#include "shared/IisaacTelemetry.h"

#include <memory>

// ============================================================
//  Break 编辑器（第一阶段：文生图 UI 链路验证 - 双踏板）
//
//  - 底图：ui_plate（双空腔凹槽，保留各自静态铰链/转轴 + 刹车 + 外壳）
//  - 叠加层：油门/刹车踏板精灵按各自力度做单应插值变形
//  - 交互：在对应凹槽按住向下拖动加力、向上减力，松手回弹；
//          两踏板力度独立，可同时处于不同力度
//  - 60Hz 单一 Timer 统一驱动动画（与 Organic Chemistry 同一约定）
//  - 读数：seg7 程序绘制七段数码管，力度→牛顿线性映射
//          （刹车 0-2000N / 油门 0-1000N），覆盖底图烧录数字
//  - 窗口：base 1168x864 设计稿坐标；默认半尺寸打开，右下角拖动
//          等比缩放（同 CRTLoss：constrainer 锁宽高比 + 全局 scale）。
//          一切绘制/命中/repaint 都在 base 坐标计算，与屏幕坐标
//          之间用 getScale() 换算
// ============================================================
class BreakAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer,
                                  private juce::AudioProcessorParameter::Listener
{
public:
    explicit BreakAudioProcessorEditor (BreakAudioProcessor&);
    ~BreakAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr float dragPixelsPerForce = 260.0f;   // base 坐标拖 260px = 满力度

private:
    void timerCallback() override;

    // 宿主自动化 / 参数变化 → 同步踏板动画（油门/刹车旋钮也能驱动踏板）
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    // 屏幕坐标 <-> base 坐标（等比缩放，宽高共享一个 scale）
    float getScale() const noexcept { return (float) getWidth() / (float) breakcal::uiWidth; }
    juce::Rectangle<int> toScreen (juce::Rectangle<int> baseRect) const;

    // 单侧踏板的交互与渲染状态
    struct PedalState
    {
        PedalAnimator animator;      // 踏板面层
        PedalAnimator armAnimator;   // 臂层（吊挂式刹车用；油门无臂，精灵保持 null）
        juce::Image overlay, armOverlay;
        juce::Point<int> overlayPos, armOverlayPos;
        // 接触阴影（面底边压入凹槽的软黑影，随力度增强；f≈0 时空图）
        juce::Image shadowImage;
        juce::Point<int> shadowPos;
        float force = 0.0f;
        float targetForce = 0.0f;
        float lastRenderedForce = -1.0f;
        // 编辑器正在主动驱动本踏板（拖动中 / 松手回弹中）。只有此时 timer 才
        // 写回参数；宿主自动化期间为 false，避免与音频线程的参数写入竞态覆盖。
        bool  editorDriving = false;
        // 每踏板独立的重绘区域（含臂+面+阴影的并集，base 坐标）。绝不能
        // 共享：两踏板交替运动时，共享变量会被对方覆盖，repaint 漏掉
        // 自己上一帧的位置 → 偶发残影。
        juce::Rectangle<int> lastPaintBounds;

        PedalState (const breakcal::PedalCalibration& faceCal,
                    const breakcal::PedalCalibration& armCal)
            : animator (faceCal), armAnimator (armCal) {}
    };

    // 宿主参数 → 踏板状态（去抖 + 拖动优先已在 parameterValueChanged 里处理）
    void applyHostForce (PedalState& ps, float newValue);

    BreakAudioProcessor& processor;
    PedalState gas, brake;

    juce::Image plateImage;      // 未锁定底图
    juce::Image switchOnImage;   // 拨杆开启态小块贴图（仅开关区域，减小打包体积）
    bool lockPedals = false;     // 拨杆：锁定踏板（松手不自动回弹）

    juce::ComponentBoundsConstrainer resizeConstrainer;   // 锁定 base 宽高比

    bool dragging = false;
    bool draggingBrake = false;          // false = 拖油门
    float dragStartY = 0.0f;             // base 坐标
    float dragStartForce = 0.0f;

    int lastBrakeN = -1;                 // 读数缓存：变化才 repaint 对应屏区
    int lastGasN = -1;

    // 顶部小屏 synthwave 场景：beatPhase 按宿主 BPM 连续累加（拍），
    // BPM 变化时速度即时跟随、相位不跳变；displayBpm 做轻平滑防突变抖动
    synthwave::SceneState scene;
    double displayBpm = 120.0;
    double lastSceneTime = 0.0;          // 墙钟秒（dt 计算）

    // 遥测：GUI 构造完成后才启动（每日至多一次 ping，失败静默）；析构先收。
    std::unique_ptr<iisaac::telemetry::Session> telemetrySession;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakAudioProcessorEditor)
};
