# Break

蒸汽波踏板效果器（UI = 蒸汽波踏板机，DSP = 磁带机走带机构）。

## 本阶段包含

- **DSP 已接入**：`processBlock` 走 `TapeEngine.h` 的走带机构（详见「DSP：走带机构」一节）。
  两踏板松开时直接旁通（输出 = 输入，0 延迟）。
- 底图 = `assets/ui_plate_unlocked.png`（双空腔：油门空腔来自"扣掉油门踏板保留转轴"、
  刹车空腔来自"扣掉刹车踏板"，均与基准图零平移对齐，由
  `WorkBuddy/outputs/tools/make_assets.py` + `make_brake_assets.py` 生成；
  新版在两块数码屏之间加了**锁定拨杆**）。拨杆开启态只存了开关那一小块的
  `assets/switch_on.png`（96×150，从锁定底图裁剪），减小打包体积。
- 油门踏板 = `assets/gas_sprite.png`，刹车踏板 = `assets/brake_sprite.png`，
  刹车臂（连杆）= `assets/arm_sprite.png`。各自按力度做 **4 点单应插值**实时变形
  （标定结构体注入，油门 1 层 / 刹车 2 层）：
  - `PedalAnimator.h` 的 `gasCal / brakeCal / brakeArmCal` 是唯一标定入口，
    分别与 make_assets.py / make_brake_assets.py / make_arm_sprite.py 一一对应，两边改必须同步。
- 刹车 = 吊挂式：运动组件分**臂层**（转轴→面顶金属带，臂顶两角两姿态钉死在转轴）
  与**面层**；渲染顺序臂先、面后（面盖住臂底连接处，无缝）。空腔图把臂也扣掉了，
  所以臂必须独立成层，不能贴回底图当静态件。
- **臂透视与变形幅度**：踩下 = 整体远离使用者 → **变小**（方向勿标反，曾把 FULL
  底边标成 90px 变宽 → 连杆看着在放大）。**幅度要与面统一**：面收窄 78%/70%，
  臂收窄比也应同档——曾收窄到 28px（54%）被反馈「连杆变形过大」，现改为
  52→38px（~73%）+ 左移仅 8px 跟随面顶中心（FULL 底角 (375,623)/(337,625)）。
- **踩下阴影**：面底边正下方画**接触阴影**（同心椭圆软黑影，中心 alpha≈0.5f，
  随力度加深下沉），绘制顺序 底图→阴影→臂→面，且**裁剪到凹槽框内**
  （`breakcal::brakeRecess()/gasRecess()`，与 make_brake_assets.py 的 RECESS
  数值一致），避免越到金属外框上。另加**踩下暗化**：`PedalAnimator::setDarkening`
  （油门 0.10 / 刹车面与臂 0.16，预乘 ARGB 只缩 RGB），强化压入阴影的层次。
  阴影区域并入每踏板 repaint 并集。
- 刹车标定：亮段成对识别面行 + 对角极值提角（REST y601..745，FULL y616..714，
  面区互相关无残余平移；f=1 面内差 ~20 为 AI 重绘肋条的内容差，非位置误差）。
- **七段数码管读数**（`SegDisplay.h`，移植 pedal_anim.py 的 draw_seven_seg）：
  程序绘制 4 位数字 + 单位 N，玻璃底色矩形覆盖底图烧录的 000/127。
  力度→牛顿线性映射：**刹车 0-2000N / 油门 0-1000N**（`breakForceMaxN /
  gasForceMaxN`，显示用标定，非物理量）。颜色采样自底图：点亮 RGB(63,218,241)、
  玻璃底 RGB(34,13,53)；点亮段带辉光描边，未点亮幽灵段保留。
  屏区玻璃矩形 = `breakDisplay()/gasDisplay()`（250,258..494,324 / 662,258..906,324，
  `_display_bbox.py` 暗区检测 + 目检微调）。读数变化才 repaint 对应屏区。
- 交互：在**左侧刹车凹槽 / 右侧油门凹槽**分别**按住向下拖动**加力、向上减力，
  **松手回弹**；两踏板力度独立（`brakeForce` / `gasForce` 参数，0..1）。
- **锁定拨杆**：两块数码屏之间（`breakcal::lockSwitchHitArea()`，x 536..632）
  的拨杆控制是否锁定踏板——拨杆开启（锁定）时踩下踏板**松手不自动回弹**，
  力度保持当前值；再次拖动可增减。**关闭锁定立即触发回弹**（把已锁定踏板
  targetForce 置 0）。切换锁定态只在拨杆位置贴 `switch_on.png` 小块图（开启态），
  始终画未锁定底图，避免两张 AI 图整图色调跳变、也减小打包体积。点击厂牌
  铭牌仍跳转官网（文字已烧录进底图，不再程序绘制厂牌，版本号仍程序绘制）。
- **窗口缩放**（同 CRTLoss）：base 设计稿 1168×864，一切绘制/命中/repaint 都在
  base 坐标计算，`getScale() = getWidth()/1168` 统一换算（绘制用
  AffineTransform::scale，鼠标事件坐标除以 scale，repaint 区域乘 scale）。
  默认 3/4 尺寸 876×648 打开，右下角拖动等比缩放
  （`setResizable(true,true)` + `ComponentBoundsConstrainer::setFixedAspectRatio`），
  范围 1/4 .. 1:1（base 分辨率为上限，再大会插值发虚）。
  `resized()` 里必须全量 repaint——动画帧的局部 repaint 区域是旧 scale 下算的。
- 60Hz 单一 Timer 统一驱动动画，局部 repaint。**repaint 区域按踏板独立计算**
  （`PedalState::lastPaintBounds`）——曾共享一个成员，两踏板交替运动时互相覆盖
  对方的旧位置记录，repaint 漏区 → 偶发残影（非必现，就是这个）。
- `tests/headless_render.cpp`（break_headless 目标）：无头跑同一份动画代码+资产，
  输出叠加层 PNG、**整帧合成图 frame_check_0/1/2.png（含 seg7 读数）**与
  不透明像素统计，用于回归验证（需 `-DBREAK_DEBUG_RENDER=1` 打印矩阵）。

## 已修的坑（勿再踩）

- 手动搭 MSVC 构建环境时，`PATH` 必须包含 Windows SDK 的 `bin\<ver>\x64`
  （`rc.exe`/`mt.exe`），只配 `INCLUDE`/`LIB` 会让 CMake 编译器自检在链接步
  骤失败（`rc: no such file or directory`）→ 直接改用 `build.ps1`。
- DLT 方程组右端项必须是 `+u/+v`（曾经写成 `-u/-v`，解出翻转矩阵 → 踏板整体不可见）。
- 单应插值采样的是**预乘 ARGB**，插值后不要再调 `premultiply()`（双重预乘压黑半透明边缘）。
- JUCE 把 SharedCode.lib 作为 order-only 依赖：只改插件源码后用
  `--target Break_Standalone`（或默认 all）才会重链 exe；`--target Break` 只编 SharedCode。
- `juce_add_binary_data` 增删资产后 `BinaryData.h` 可能不重新生成（stale）：
  删掉 `juce_binarydata_BreakBinaryData/JuceLibraryCode/BinaryData.h` 再编。
- repaint 区域必须覆盖**上一帧 overlay 位置 ∪ 当前帧位置**，且每踏板独立记录，
  否则局部重绘漏区 → 残影。
- 接触阴影区域必须并入每踏板 repaint 并集（阴影椭圆可超出面四角 bbox 下方
  ~30px），漏并 → 阴影残迹。
- `Rectangle<float>` 没有 `getSmallestIntegerBounds()`，用 `toNearestInt()`。
- JUCE 压缩 BinaryData 变量名里的连续分隔符：`vt323-latin.ttf` → `vt323latin_ttf`。
- **宿主自动化参数 ≠ 编辑器写回参数**：宿主在音频线程同步调 `setValueNotifyingHost`
  （触发 `parameterValueChanged`），编辑器 `timerCallback` 在消息线程。若 timer
  每帧按 `|param - force|` 写回参数，会在"参数已更新、force 未更新"的竞态窗口里
  把旧 `force` 写回、覆盖宿主曲线值 → 曲线不跟随/旋钮跳回。正确做法：用
  `PedalState::editorDriving` 标志，**只在用户拖动/回弹时写回**，宿主自动化期间
  完全不写回（`applyHostForce` 同时把 `editorDriving` 置 false 表示宿主接管）。

## 顶部铭牌文字（程序绘制，非图片）

- 底图上的旧文字（DREAMSCAPE / AUDIO MOD // STUDIO UNIT）已离线抹除：
  `outputs/tools/wipe_labels.py`（mini-inpaint：亮笔画 mask + 膨胀 3px + 邻域扩散
  填充，保留铭牌紫色底与做旧纹理）。备份在 `assets/ui_plate_backup.png`。
- 新文字由 `Labels.h` 运行时绘制：左上 `iisaacbeats.cn`（固定）、右上
  `v + JucePlugin_VersionString`（自动跟随 CMake `VERSION`，改版本不用动图）。
- 字体 VT323（官网同款，woff2→ttf 存 `assets/vt323-latin.ttf` 嵌入 BinaryData）。
  16px（8×16 网格整数倍）渲染边缘最硬；字色 0xffb899d4（原字采样）。

## 顶部小屏：程序化 Synthwave 场景（双时间轴）

- `SynthwaveScene.h` 纯绘制函数（与 SegDisplay.h 同约定），覆盖底图烧录的
  AI 静态场景；玻璃区实测内沿 `(66, 50, 1036, 161)`（base 坐标，四角 8x
  放大目检；用户半缩放实测 516x80@(35,82)×2 互证），微小圆角 8px
  （`cornerRadius`，`addRoundedRectangle` 裁剪）。
- 元素：渐变天空、闪烁星空（确定性 hash，无状态）、巨大落日（逐水平行段
  绘制，条纹行跳过 → 真缝隙露天空，每 2 拍循环下移）、双层线框山脉
  （中央 V 谷露太阳，近山品红描边+辉光）、透视网格地面（纵向放射 +
  横向青线随前进距离流动）、**两侧棕榈树剪影**（沿轨道世界坐标固定，
  左右独立随机间距、不同相，从极远处淡入逼近、放大再掠过）、
  地平线辉光带、CRT 扫描线。
- **双时间轴**：`beatPhase`（BPM 拍）只驱动太阳脉动/条纹等非前进运动，
  `PluginProcessor::getCurrentBpm()` 从 playhead 读（JUCE 8
  `getPosition()->getBpm()` Optional API，无宿主回落 120，`displayBpm`
  EMA 平滑防抖）；**`travel`（前进距离）由两踏板驱动**——`forwardSpeed`
  = 基速 ×(1+油门增益) ×(1-刹车)，满刹车停止、满油门加速，网格滚动与
  棕榈树都随 `travel` 连续累加（与 BPM 解绑，速度改变位置零跳变）。
- 调参入口全在 `SynthwaveScene.h`：太阳大小/位置（`sunR`/`sunCY`）、
  网格密度（横向 `0.30f` / 纵向 `W*0.13f`）、前进基速 `baseSpeedCellsPerSec`
  / 油门增益 `gasSpeedGain`、棕榈树间距 `spacing` / 最远 `farZ`、
  条纹密度（`u * 5.0f`）。
- 无头验证：`break_headless` 输出 `synth_check_0/1/2.png` 三相位帧。

## 构建（本机已验证）

```bash
# 推荐：一键脚本（自动用 vswhere 探测最新 MSVC + Windows SDK）
powershell -NoProfile -ExecutionPolicy Bypass -File "i:/Break/build.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "i:/Break/build.ps1" -Target Break_VST3
```

手动等价命令（脚本内部就是把环境配好再跑这些）：

```bash
# Ninja + MSVC（手动设置 INCLUDE/LIB/PATH，勿用 Enter-VsDevShell）
cmake -S I:\Break -B I:\Break\cmake-build-ninja -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="I:\CRTLoss\cmake-build-release-visual-studio\_deps\juce-src"

# 全量（含 Standalone + VST3 + headless）
cmake --build I:\Break\cmake-build-ninja --config Release

# 或按目标单独编（改 FORMATS 后需先重跑上面的 cmake 配置一步）
cmake --build I:\Break\cmake-build-ninja --config Release --target Break_Standalone
cmake --build I:\Break\cmake-build-ninja --config Release --target Break_VST3
cmake --build I:\Break\cmake-build-ninja --config Release --target break_headless
```

产物：
- Standalone：`cmake-build-ninja/Break_artefacts/Release/Standalone/Break.exe`
- VST3：`cmake-build-ninja/Break_artefacts/Release/VST3/Break.vst3`（JUCE 8 内置 VST3 SDK，无需另下 steinberg；manifest 资源嵌入，无独立 moduleinfo.json）

## DSP：走带机构（`TapeEngine.h`）

整台机器 = 一台磁带机，**所有控制都在两个踏板上，没有其它控件**。
引擎是纯 C++（不依赖 JUCE），便于无头测试；`PluginProcessor` 只做参数读取与转发。

### 踏板语义

| | 走带速度 | 音高 | 音色 | 机械声 |
|---|---|---|---|---|
| 油门 `gas` | 1.0 → 2^0.5 = 1.41 | **上扬** +6 半音 | tilt 提亮 +5.5dB、去隆隆声、磁带饱和推进 | 轻微 wow/flutter |
| 刹车 `brake` | 1.0 → 2^-4.6 = 0.041 | **下滑** -4.6 八度 | tilt 压暗 -7.5dB、磁头低通 17k→260Hz | wow/flutter 加重 + 摩擦啸叫 |
| 都松开 | 回 1.0，读头弹簧回位 | 原音高 | **直接旁通（输出=输入，0 延迟）** | 无 |

速度按 `2^八度` 映射（踏板行程 = 音高线性）；加速时间常数 80ms、
制动 35ms（刹车更"咬手"）。**油门+刹车同踩 → 刹车优先**：引擎在
`brakeTarget > 0` 时把油门清零，只按刹车走带（不再叠加）。

### 静止旁通 + 变速实现（真 varispeed，不是 pitch shifter）

- 环形缓冲 4.0s（取到 2 的幂），写头按输入推进，**读头按 `speed` 独立推进**，
  4 点 Catmull-Rom 分数插值 → 时长是真的被拉长/压缩（tape stop 会真的把时间拖住）。
- **静止直接旁通**：两踏板都松开且已回位（speed≈1、delay≈loopLen）时，用 30ms 等功率
  交叉淡化切到纯旁通，输出 = 输入、0 延迟；读头空转待命（`processBypass` 只推进写头+读头，
  不走滤波链）。因此不再上报固定延迟（`setLatencySamples` 已移除）。
- **松手瞬间读头立即重置**：踏板从按下回弹到 0 的下降沿（`wasPedalDown && !pedalDown`），
  直接把读头 `setDelay(loopLen)`、`speedSm=1`。这样音高**立即**恢复正常，不再有
  "追带"的加速尾巴（曾经松手后要追带几秒才归位，用户反馈"0N 之后声音还在加速"）。
  读头跳变被随后 30ms 旁通淡化掩盖。见 `tests/dsp_smoke.cpp` 第 7 项。
- **弹簧回位**（保留作微调）：旁通态 / BPM 变化时用一个比例项把延迟平滑拉回 loopLen
  （"机器转回正常速度"）。门控必须够陡——曾经 `*1.6` 导致轻踩刹车时回位力抵消制动，
  音高降不下去（0.3 刹车只能降到 0.54 而非 0.38）。
- **接头（splice）**：延迟漂出缓冲范围时把读头搬回 loopLen，40ms 等功率交叉淡化。
  减速（刹车）时读头越落越后，落到缓冲底才回卷（满刹车发生在已静音段，听不到）。

### BPM 对齐回卷（加速的"片段重复"落在节拍上）

加速时读头追写头，delay 会缩到 0，必须回卷（= 重复播放一段）。回卷改为**按宿主 BPM
对齐**，而不是撞到边界才随机跳：

- `loopLen = beatsPerLoop × beatLen`（`beatsPerLoop=1`，即一次重复 1 拍；`beatLen = fs*60/bpm`）。
- `PluginProcessor::readPlayhead` 从 playhead 读 BPM 与 `ppqPosition`，换算成
  "当前块起点到下一个节拍边界的样点数" 传给引擎；引擎在块内逐样点递减节拍时钟。
- 当读头按当前速度"未来一拍内会撞写头"时，**调度一次回卷**，等节拍时钟归零
  （= 节拍边界）才执行，回卷落点 = loopLen。因此每次重复都从节拍上开始。
- 回卷瞬间的 40ms 淡化期间读头正在过渡，此时**禁止再次调度**
  （`!isFading()`），否则会因 `getDelaySamples()` 仍返回旧读头位置而误判、触发
  双重回卷（已修，见 `tests/dsp_smoke.cpp` 第 6 项）。
- 实测 @120bpm：满油门回卷发生在 `47998, 95998, …`（间隔 48000 样点 = 2 拍，
  相位偏差 0 样点）。

### 信号链（每声道，顺序固定；旁通态整体跳过）

```
输入 → 磁带饱和(tanh, 油门量) → +磁带底噪 →【走带：wow/flutter 调制速度】
     → 停止闸门 → 高通(并联, 油门量) → Tilt EQ(1kHz pivot) → 磁头低通(并联, 刹车量)
     → +机械摩擦噪声(带通白噪) → 软限幅
```

- **底噪写在磁带上**（进缓冲前注入）→ 会跟着一起变速；**摩擦噪声在走带后**注入（不变速）。
- 停止闸门：`speed < 0.16` 开始淡出，`< 0.055` 完全静音 → 满刹车最终是"磁带停转"的静音。
- 两声道 wow/flutter 相位错开 → 立体声宽度（真实磁带两轨不同步）。

### 透明性约束（改代码别踩）

静止状态必须**逐比特级等于输入**（直接旁通），湿态的所有滤波器都是**并联混合**介入
（`y += mix*(filtered - y)`），而不是串联固定滤波器：

- 低通 `lpMix = brake`、高通 `hpMix = gas`、饱和 `driveMix = gas`。
- wow/flutter 深度**不能留常数项**：常数 0.004 就让读头延迟被 ±89 样点缓慢调制。
- 旁通切换用干/湿交叉淡化（30ms），避免从 0 延迟干声到 loopLen 延迟湿声的硬切咔哒。

### 无头验证

```bash
cmake --build I:\Break\cmake-build-ninja --config Release --target break_dsp_test
I:\Break\cmake-build-ninja\break_dsp_test.exe     # ALL_OK
```

覆盖：静止旁通（输出==输入，误差 0）、满刹车停转、满油门电平、随机踏板下的 NaN/峰值、
**踏板→音高方向**（油门 0.6 实测 1.226 / 期望 1.231；刹车 0.3 实测 0.390 / 期望 0.384）、
**加速回卷对齐节拍**（回卷相位偏差 0 样点）、
**松手瞬间读头重置**（松手后 0.4s 内输出==输入，误差 0）。

## 遥测 / 更新提示 / 安装

- **遥测**（`shared/IisaacTelemetry.h`，header-only，移植自 LDSJvst）：在编辑器
  构造末尾创建 `iisaac::telemetry::Session`（product `break`），5 秒后起工作线程，
  读/建 `%APPDATA%\iisaacbeats\Telemetry\break.xml`（进程锁），每日至多向
  `iisaacbeats.cn/api/telemetry/ping` POST 一次；失败静默、留 15 分钟冷却，跨 UTC
  日自动再报。编辑器析构开头 `telemetrySession.reset()` 保证在消息线程 join 工作线程。
  定义 `IISAAC_TELEMETRY_DISABLED` 或设环境变量 `IISAAC_TELEMETRY_DISABLED=1` 可停用。
- **更新提示**（`source/network/{Version,UpdateChecker}` + `source/ui/UpdateDialog`）：
  Processor 构造里延迟 5 秒（进程级去重）异步 GET `iisaacbeats.cn/api/update/check`，
  若 `has_update` 且本地版本较旧，则弹品红霓虹风格更新弹窗（Download / Remind Me Later；
  `force_update` 时不可跳过）。服务端字段：`has_update / latest_version / download_url /
  changelog / force_update`。
- **安装脚本**（Windows，Inno Setup 6）：`build_installer.bat` 自动探测 Release 版
  `Break.vst3` 并调用 `Break_installer.iss` 打包，产物 `dist\Break_Setup_<ver>_x64.exe`，
  安装到 `C:\Program Files\Common Files\VST3\iisaacbeats.cn`。版本号与 CMake 的
  `VERSION` 保持同步（改版本时同步更新 `.iss` / `.bat` 里的版本常量）。

## 后续待办

- 力度→牛顿的真实硬件映射（当前为显示用线性标定）。
- APVTS 参数化、preset 保存。
