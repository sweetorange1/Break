#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "source/network/UpdateChecker.h"

#include <atomic>

// 平台标识（与遥测口径一致，<os>-<arch>）
static juce::String GetUpdatePlatformString()
{
#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || defined(__arm64__)
    const juce::String arch = "arm64";
#elif defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
    const juce::String arch = "x64";
#else
    const juce::String arch = "x86";
#endif

#if JUCE_WINDOWS
    return "win-" + arch;
#elif JUCE_MAC
    return "mac-" + arch;
#elif JUCE_LINUX
    return "linux-" + arch;
#else
    juce::ignoreUnused (arch);
    return "unknown";
#endif
}

BreakAudioProcessor::BreakAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (gasParam = new juce::AudioParameterFloat (
        juce::ParameterID { "gasForce", 1 },
        "Gas Force",
        juce::NormalisableRange<float> (0.0f, 1.0f),
        0.0f));

    addParameter (brakeParam = new juce::AudioParameterFloat (
        juce::ParameterID { "brakeForce", 1 },
        "Brake Force",
        juce::NormalisableRange<float> (0.0f, 1.0f),
        0.0f));

    // 启动时延迟 5 秒检查一次更新（进程级去重，避免多实例重复触发）
    static std::atomic<bool> updateOnceFlag{false};
    if (! updateOnceFlag.exchange (true, std::memory_order_acquire))
    {
        juce::Timer::callAfterDelay (5000, []
        {
            iisaac::network::CheckForUpdatesAsync (
                "break",
                juce::String (JucePlugin_VersionString),
                GetUpdatePlatformString(),
                [] (const iisaac::network::UpdateInfo& info)
                {
                    if (info.has_update)
                        iisaac::network::ShowUpdateDialog (info);
                });
        });
    }
}

void BreakAudioProcessor::prepareToPlay (double sampleRate, int)
{
    tapeEngine.prepare (sampleRate, getTotalNumOutputChannels());
    // 静止时完全旁通（0 延迟），不再上报固定延迟
}

double BreakAudioProcessor::getCurrentBpm() const
{
    // 只读音频线程缓存的 BPM，绝不在消息线程调 getPlayHead()->getPosition()
    // （否则与音频线程的 processBlock 并发访问宿主 playhead → 偶发崩溃）。
    return (double) cachedBpm.load (std::memory_order_relaxed);
}

void BreakAudioProcessor::readPlayhead (double& bpm, int& samplesToNextBeat) const
{
    bpm = 120.0;
    samplesToNextBeat = -1;   // 无宿主节拍信息

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 999.0, *b);

            // ppqPosition：当前块起点的四分音符拍位置（double）
            if (auto ppq = pos->getPpqPosition())
            {
                const double fs = getSampleRate();
                if (fs > 0.0 && bpm > 0.0)
                {
                    const double beatLenSamples = fs * 60.0 / bpm;
                    double beatsToNext = std::ceil (*ppq) - *ppq;   // 到下一个整数拍
                    if (beatsToNext <= 0.0) beatsToNext = 1.0;       // 正好在拍上 → 下一拍
                    samplesToNextBeat = (int) std::lround (beatsToNext * beatLenSamples);
                }
            }
        }
    }
}

bool BreakAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void BreakAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // 参数优先（宿主自动化 / UI 都会写参数），回落原子量
    const float gas   = gasParam   != nullptr ? gasParam->get()   : gasForce.load   (std::memory_order_relaxed);
    const float brake = brakeParam != nullptr ? brakeParam->get() : brakeForce.load (std::memory_order_relaxed);

    double bpm = 120.0;
    int    samplesToNextBeat = -1;
    readPlayhead (bpm, samplesToNextBeat);   // 仅在音频线程访问 playhead
    cachedBpm.store ((float) bpm, std::memory_order_relaxed);

    tapeEngine.process (buffer.getArrayOfWritePointers(),
                        buffer.getNumChannels(),
                        buffer.getNumSamples(),
                        juce::jlimit (0.0f, 1.0f, gas),
                        juce::jlimit (0.0f, 1.0f, brake),
                        bpm,
                        samplesToNextBeat);
}

juce::AudioProcessorEditor* BreakAudioProcessor::createEditor()
{
    return new BreakAudioProcessorEditor (*this);
}

void BreakAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement state ("Break");
    state.setAttribute ("gasForce",   (double) gasForce.load());
    state.setAttribute ("brakeForce", (double) brakeForce.load());
    copyXmlToBinary (state, destData);
}

void BreakAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const float g = (float) xml->getDoubleAttribute ("gasForce", 0.0);
        const float b = (float) xml->getDoubleAttribute ("brakeForce", 0.0);
        gasForce.store (g);
        brakeForce.store (b);
        if (gasParam != nullptr)   gasParam->setValueNotifyingHost (g);
        if (brakeParam != nullptr) brakeParam->setValueNotifyingHost (b);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreakAudioProcessor();
}
