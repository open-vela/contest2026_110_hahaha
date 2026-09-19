# 板端音频自检

这组文件用于 `R528S3-Gemini-S1 + OpenVela nsh_minidisplay` 板端串口排查。

## 1. 快速探测

把 [`board_audio_probe.nsh`](/home/xjx/contest2026_110_hahaha/tools/audio/board_audio_probe.nsh) 放到板端后执行：

```sh
sh /data/board_audio_probe.nsh
```

它会做四件事：

- 列出 `/dev/audio` 节点
- 检查 `aplay / arecord / amixer / nxplayer / nxrecorder` 是否存在
- 列出播放和录音设备
- 打印后续录放测试该输入的命令

## 2. 喇叭测试

在串口里输入：

```sh
nxplayer
```

然后粘贴 [`nxplayer_tone_1k.cmd`](/home/xjx/contest2026_110_hahaha/tools/audio/nxplayer_tone_1k.cmd) 的内容：

```text
device /dev/audio/pcm0p
tone 16000 2 1000
q
```

预期结果：喇叭发出 2 秒 1 kHz 蜂鸣。

## 3. 麦克风录音

在串口里输入：

```sh
nxrecorder
```

然后粘贴 [`nxrecorder_mic_16k.cmd`](/home/xjx/contest2026_110_hahaha/tools/audio/nxrecorder_mic_16k.cmd) 的内容：

```text
device /dev/audio/pcm0c
recordraw /data/audio_test/mic_16k_s16_mono.pcm 1 16 16000 0
stop
q
```

注意：`recordraw` 之后录音会持续进行，`stop` 需要你在说话 3-5 秒后手动再输入。

## 4. 回放录音

在串口里输入：

```sh
nxplayer
```

然后粘贴 [`nxplayer_play_16k.cmd`](/home/xjx/contest2026_110_hahaha/tools/audio/nxplayer_play_16k.cmd) 的内容：

```text
device /dev/audio/pcm0p
playraw /data/audio_test/mic_16k_s16_mono.pcm 1 16 16000 0
q
```

## 5. 结果判断

- `tone` 能响：喇叭链路基本通
- `recordraw` 能生成文件，且 `playraw` 能听到刚才说话：麦克风链路基本通
- `aplay -l` 有设备但 `arecord -l` 没设备：偏向只有播放通，录音链路还没打通
- `/dev/audio/pcm0p` 或 `/dev/audio/pcm0c` 不存在：先查板级驱动或镜像配置
- 录音文件有大小但回放是静音：优先查 `amixer`、模拟麦接法、增益和通道

## 6. 可选补充

如果 `arecord` 和 `aplay` 支持常规 tinyalsa 参数，可以再补做一轮：

```sh
aplay -l
arecord -l
amixer
amixer contents
```

这一步主要用于看设备枚举和 mixer 通路，不替代上面的 `nxplayer / nxrecorder` 实测。

## 7. TTS 产物入口

板端和主机侧的职责约定如下：

1. 板端 `qiban_voice_service speak ...` 或 `qiban_voice_service announce-nav`
2. 板端写出 `/data/qiban_voice/tts/last_request.txt`
3. 主机侧或云侧真实 TTS 引擎先完成合成
4. 主机侧统一用 [`qiban_tts_worker.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_tts_worker.sh)
   把任意合成音频归一化为 `16kHz / 16-bit / mono / PCM`
5. 再把归一化结果送回板端
6. 板端执行 `qiban_voice_service import-tts <pcm_path>`

标准板端目标路径是：

```text
/data/qiban_voice/tts/last_tts_16k_s16_mono.pcm
```

主机侧标准化示例：

```bash
./tools/audio/qiban_tts_worker.sh \
  --request /tmp/last_request.txt \
  --source /tmp/tts_out.wav \
  --output /tmp/last_tts_16k_s16_mono.pcm
```

板端导入示例：

```bash
qiban_voice_service import-tts /data/tts/last_tts_16k_s16_mono.pcm
qiban_voice_service play /data/qiban_voice/tts/last_tts_16k_s16_mono.pcm
```

## 8. ASR 产物入口

推荐链路：

1. 板端执行：

```bash
qiban_voice_service listen 4
```

2. 板端会生成：

- `/data/qiban_voice/asr/last_request.json`
- `/data/qiban_voice_last_audio.json`

3. 主机侧或云侧 ASR 读取 `last_request.json`，拿到 `pcm_path`
4. 识别出中文文本后，回传：

```bash
qiban_voice_service import-asr 导航到清华大学
```

也就是说：

- “谁来把录音 PCM 变成中文汉字”：
  不是板端 `qiban_voice_service`
- 约定由主机侧 / 云侧 ASR worker 负责
- 板端只负责准备录音、接收识别结果、继续调用导航

主机侧 worker 示例：

```bash
./tools/audio/qiban_asr_worker.sh \
  --adb-serial <device_serial> \
  --recognized-text "导航到清华大学"
```

如果你已经有自己的 ASR 脚本，例如 `host_asr.sh`，它只需要：

- 接收一个 PCM 文件路径作为第一个参数
- 把 UTF-8 中文识别结果打印到 stdout

则可直接接：

```bash
./tools/audio/qiban_asr_worker.sh \
  --adb-serial <device_serial> \
  --asr-script ./host_asr.sh
```

这个 worker 会自动：

1. 从板端读取 `/data/qiban_voice/asr/last_request.json`
2. 把板端 PCM 拉到主机 `/tmp/qiban_asr/`
3. 调你的 ASR 脚本
4. 自动执行 `adb shell qiban_voice_service import-asr "<识别文本>"`

## 9. 本机 ASR / TTS 部署

这套仓库现在补了主机侧语音引擎封装，默认方案是：

- `ASR`：`Vosk` + `vosk-model-small-cn-0.22`
- `TTS`：`Edge TTS`，默认音色 `zh-CN-XiaoxiaoNeural`

特点：

- 不依赖系统 `apt`
- `ASR` 离线本地运行
- `TTS` 在本机执行，默认调用轻量在线语音合成接口，不下载超大中文声学模型
- 依赖、模型和缓存都落在 `tools/audio/` 目录下
- 仍然兼容现有 `qiban_voice_service` 的 `listen / import-asr / speak / import-tts` 协议

### 9.1 一次性安装

在仓库根目录执行：

```bash
./tools/audio/setup_local_voice.sh
```

它会做三件事：

1. 用 `tools/audio/.pydeps` 自举本地 Python 包目录
2. 安装 `vosk` 和 `edge-tts`
3. 下载默认中文 ASR 模型到 `tools/audio/models/`

如果你要替换模型地址，可先导出这些变量再执行：

```bash
export QIBAN_ASR_MODEL_URL=...
export QIBAN_ASR_MODEL_DIR=...
export QIBAN_TTS_VOICE=zh-CN-YunxiNeural
./tools/audio/setup_local_voice.sh
```

### 9.2 本机 ASR 自测

假设你已经有一个 `16k / 16bit / mono / PCM` 文件：

```bash
./tools/audio/qiban_asr_local.sh /tmp/test.pcm
```

它会把识别结果直接打印到 `stdout`。

如果要直接接板端 `listen` 请求，则执行：

```bash
./tools/audio/qiban_asr_local_worker.sh --adb-serial <device_serial>
```

它会自动：

1. 从板端取 `/data/qiban_voice/asr/last_request.json`
2. 拉取录音 PCM
3. 用本机 `Vosk` 跑中文识别
4. 自动回灌 `qiban_voice_service import-asr "<识别结果>"`

### 9.3 本机 TTS 自测

直接传文本：

```bash
./tools/audio/qiban_tts_local.sh \
  --text "前方五十米右转" \
  --output /tmp/qiban_tts.pcm
```

或者复用板端 TTS 请求文件：

```bash
./tools/audio/qiban_tts_local.sh \
  --request /tmp/last_request.txt \
  --output /tmp/qiban_tts.pcm
```

输出结果始终是：

- `16kHz`
- `16-bit`
- `mono`
- 原始 `PCM`

如果要直接接板端 `speak` 请求，则执行：

```bash
./tools/audio/qiban_tts_local_worker.sh \
  --adb-serial <device_serial> \
  --play-after-import
```

它会自动：

1. 从板端取 `/data/qiban_voice/tts/last_request.txt`
2. 用本机 `Edge TTS` 合成中文语音
3. 生成标准 PCM
4. 推回板端并执行 `qiban_voice_service import-tts ...`
5. 可选地再触发一次 `qiban_voice_service play ...`

### 9.4 主机侧常驻语音桥接

如果你现在 `ASR` 模型和 `TTS` 依赖都已经装好，下一步最实用的是把主机侧桥接脚本常驻起来：

```bash
./tools/audio/qiban_voice_local_loop.sh \
  --adb-serial <device_serial> \
  --play-after-import
```

这个脚本会持续轮询板端：

- `listen` 请求：自动调用本机 `Vosk`，再执行 `qiban_voice_service import-asr "<识别结果>"`
- `speak / announce-nav` 请求：自动调用本机 `Edge TTS`，再执行 `qiban_voice_service import-tts <pcm>`

常见调试方式：

```bash
# 只跑 ASR
./tools/audio/qiban_voice_local_loop.sh --adb-serial <device_serial> --asr-only

# 只跑 TTS
./tools/audio/qiban_voice_local_loop.sh --adb-serial <device_serial> --tts-only

# 只处理当前已有请求一次
./tools/audio/qiban_voice_local_loop.sh --adb-serial <device_serial> --once
```

板端串口这时只需要继续发起请求：

```bash
qiban_voice_service listen 4
qiban_voice_service speak 前方五十米右转
qiban_voice_service announce-nav
```

注意：

- `listen 4` 仍然只是准备录音脚本，板端还是要按前面的 `nxrecorder` 步骤先把 PCM 真正录出来
- 这版常驻桥接已经能把“板端请求 -> 主机 ASR/TTS -> 回灌板端”自动串起来
- 还没有把 `nxrecorder / nxplayer` 做成板端后台守护，这部分仍然保留手工联调方式

### 9.5 Relay Server worker

如果你不再走 `USB + adb`，而是改成“开发板 <-> relay server <-> 主机 worker”，可以直接用新增脚本：

```bash
./tools/audio/qiban_server_voice_loop.sh \
  --server-url http://<server_ip>:8787
```

单次 worker：

```bash
./tools/audio/qiban_server_asr_local_worker.sh \
  --server-url http://<server_ip>:8787

./tools/audio/qiban_server_tts_local_worker.sh \
  --server-url http://<server_ip>:8787
```

推荐板端配合方式：

```bash
qiban_voice_service server-config http://<server_ip>:8787 r528-demo-001
qiban_voice_service listen 4
# 完成 nxrecorder 真录音
qiban_voice_service submit-asr
qiban_voice_service poll-asr

qiban_voice_service speak 前方五十米右转
qiban_voice_service poll-tts
```

### 9.6 新增脚本

- [`setup_local_voice.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/setup_local_voice.sh)：安装依赖并下载默认 ASR 模型
- [`qiban_voice_env.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_voice_env.sh)：本地语音环境变量
- [`qiban_asr_local.py`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_asr_local.py)：离线 ASR 推理
- [`qiban_asr_local.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_asr_local.sh)：离线 ASR shell 入口
- [`qiban_asr_local_worker.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_asr_local_worker.sh)：板端 ASR 回灌 worker
- [`qiban_tts_local.py`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_tts_local.py)：主机侧 TTS 合成并归一化成 PCM
- [`qiban_tts_local.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_tts_local.sh)：主机侧 TTS shell 入口
- [`qiban_tts_local_worker.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_tts_local_worker.sh)：板端 TTS 回灌 worker
- [`qiban_voice_local_loop.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_voice_local_loop.sh)：主机侧常驻 ASR/TTS 自动桥接
- [`qiban_server_asr_local_worker.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_server_asr_local_worker.sh)：relay server 单次 ASR worker
- [`qiban_server_tts_local_worker.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_server_tts_local_worker.sh)：relay server 单次 TTS worker
- [`qiban_server_voice_loop.sh`](/home/xjx/contest2026_110_hahaha/tools/audio/qiban_server_voice_loop.sh)：relay server 常驻 ASR/TTS worker
