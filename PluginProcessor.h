#pragma once

#include <JuceHeader.h>
#include "TapeEngine.h"

// ============================================================
//  Break - 蒸汽波踏板效果器（走带机构）
//
//  DSP 主体在 TapeEngine.h：把整台机器当作一台磁带机，
//  油门 = 加速（音高上扬 / 变亮 / 饱和，重复片段按宿主 BPM 对齐），
//  刹车 = 减速直至 tape stop（音高下滑 / wow-flutter / 高频丢失 / 摩擦噪声）。
//  两踏板松开 → 直接旁通（0 延迟），读头空转待命。
//  gasForce / brakeForce 以原子值暴露给编辑器，参数对象供宿主自动化。
// ============================================================
class BreakAudioProcessor : public juce::AudioProcessor
{
public:
    BreakAudioProcessor();
    ~BreakAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Break"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // 松刹车后磁带里存着的素材最长还能再放一个缓冲
    double getTailLengthSeconds() const override { return (double) tape::bufferSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // 宿主 BPM（场景动画速度源）。宿主未提供 / Standalone 无宿主时回落 120。
    double getCurrentBpm() const;

    // 从 playhead 读 BPM 与"到下一个节拍边界的样点数"（-1 = 无宿主）
    void readPlayhead (double& bpm, int& samplesToNextBeat) const;

    // 油门/刹车力度 0..1（编辑器实时写入；后续 DSP 在 processBlock 消费）
    std::atomic<float> gasForce   { 0.0f };
    std::atomic<float> brakeForce { 0.0f };

    // 最近一次在音频线程读到的宿主 BPM（供消息线程只读）。绝不能在消息线程
    // 调 getPlayHead()->getPosition()——宿主 playhead 通常只允许音频线程访问，
    // 两线程并发读取是"点停带后偶发崩溃"的根因。
    std::atomic<float> cachedBpm { 120.0f };

    // v1 直接暴露参数指针给编辑器；接入 APVTS 是后续工作
    juce::AudioParameterFloat* gasParam   = nullptr;
    juce::AudioParameterFloat* brakeParam = nullptr;

    // 走带机构（含饱和 / tilt / 磁头低通 / wow-flutter / 噪声）
    tape::Engine tapeEngine;

    // 当前走带速度（调试/UI 可轮询）
    float getTapeSpeed() const noexcept { return tapeEngine.getSpeed(); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakAudioProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
