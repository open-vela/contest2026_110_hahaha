# 开发板语音链路测试手册

> 适用硬件：R528S3-Gemini-S1 + openvela nsh_minidisplay + 2.8寸 SPI 屏
> 生成时间：2026-08-05

---

## 目录

- [一、测试前准备](#一测试前准备)
- [二、设备节点检查](#二设备节点检查)
- [三、音频命令检查](#三音频命令检查)
- [四、Mixer 通路检查](#四mixer-通路检查)
- [五、喇叭测试](#五喇叭测试)
- [六、麦克风录音测试](#六麦克风录音测试)
- [七、录音回放测试](#七录音回放测试)
- [八、voice_service 功能测试](#八voice_service-功能测试)
- [九、ASR 链路测试（需主机配合）](#九asr-链路测试需主机配合)
- [十、TTS 链路测试（需主机配合）](#十tts-链路测试需主机配合)
- [十一、常见问题排查](#十一常见问题排查)
- [十二、测试结果记录表](#十二测试结果记录表)

---

## 一、测试前准备

### 1.1 所需工具

- 开发板（已刷入 openvela 固件）
- USB 串口线（连接板端 NSH Shell）
- USB 数据线（ADB 连接，用于主机侧 ASR/TTS）
- 主机（已安装 Python 3、Vosk、Edge TTS）
- 耳机或喇叭（可选，用于外放测试）

### 1.2 音频配置确认

当前固件已启用的音频配置（`.config`）：

```
CONFIG_R528_AUDIO=y                          # R528 音频驱动
CONFIG_SND_CODEC_SUN8IW20_AUDIOCODEC=y       # AllWinner 音频编解码器
CONFIG_SND_PLATFORM_SUNXI_PCM=y              # PCM 接口
CONFIG_COMPONENTS_AW_TINY_ALSA_LIB=y         # ALSA 库
CONFIG_COMPONENTS_AW_ALSA_UTILS=y            # ALSA 工具
CONFIG_COMPONENTS_AW_ALSA_UTILS_AMIXER=y     # amixer
CONFIG_COMPONENTS_AW_ALSA_UTILS_APLAY=y      # aplay
CONFIG_COMPONENTS_AW_ALSA_UTILS_ARECORD=y    # arecord
CONFIG_AW_DRIVERS_AUDIO=y                    # 音频驱动框架
CONFIG_AW_AUDIO_CODEC=y                      # 音频编解码器框架
CONFIG_SYSTEM_NXPLAYER=y                     # nxplayer 播放器
CONFIG_SYSTEM_NXRECORDER=y                   # nxrecorder 录音器
CONFIG_AUDIO_CUSTOM_DEV_PATH=y               # 自定义设备路径
CONFIG_AUDIO_DEV_PATH="/dev/audio"           # 音频设备目录
CONFIG_AUDIOUTILS_NXAUDIO_DEVPATH="/dev/audio/pcm0p"  # 默认播放 PCM 设备
```

### 1.3 标准音频参数

| 参数 | 值 |
|------|-----|
| 采样率 | 16000 Hz |
| 位深 | 16 bit |
| 声道数 | 1 (mono) |
| 格式 | 原始 PCM (无头) |
| 设备路径 | 播放 `/dev/audio/pcm0p`，录音 `/dev/audio/pcm0c` |
| 每秒数据量 | 32 KB/s |

---

## 二、设备节点检查

**在 NSH Shell 中执行：**

```bash
ls /dev/audio
ls /dev/audio/*
```

**预期结果：**

```
/dev/audio:
 pcm0c
 pcm0p
```

**判断标准：**

| 结果 | 说明 | 处理 |
|------|------|------|
| `/dev/audio/pcm0p` 和 `/dev/audio/pcm0c` 存在 | 音频设备已注册 | 继续下一步 |
| `/dev/audio/` 为空 | 驱动未加载 | 检查 CONFIG_R528_AUDIO 和 CONFIG_SND_CODEC |
| `/dev/audio` 不存在 | 音频子系统未初始化 | 检查 CONFIG_AUDIO |

---

## 三、音频命令检查

**在 NSH Shell 中执行：**

```bash
help nxplayer
help nxrecorder
aplay -l
arecord -l
```

**预期结果：**

- `nxplayer` 显示帮助信息（包含 device, tone, playraw 等命令）
- `nxrecorder` 显示帮助信息（包含 device, recordraw 等命令）
- `aplay -l` 列出至少 1 个播放设备
- `arecord -l` 列出至少 1 个录音设备

**判断标准：**

| 结果 | 说明 | 处理 |
|------|------|------|
| 所有命令可用 | 工具链完整 | 继续下一步 |
| `nxplayer` command not found | 未编译 | 检查 CONFIG_SYSTEM_NXPLAYER |
| `nxrecorder` command not found | 未编译 | 检查 CONFIG_SYSTEM_NXRECORDER |
| `aplay -l` 无设备 | ALSA 未发现设备 | 检查 codec 驱动 |

---

## 四、Mixer 通路检查

**在 NSH Shell 中执行：**

```bash
amixer
amixer contents
```

**预期结果：**

`amixer` 显示当前 mixer 状态，`amixer contents` 列出所有控制项，包括：

- 音量控制（DAC Volume, ADC Volume）
- 通道开关（Left Output Mixer, Right Output Mixer）
- 输入源选择（Mic, Line In）

**判断标准：**

| 结果 | 说明 | 处理 |
|------|------|------|
| 有控制项且音量 > 0 | mixer 正常 | 继续下一步 |
| 有控制项但音量 = 0 | 静音 | 设置音量（见下方） |
| 无控制项或报错 | mixer 异常 | 检查 codec 驱动 |

**设置音量示例：**

```bash
# 查看当前音量
amixer contents | grep -i volume

# 设置 DAC 音量（具体命令视 mixer 控制项而定）
amixer cset name='DAC Volume' 160
amixer cset name='ADC Volume' 160
```

---

## 五、喇叭测试

**目的**：验证喇叭播放链路是否正常。

**在 NSH Shell 中执行：**

```bash
nxplayer
```

进入 nxplayer 交互模式后，输入以下命令：

```
device /dev/audio/pcm0p
tone 16000 2 1000
q
```

**命令说明：**

| 命令 | 含义 |
|------|------|
| `device /dev/audio/pcm0p` | 选择播放 PCM 设备 |
| `tone 16000 2 1000` | 16kHz 采样率，持续 2 秒，1kHz 频率 |
| `q` | 退出 nxplayer |

**预期结果：**

喇叭发出 2 秒 1kHz 蜂鸣声（"嘀——"）。

**判断标准：**

| 结果 | 说明 | 处理 |
|------|------|------|
| 有蜂鸣声 | 喇叭链路正常 | 继续下一步 |
| 无声 | 播放链路不通 | 见[十一、常见问题排查](#十一常见问题排查) |
| 有杂音/破音 | 硬件或增益问题 | 检查 mixer 增益设置 |

---

## 六、麦克风录音测试

**目的**：验证麦克风录音链路是否正常。

### 6.1 创建测试目录

```bash
mkdir -p /data/audio_test
```

### 6.2 录制音频

```bash
nxrecorder
```

进入 nxrecorder 交互模式后，输入以下命令：

```
device /dev/audio/pcm0c
recordraw /data/audio_test/mic_test.pcm 1 16 16000 0
```

**命令说明：**

| 命令 | 含义 |
|------|------|
| `device /dev/audio/pcm0c` | 选择录音 PCM 设备 |
| `recordraw <路径> <声道> <位深> <采样率> <0>` | 开始原始 PCM 录音 |

**此时录音已开始**，对着麦克风说话 3-5 秒，然后输入：

```
stop
q
```

### 6.3 验证录音文件

```bash
ls -l /data/audio_test/mic_test.pcm
```

**预期结果：**

文件大小 > 0。按 16kHz/16bit/mono 计算，每秒约 32KB，录 5 秒应约 160KB。

**判断标准：**

| 文件大小 | 说明 | 处理 |
|----------|------|------|
| > 100KB（录 3-5 秒） | 录音正常 | 继续下一步 |
| 几 KB | 录音时间太短或异常 | 重新录制，确保说了话 |
| 0 字节 | 录音链路不通 | 见[十一、常见问题排查](#十一常见问题排查) |
| 文件不存在 | 录音未启动 | 检查 nxrecorder 命令是否正确 |

---

## 七、录音回放测试

**目的**：验证录音文件能正确播放。

```bash
nxplayer
```

进入 nxplayer 交互模式后，输入：

```
device /dev/audio/pcm0p
playraw /data/audio_test/mic_test.pcm 1 16 16000 0
q
```

**预期结果：**

喇叭播放出刚才录的语音内容。

**判断标准：**

| 结果 | 说明 | 处理 |
|------|------|------|
| 能听到录音 | 完整录放链路正常 | 继续下一步 |
| 无声 | 播放链路问题 | 检查 mixer 音量和设备路径 |
| 有杂音/变速 | 参数不匹配 | 确认录制和播放参数一致 |

---

## 八、voice_service 功能测试

### 8.1 意图解析测试

```bash
qiban_voice_service intent "导航到清华大学"
```

**预期输出：**

```
意图: NAVIGATE
目的地: 清华大学
```

### 8.2 更多意图测试

```bash
qiban_voice_service intent "帮我导航到北京大学东门"
qiban_voice_service intent "取消导航"
qiban_voice_service intent "放大地图"
qiban_voice_service intent "刷新地图"
```

### 8.3 别名列表

```bash
qiban_voice_service aliases
```

### 8.4 示例列表

```bash
qiban_voice_service examples
```

### 8.5 录音准备

```bash
qiban_voice_service listen 4
```

**预期输出：**

```
Listening prepared.
pcm: /data/qiban_voice/recordings/voice_XXXXX.pcm
start cmd: /data/qiban_voice/scripts/record_XXXXX_start.cmd
stop cmd: /data/qiban_voice/scripts/record_XXXXX_stop.cmd
```

**注意**：`listen` 只是准备录音脚本，还需要手动执行 nxrecorder 完成实际录音。

### 8.6 TTS 请求

```bash
qiban_voice_service speak "前方五十米右转"
```

**预期输出：**

```
tts prepared.
text: 前方五十米右转
request: /data/qiban_voice/tts/last_request.txt
```

**注意**：板端没有 TTS 引擎，需要主机侧 worker 完成合成。

---

## 九、ASR 链路测试（需主机配合）

### 9.1 主机侧安装（一次性）

```bash
cd /home/xjx/contest2026_110_hahaha
./tools/audio/setup_local_voice.sh
```

安装内容：
- Python 包：vosk, edge-tts
- ASR 模型：vosk-model-small-cn-0.22（约 50MB）

### 9.2 板端发起录音

```bash
# 板端 NSH
qiban_voice_service listen 4
nxrecorder
# 粘贴 start cmd 内容：
device /dev/audio/pcm0p
recordraw /data/qiban_voice/recordings/voice_XXXXX.pcm 1 16 16000 0
# 对着麦克风说 "导航到清华大学"，等 3-5 秒后输入：
stop
q
```

### 9.3 主机侧 ASR 识别

**方式 A：自动 worker（推荐）**

```bash
# 主机终端
./tools/audio/qiban_asr_local_worker.sh --adb-serial <设备序列号>
```

自动完成：拉取 PCM → Vosk 识别 → 回灌结果到板端

**方式 B：手动步骤**

```bash
# 1. 拉取录音文件
adb pull /data/qiban_voice/recordings/voice_XXXXX.pcm /tmp/test.pcm

# 2. 本机识别
./tools/audio/qiban_asr_local.sh /tmp/test.pcm

# 3. 回灌识别结果
adb shell qiban_voice_service import-asr "导航到清华大学"
```

### 9.4 验证导航触发

```bash
# 板端
cat /data/qiban_nav_state.json
```

**预期**：`active: true`, `destination: "清华大学"`

---

## 十、TTS 链路测试（需主机配合）

### 10.1 板端发起 TTS 请求

```bash
qiban_voice_service speak "前方五十米右转"
```

### 10.2 主机侧 TTS 合成

**方式 A：自动 worker（推荐）**

```bash
./tools/audio/qiban_tts_local_worker.sh --adb-serial <设备序列号> --play-after-import
```

自动完成：读取请求文本 → Edge TTS 合成 → 推送到板端 → 触发播放

**方式 B：手动步骤**

```bash
# 1. 本机合成
./tools/audio/qiban_tts_local.sh --text "前方五十米右转" --output /tmp/tts.pcm

# 2. 推送到板端
adb push /tmp/tts.pcm /data/qiban_voice/tts/last_tts_16k_s16_mono.pcm

# 3. 板端播放
adb shell qiban_voice_service play /data/qiban_voice/tts/last_tts_16k_s16_mono.pcm
```

### 10.3 验证播放

**预期**：喇叭播放出 "前方五十米右转" 的语音。

---

## 十一、常见问题排查

### 11.1 `/dev/audio/pcm0p` 或 `/dev/audio/pcm0c` 不存在

**原因**：音频驱动未加载

**排查**：

```bash
# 检查内核配置
cat /proc/config.gz | gunzip | grep R528_AUDIO
# 应该看到 CONFIG_R528_AUDIO=y
```

**修复**：menuconfig 启用 `CONFIG_R528_AUDIO=y`

### 11.2 喇叭无声

**排查步骤**：

```bash
# 1. 检查设备是否存在
ls /dev/audio/pcm0p
ls /dev/audio/pcm0c

# 2. 检查 mixer 音量
amixer contents | grep -i volume
# 如果音量为 0，设置音量：
amixer cset name='DAC Volume' 160

# 3. 检查输出通路
amixer contents | grep -i mixer
# 确认 Left/Right Output Mixer 已打开

# 4. 检查硬件连接
# 确认喇叭/耳机已正确连接
```

### 11.3 麦克风录音文件为空

**排查步骤**：

```bash
# 1. 检查录音设备
arecord -l
# 应该看到至少 1 个设备

# 2. 检查 ADC 音量
amixer contents | grep -i adc
# 如果音量为 0，设置音量：
amixer cset name='ADC Volume' 160

# 3. 检查输入源
amixer contents | grep -i mic
# 确认 Mic 输入已选择

# 4. 尝试用 arecord 录音
arecord -D hw:0,0 -f S16_LE -r 16000 -c 1 -d 5 /data/audio_test/test.wav
# -d 5 表示录 5 秒
```

### 11.4 录音有杂音

**可能原因**：

- 增益过高 → 降低 ADC Volume
- 环境噪声 → 在安静环境重试
- 接地问题 → 检查硬件连接

### 11.5 nxrecorder 报错 "Failed to open device"

**原因**：设备被其他进程占用

**排查**：

```bash
# 检查是否有进程在使用音频设备
ps | grep audio
# 如果有，先关闭
```

### 11.6 ASR 识别不准确

**排查**：

- 确认录音参数是 16kHz/16bit/mono
- 确认录音内容清晰，无明显背景噪声
- 尝试使用更大的 ASR 模型

### 11.7 主机侧 Vosk 安装失败

```bash
# 手动安装
pip3 install vosk
# 下载模型
wget https://alphacephei.com/vosk/models/vosk-model-small-cn-0.22.zip
unzip vosk-model-small-cn-0.22.zip -d tools/audio/models/
```

---

## 十二、测试结果记录表

按顺序完成每项测试，在"结果"列填写 PASS/FAIL，并在"备注"列记录具体情况。

| 序号 | 测试项 | 命令 | 预期结果 | 实际结果 | 备注 |
|------|--------|------|----------|----------|------|
| 1 | 设备节点 | `ls /dev/audio/pcm0p` / `ls /dev/audio/pcm0c` | 文件存在 | | |
| 2 | nxplayer 可用 | `help nxplayer` | 显示帮助 | | |
| 3 | nxrecorder 可用 | `help nxrecorder` | 显示帮助 | | |
| 4 | aplay 设备 | `aplay -l` | 列出设备 | | |
| 5 | arecord 设备 | `arecord -l` | 列出设备 | | |
| 6 | mixer 状态 | `amixer` | 有控制项 | | |
| 7 | 喇叭蜂鸣 | `nxplayer → tone 16000 2 1000` | 2 秒 1kHz 声 | | |
| 8 | 麦克风录音 | `nxrecorder → recordraw → stop` | PCM 文件 > 0 | | |
| 9 | 录音回放 | `nxplayer → playraw` | 听到录音 | | |
| 10 | 意图解析 | `intent "导航到清华大学"` | NAVIGATE | | |
| 11 | listen 准备 | `listen 4` | 生成脚本 | | |
| 12 | ASR 识别 | 主机 Vosk 识别 | 返回中文文本 | | |
| 13 | TTS 合成 | 主机 Edge TTS | 生成 PCM | | |
| 14 | TTS 播放 | `play <pcm>` | 听到语音 | | |

**测试日期**：

**测试人员**：

**固件版本**：

**硬件版本**：

---

> 本文档与 `tools/audio/` 目录下的脚本配套使用。
> 详细音频工具说明参见 `tools/audio/README.md`。
