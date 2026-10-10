cmake_minimum_required(VERSION 3.22)
project(Break VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===== JUCE 8.0.12（与 Pupon / CRTLoss / Organic Chemistry 一致）=====
# 本机构建时用 -DFETCHCONTENT_SOURCE_DIR_JUCE 指向已有 clone，避免重新下载
include(FetchContent)
FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
)
FetchContent_MakeAvailable(juce)

# ===== 当前：Standalone + VST3，仅踏板动画链路，无 DSP =====
juce_add_plugin(
        Break
        PRODUCT_NAME "Break"
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "iisaacbeats.cn"
        BUNDLE_ID "cn.iisaacbeats.Break"
        PLUGIN_MANUFACTURER_CODE Isac
        PLUGIN_CODE Brk1
        FORMATS Standalone VST3 AU
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_AUTO_MANIFEST FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(Break)

# ===== 文生图 UI 链路资产：空腔底图 + 踏板精灵（由 outputs/tools/make_assets.py 生成）=====
juce_add_binary_data(BreakBinaryData
        SOURCES
        assets/ui_plate_unlocked.png
        assets/switch_on.png
        assets/gas_sprite.png
        assets/brake_sprite.png
        assets/arm_sprite.png
        assets/vt323-latin.ttf
)

target_compile_definitions(Break
        PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(Break PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        PedalAnimator.cpp
        PedalAnimator.h
        TapeEngine.h
        SegDisplay.h
        SynthwaveScene.h
        Labels.h
        shared/IisaacTelemetry.h
        source/network/Version.h
        source/network/Version.cpp
        source/network/UpdateChecker.h
        source/network/UpdateChecker.cpp
        source/ui/UpdateDialog.h
        source/ui/UpdateDialog.cpp
)

# Standalone 需要 juce_audio_utils（传递依赖 juce_audio_devices）
target_link_libraries(Break PRIVATE juce::juce_audio_utils BreakBinaryData)

# ===== 无头诊断：用与插件相同的动画代码+资产跑渲染，产出 PNG 验证 =====
add_executable(break_headless tests/headless_render.cpp PedalAnimator.cpp)
target_link_libraries(break_headless PRIVATE juce::juce_core juce::juce_graphics BreakBinaryData)
target_compile_definitions(break_headless PRIVATE BREAK_DEBUG_RENDER=1)
if(MSVC)
        target_compile_options(break_headless PRIVATE /utf-8)
endif()

# ===== DSP 冒烟测试（纯 C++，不链接 JUCE） =====
add_executable(break_dsp_test tests/dsp_smoke.cpp)
target_include_directories(break_dsp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
        target_compile_options(break_dsp_test PRIVATE /utf-8)
endif()

# 源码含中文注释，MSVC 需按 UTF-8 解释
if(MSVC)
        target_compile_options(Break PRIVATE /utf-8)
endif()
