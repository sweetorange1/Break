#include "PluginEditor.h"
#include "SegDisplay.h"
#include "Labels.h"

#include <cmath>

namespace
{
    // 接触阴影：踏板面底边正下方的软椭圆黑影——面压进凹槽、把接触线
    // 附近压暗。力度越大影子越深、越往下沉（中心相对底边下移）。
    // 同心椭圆笔刷（外→内逐层加深），中心累计 alpha ≈ 0.5*f。
    // 输出预乘 ARGB 小图；绘制时必须先于臂/面层（影在物体下面），
    // 且裁剪到凹槽框内（breakcal::*Recess），避免越到金属外框上。
    juce::Image makeContactShadow (const juce::Point<float> q[4], float f,
                                   juce::Point<int>& outPos)
    {
        const juce::Line<float> bottom (q[3].x, q[3].y, q[2].x, q[2].y);   // BL -> BR
        const auto mid = bottom.getPointAlongLineProportionally (0.5f);
        const float w  = q[2].getDistanceFrom (q[3]);
        const float rx = w * 0.58f;
        const float ry = 10.0f + 12.0f * f;
        const float cy = mid.y + 3.0f + 5.0f * f;

        const int x0 = (int) std::floor (mid.x - rx) - 1;
        const int y0 = (int) std::floor (cy - ry) - 1;
        const int bw = (int) std::ceil (2.0f * rx) + 3;
        const int bh = (int) std::ceil (2.0f * ry) + 3;
        if (bw <= 0 || bh <= 0)
            return {};
        outPos = { x0, y0 };

        juce::Image img (juce::Image::ARGB, bw, bh, true);
        juce::Graphics g (img);
        constexpr int rings = 6;
        for (int i = rings; i >= 1; --i)
        {
            const float t = (float) i / (float) rings;   // 1 = 最外圈
            const float s = 0.30f + 0.70f * t;           // 半径比例
            g.setColour (juce::Colours::black.withAlpha (0.09f * f));
            g.fillEllipse (mid.x - rx * s - (float) x0, cy - ry * s - (float) y0,
                           2.0f * rx * s, 2.0f * ry * s);
        }
        return img;
    }
}

BreakAudioProcessorEditor::BreakAudioProcessorEditor (BreakAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      gas (breakcal::gasCal, breakcal::brakeArmCal),      // 油门无臂：armAnimator 精灵保持 null
      brake (breakcal::brakeCal, breakcal::brakeArmCal)
{
    plateImage = juce::ImageCache::getFromMemory (BinaryData::ui_plate_unlocked_png,
                                                  BinaryData::ui_plate_unlocked_pngSize);
    switchOnImage = juce::ImageCache::getFromMemory (BinaryData::switch_on_png,
                                                     BinaryData::switch_on_pngSize);
    gas.animator.setSprite (juce::ImageCache::getFromMemory (BinaryData::gas_sprite_png,
                                                             BinaryData::gas_sprite_pngSize));
    brake.animator.setSprite (juce::ImageCache::getFromMemory (BinaryData::brake_sprite_png,
                                                               BinaryData::brake_sprite_pngSize));
    brake.armAnimator.setSprite (juce::ImageCache::getFromMemory (BinaryData::arm_sprite_png,
                                                                  BinaryData::arm_sprite_pngSize));

    // 踩下暗化：踏板/连杆远离光源方向 → 轻微压暗，强化「压下去」的层次
    gas.animator.setDarkening (0.10f);
    brake.animator.setDarkening (0.16f);
    brake.armAnimator.setDarkening (0.16f);

    // 等比缩放（同 CRTLoss）：constrainer 锁 base 宽高比；右下角 handle
    // 由 setResizable(true, true) 提供。范围 1/4 .. 1:1（base 分辨率上限，
    // 再大会插值发虚），默认 3/4 尺寸打开。
    setResizable (true, true);
    resizeConstrainer.setFixedAspectRatio ((float) breakcal::uiWidth / (float) breakcal::uiHeight);
    resizeConstrainer.setSizeLimits (breakcal::uiWidth / 4, breakcal::uiHeight / 4,
                                     breakcal::uiWidth, breakcal::uiHeight);
    setConstrainer (&resizeConstrainer);
    setSize (breakcal::uiWidth * 3 / 4, breakcal::uiHeight * 3 / 4);

    // 监听参数变化：宿主自动化 / 旋钮 → 踏板动画跟随
    if (processor.gasParam != nullptr)
        processor.gasParam->addListener (this);
    if (processor.brakeParam != nullptr)
        processor.brakeParam->addListener (this);

    // 打开编辑器时同步当前参数值（宿主可能已恢复 preset / 自动化）
    gas.force   = gas.targetForce   = processor.gasParam   != nullptr ? processor.gasParam->get()   : 0.0f;
    brake.force = brake.targetForce = processor.brakeParam != nullptr ? processor.brakeParam->get() : 0.0f;

    lastSceneTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);

    // GUI 构造完成后才启动遥测（每日至多一次 ping，失败静默）
    telemetrySession = std::make_unique<iisaac::telemetry::Session> (
        iisaac::telemetry::forPlugin ("break", JucePlugin_VersionString,
                                      JucePlugin_VersionString, processor.wrapperType));
}

BreakAudioProcessorEditor::~BreakAudioProcessorEditor()
{
    // 先收遥测：Session 析构要在消息线程等待工作线程退出。
    telemetrySession.reset();

    if (processor.gasParam != nullptr)
        processor.gasParam->removeListener (this);
    if (processor.brakeParam != nullptr)
        processor.brakeParam->removeListener (this);
}

void BreakAudioProcessorEditor::parameterValueChanged (int parameterIndex, float newValue)
{
    // 判断是油门还是刹车参数（用 getParameters() 索引定位，避免硬编码序号）
    const auto& params = processor.getParameters();
    if (parameterIndex < 0 || parameterIndex >= params.size())
        return;

    const auto* p = params.getUnchecked (parameterIndex);
    if (p == processor.gasParam)
    {
        if (! (dragging && ! draggingBrake))   // 正在拖油门 → 忽略宿主，用户优先
            applyHostForce (gas, newValue);
    }
    else if (p == processor.brakeParam)
    {
        if (! (dragging && draggingBrake))     // 正在拖刹车 → 忽略宿主，用户优先
            applyHostForce (brake, newValue);
    }
}

void BreakAudioProcessorEditor::parameterGestureChanged (int, bool)
{
    // 不需要处理手势开始/结束（本插件踏板交互未走 gesture 协议）
}

void BreakAudioProcessorEditor::applyHostForce (PedalState& ps, float newValue)
{
    // 去抖：编辑器自己写回的值（与当前 force 几乎一致）忽略，避免回环
    if (std::abs (ps.force - newValue) < 0.0015f)
        return;
    ps.force = newValue;
    ps.targetForce = newValue;
    ps.editorDriving = false;   // 宿主接管：停止编辑器的回弹/写回，避免互相覆盖
}

void BreakAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1520));

    if (plateImage.isNull())
    {
        g.setColour (juce::Colours::red);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("ASSET MISSING: ui_plate", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    // 全部内容按 base 设计稿坐标绘制，整体缩放到当前窗口
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::scale (getScale()));

    // 始终用未锁定底图；锁定态只在拨杆位置贴开关开启态的小块图，
    // 避免两张 AI 图整图色调/纹理跳变，也减小打包体积。
    g.drawImageAt (plateImage, 0, 0);
    if (lockPedals && ! switchOnImage.isNull())
    {
        const auto sw = breakcal::lockSwitchHitArea().toNearestInt();
        g.drawImageAt (switchOnImage, sw.getX(), sw.getY());
    }

    // 顶部小屏：程序化 synthwave 场景（覆盖底图静态 AI 图，BPM 驱动）
    synthwave::drawScene (g, synthwave::windowRect(), scene);

    // 顶部铭牌文字：厂牌已烧录进底图，这里只画自动版本号
    labels::drawTopLabels (g, JucePlugin_VersionString);

    // 七段数码管读数（覆盖底图烧录的 000 / 127）
    const seg7::Style segStyle;
    seg7::drawDisplay (g, breakcal::brakeDisplay(),
                       (int) std::round (brake.force * breakcal::brakeForceMaxN), 4, "N", segStyle);
    seg7::drawDisplay (g, breakcal::gasDisplay(),
                       (int) std::round (gas.force * breakcal::gasForceMaxN), 4, "N", segStyle);

    // 接触阴影：先于臂/面绘制（影在物体下面），裁剪到各自凹槽框内
    auto drawShadow = [&g] (PedalState& ps, const juce::Rectangle<float>& recess)
    {
        if (ps.shadowImage.isNull())
            return;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (recess.toNearestInt());
        g.drawImageAt (ps.shadowImage, ps.shadowPos.x, ps.shadowPos.y);
    };
    drawShadow (brake, breakcal::brakeRecess());
    drawShadow (gas, breakcal::gasRecess());

    // 渲染顺序：臂在面之后面（先画臂，面盖住臂底连接处）；油门无臂
    if (! brake.armOverlay.isNull())
        g.drawImageAt (brake.armOverlay, brake.armOverlayPos.x, brake.armOverlayPos.y);
    if (! brake.overlay.isNull())
        g.drawImageAt (brake.overlay, brake.overlayPos.x, brake.overlayPos.y);
    if (! gas.armOverlay.isNull())
        g.drawImageAt (gas.armOverlay, gas.armOverlayPos.x, gas.armOverlayPos.y);
    if (! gas.overlay.isNull())
        g.drawImageAt (gas.overlay, gas.overlayPos.x, gas.overlayPos.y);
}

void BreakAudioProcessorEditor::resized()
{
    // 缩放比变了，全量重画一次（动画帧的局部 repaint 用 base 坐标，
    // 在旧 scale 下算出的区域对新 scale 不再可靠）
    repaint();
}

juce::Rectangle<int> BreakAudioProcessorEditor::toScreen (juce::Rectangle<int> baseRect) const
{
    return baseRect.toFloat().transformedBy (juce::AffineTransform::scale (getScale()))
                     .toNearestInt();
}

void BreakAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // 事件坐标是屏幕像素，换算回 base 设计稿坐标再命中
    const auto pos = e.position / getScale();

    // 点击厂牌铭牌 → 打开官网（品牌入口；文字已烧录进底图，仅保留跳转）
    if (labels::brandRect().toFloat().contains (pos))
    {
        juce::URL ("https://iisaacbeats.cn").launchInDefaultBrowser();
        return;
    }

    // 锁定拨杆（两块数码屏之间）：点击切换锁定模式
    if (breakcal::lockSwitchHitArea().contains (pos))
    {
        lockPedals = ! lockPedals;
        if (! lockPedals)   // 关闭锁定 → 已锁定的踏板立即触发松手回弹
        {
            if (gas.force > 0.001f)   { gas.targetForce   = 0.0f; gas.editorDriving   = true; }
            if (brake.force > 0.001f) { brake.targetForce = 0.0f; brake.editorDriving = true; }
        }
        repaint();
        return;
    }

    if (breakcal::brakeHitArea().contains (pos))
    {
        dragging = true;
        draggingBrake = true;
        dragStartY = pos.y;
        dragStartForce = brake.force;
        brake.editorDriving = true;
    }
    else if (breakcal::gasHitArea().contains (pos))
    {
        dragging = true;
        draggingBrake = false;
        dragStartY = pos.y;
        dragStartForce = gas.force;
        gas.editorDriving = true;
    }
}

void BreakAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const float dy = e.position.y / getScale() - dragStartY;
    const float f = juce::jlimit (0.0f, 1.0f,
                                  dragStartForce + dy / dragPixelsPerForce);
    (draggingBrake ? brake.force : gas.force) = f;
    (draggingBrake ? brake.targetForce : gas.targetForce) = f;
    (draggingBrake ? brake.editorDriving : gas.editorDriving) = true;
}

void BreakAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        if (! lockPedals)
        {
            // 松手回弹
            (draggingBrake ? brake.targetForce : gas.targetForce) = 0.0f;
        }
        else
        {
            // 锁定：松手保持当前力度不自动复位。结束编辑器驱动（最终值
            // 已在拖动中逐帧写回参数）
            PedalState& ps = draggingBrake ? brake : gas;
            ps.targetForce = ps.force;
            ps.editorDriving = false;
        }
        dragging = false;
    }
}

void BreakAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    // 悬停在厂牌铭牌 / 锁定拨杆上 → 手型光标，提示可点击
    const auto pos = e.position / getScale();
    setMouseCursor ((labels::brandRect().toFloat().contains (pos)
                     || breakcal::lockSwitchHitArea().contains (pos))
                        ? juce::MouseCursor::PointingHandCursor
                        : juce::MouseCursor::NormalCursor);
}

void BreakAudioProcessorEditor::timerCallback()
{
    // --- 场景相位推进（宿主 BPM 驱动）---
    // 相位连续累加：BPM 改变只改变速率，画面位置永不错位。
    // displayBpm 用 EMA 平滑（时间常数 ~80ms），宿主 BPM 抖动不闪跳。
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    double dt = now - lastSceneTime;
    lastSceneTime = now;
    dt = juce::jlimit (0.0, 0.1, dt);
    const double targetBpm = juce::jlimit (20.0, 999.0, processor.getCurrentBpm());
    displayBpm += (targetBpm - displayBpm) * juce::jmin (1.0, dt * 12.0);
    scene.beatPhase += dt * displayBpm / 60.0;          // 太阳脉动/条纹仍随 BPM
    scene.wallTime += dt;
    // 前进距离（网格滚动 + 棕榈树掠过）由踏板驱动，与 BPM 解绑：
    // 刹车按幅度减速（满刹停止），油门加速；刹车+油门同踩 → 刹车优先。
    const float effGas = brake.force > 0.0f ? 0.0f : gas.force;
    scene.travel += (float) dt * synthwave::forwardSpeed (effGas, brake.force);
    repaint (toScreen (synthwave::windowRect().toNearestInt()));

    auto relax = [] (PedalState& ps)
    {
        if (ps.targetForce < ps.force)      // 只在回弹时插值（拖动中 target==force）
            ps.force += (ps.targetForce - ps.force) * 0.20f;
        if (std::abs (ps.targetForce - ps.force) < 0.0015f)
            ps.force = ps.targetForce;
    };
    relax (gas);
    relax (brake);

    processor.gasForce.store (gas.force, std::memory_order_relaxed);
    processor.brakeForce.store (brake.force, std::memory_order_relaxed);

    // 只在编辑器主动驱动（拖动 / 松手回弹）时写回参数；宿主自动化期间绝不写回，
    // 否则会与音频线程的参数写入竞态覆盖（正是"曲线不跟随"的根因）。
    if (gas.editorDriving && processor.gasParam != nullptr)
        processor.gasParam->setValueNotifyingHost (gas.force);
    if (brake.editorDriving && processor.brakeParam != nullptr)
        processor.brakeParam->setValueNotifyingHost (brake.force);

    // 回弹完成（force 归 0）→ 结束编辑器驱动
    if (gas.editorDriving && gas.targetForce < 0.001f && gas.force < 0.001f)
        gas.editorDriving = false;
    if (brake.editorDriving && brake.targetForce < 0.001f && brake.force < 0.001f)
        brake.editorDriving = false;

    auto renderIfChanged = [this] (PedalState& ps)
    {
        if (std::abs (ps.force - ps.lastRenderedForce) > 0.0004f)
        {
            // 臂层 + 面层的包围盒并集（base 坐标）
            juce::Rectangle<int> bounds;
            auto doRender = [&] (PedalAnimator& an, juce::Image& ov, juce::Point<int>& pos)
            {
                an.render (ps.force, ov, pos);
                if (! ov.isNull())
                {
                    juce::Rectangle<int> b (pos.x, pos.y, ov.getWidth(), ov.getHeight());
                    bounds = bounds.isEmpty() ? b : bounds.getUnion (b);
                }
            };
            doRender (ps.armAnimator, ps.armOverlay, ps.armOverlayPos);
            doRender (ps.animator, ps.overlay, ps.overlayPos);

            // 接触阴影：与面层同一力度驱动；f≈0 清空避免残留
            if (ps.force > 0.02f)
            {
                juce::Point<float> q[4];
                ps.animator.currentQuad (ps.force, q);
                ps.shadowImage = makeContactShadow (q, ps.force, ps.shadowPos);
                if (! ps.shadowImage.isNull())
                {
                    const juce::Rectangle<int> sb (ps.shadowPos.x, ps.shadowPos.y,
                                                    ps.shadowImage.getWidth(),
                                                    ps.shadowImage.getHeight());
                    bounds = bounds.isEmpty() ? sb : bounds.getUnion (sb);
                }
            }
            else
            {
                ps.shadowImage = {};
            }

            // 残影修复：repaint 区域 = 本踏板上一帧位置 ∪ 当前帧位置
            // （bounds 已含臂+面+阴影，base 坐标换算到屏幕后再提交）。
            // lastPaintBounds 每踏板独立（此前共享导致两踏板交替运动时
            // 互相覆盖对方的旧位置记录，repaint 漏区 → 偶发残影）。
            const auto unionBounds = ps.lastPaintBounds.isEmpty()
                                       ? bounds : ps.lastPaintBounds.getUnion (bounds);
            repaint (toScreen (unionBounds));
            ps.lastPaintBounds = bounds;
            ps.lastRenderedForce = ps.force;
        }
    };
    renderIfChanged (brake);
    renderIfChanged (gas);

    // 读数屏：显示值变化才 repaint 对应玻璃区（base→屏幕坐标）
    const int brakeN = (int) std::round (brake.force * breakcal::brakeForceMaxN);
    const int gasN   = (int) std::round (gas.force * breakcal::gasForceMaxN);
    if (brakeN != lastBrakeN)
    {
        repaint (toScreen (breakcal::brakeDisplay().toNearestInt()));
        lastBrakeN = brakeN;
    }
    if (gasN != lastGasN)
    {
        repaint (toScreen (breakcal::gasDisplay().toNearestInt()));
        lastGasN = gasN;
    }
}
