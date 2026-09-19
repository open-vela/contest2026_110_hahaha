# qiban_voice_service

`qiban_voice_service` 是 `骑伴 AI 智能电动车中控屏` 的板端语音编排骨架。

当前实现分两层：

- `M1`：文本意图桥接
- `M2`：真实录音 / 真播放 / TTS 请求编排骨架

## M1：文本意图桥接

第一阶段保留，继续支持：

- 接收文本化语音意图
- 解析“导航到 / 取消导航 / 放大地图 / 缩小地图 / 刷新地图”
- 将意图转发给 `qiban_nav_service` 与 `qiban_map_service`
- 记录最近一次意图到 `/data/qiban_voice_last_intent.json`

基础命令：

```bash
qiban_voice_service examples
qiban_voice_service aliases
qiban_voice_service intent 导航到清华大学
qiban_voice_service intent 帮我导航到北大东门
qiban_voice_service intent 取消导航
qiban_voice_service nav 清华大学
qiban_voice_service clear
qiban_voice_service zoom in
qiban_voice_service refresh
```

## M2：真实录音 / TTS 第二阶段骨架

第二阶段这版做的是板端可执行的编排层，不伪装成完整 ASR / TTS：

- 真正落录音输出路径
- 真正落 ASR 请求路径
- 真正落 PCM 回放路径
- 真正落 TTS 文本请求路径
- 用状态文件把语音链路接到 UI / 调试工具
- 保留 `mock-asr` 文本注入口，继续复用现有导航桥接

当前还没有做的部分：

- 实时麦克风唤醒
- 真正的 ASR 引擎
- 真正的 TTS 合成引擎
- 自动把 `nxrecorder / nxplayer` 全流程托管成后台守护任务

## 新增命令

```bash
qiban_voice_service record [seconds] [pcm_path]
qiban_voice_service listen [seconds] [pcm_path]
qiban_voice_service record-play [seconds] [pcm_path]
qiban_voice_service play <pcm_path>
qiban_voice_service mock-asr <text>
qiban_voice_service import-asr <text>
qiban_voice_service speak <text>
qiban_voice_service server-config <server_url> <device_id>
qiban_voice_service submit-asr
qiban_voice_service poll-asr [job_id]
qiban_voice_service submit-tts [text]
qiban_voice_service poll-tts [job_id]
qiban_voice_service import-tts <pcm_path>
qiban_voice_service announce-nav
qiban_voice_service audio-status
qiban_voice_service tts-status
qiban_voice_service status
qiban_voice_service status intent
qiban_voice_service status audio
qiban_voice_service status tts
```

说明：

- `record`：准备一组真实录音脚本和 PCM 输出路径
- `listen`：语义上等同“准备一次语音监听录音”，便于后续接 ASR
- `record-play`：板端自动录音指定秒数，录完后回放一次录音 PCM
- `play`：准备一组真实 PCM 回放脚本
- 默认录音设备是 `/dev/audio/pcm0c`，默认播放设备是 `/dev/audio/pcm0p`
- 如需覆盖，可设置环境变量 `QIBAN_VOICE_RECORD_DEVICE` / `QIBAN_VOICE_PLAYBACK_DEVICE`
- `mock-asr <text>`：用文本模拟识别结果，再走现有意图桥接
- `import-asr <text>`：导入主机 / 云侧真实 ASR 返回的中文文本，再走现有意图桥接
- `speak <text>`：写入一份 TTS 请求文本；如果占位 PCM 已经存在，则同时准备回放脚本
- `server-config <server_url> <device_id>`：保存 relay server 地址和设备 ID
- `submit-asr`：把最近一次 `listen` 录下来的 PCM 上传到 relay server
- `poll-asr [job_id]`：查询 ASR 任务状态；任务完成后自动执行 `import-asr`
- 如果 ASR 文本是导航意图，导航转发成功后会自动生成当前导航播报并排队 TTS 任务
- `submit-tts [text]`：手动把最近一次或指定文本上传到 relay server
- `poll-tts [job_id]`：查询 TTS 任务状态；任务完成后自动下载 PCM 并执行 `import-tts`
- `import-tts <pcm_path>`：把外部真实 TTS 合成出来的 PCM 导入板端标准路径
- `announce-nav`：读取 `/data/qiban_nav_state.json`，生成一条当前导航播报的 TTS 请求

## 运行时文件

状态文件：

- `/data/qiban_voice_last_intent.json`
- `/data/qiban_voice_last_audio.json`
- `/data/qiban_voice_last_tts.json`

运行目录：

- `/data/qiban_voice/recordings/`
- `/data/qiban_voice/scripts/`
- `/data/qiban_voice/asr/`
- `/data/qiban_voice/tts/`

ASR 请求文件：

- `/data/qiban_voice/asr/last_request.json`

TTS 请求文本：

- `/data/qiban_voice/tts/last_request.txt`

占位 TTS PCM 约定路径：

- `/data/qiban_voice/tts/last_tts_16k_s16_mono.pcm`

Relay server 配置文件：

- `/data/qiban_voice_server_config.json`

最近一次 server 任务状态：

- `/data/qiban_voice_last_asr_job.json`
- `/data/qiban_voice_last_tts_job.json`

## 录音联调

先在板端准备一次录音：

```bash
qiban_voice_service record 4
```

它会生成两份脚本：

- `record_*_start.cmd`
- `record_*_stop.cmd`

板端串口操作顺序：

```bash
nxrecorder
```

然后：

1. 粘贴 `record_*_start.cmd` 的内容
2. 对着麦克风说话约 4 秒
3. 粘贴 `record_*_stop.cmd` 的内容

这样可以避免把 `recordraw` 和 `stop` 一次性喂进去，导致刚启动就立刻停录。

## Relay Server 联调

先在板端写入中转服务配置：

```bash
qiban_voice_service server-config http://<server_ip>:8787 r528-demo-001
```

### ASR 任务

板端顺序：

```bash
qiban_voice_service listen 4
# 按前文步骤完成 nxrecorder 真录音
qiban_voice_service submit-asr
qiban_voice_service poll-asr
qiban_voice_service poll-tts
qiban_voice_service play /data/qiban_voice/tts/last_tts_16k_s16_mono.pcm
```

说明：

- `listen 4` 仍然只负责准备录音路径和命令
- 真正录音完成后，再执行 `submit-asr`
- `poll-asr` 会查询 relay server；如果任务已完成，会自动执行 `import-asr`
- 如果识别文本是“导航到软件园二期”这类导航意图，导航成功后会自动排队导航播报 TTS
- `poll-tts` 负责下载合成后的播报 PCM，`play` 负责准备 `nxplayer` 回放脚本

### TTS 任务

板端顺序：

```bash
qiban_voice_service speak 前方五十米右转
qiban_voice_service poll-tts
```

如果已经配好 `server-config`，`speak` 和 `announce-nav` 会自动提交 `TTS` 任务到 relay server。

`poll-tts` 在任务完成后会自动：

1. 下载 server 返回的 PCM
2. 导入到 `/data/qiban_voice/tts/last_tts_16k_s16_mono.pcm`
3. 保留给后续 `qiban_voice_service play ...` 使用

## 主机侧 ASR 接入

建议采用这条链路：

1. 板端执行：

```bash
qiban_voice_service listen 4
```

2. 板端会写出：

- `/data/qiban_voice/asr/last_request.json`
- `/data/qiban_voice_last_audio.json`

其中 `last_request.json` 至少会给出：

- `pcm_path`
- `duration_s`
- `sample_rate_hz`
- `channels`
- `bits_per_sample`
- `callback_command`

3. 主机侧把 `pcm_path` 对应的 PCM 拉下来跑真实 ASR
4. 主机侧拿到识别结果后，回传：

```bash
qiban_voice_service import-asr 导航到清华大学
```

这样主机只负责“语音转汉字”，板端继续负责：

- 中文意图解析
- 导航服务调用
- UI 联动

如果只是联调，也可以直接跳过真实 ASR，用：

```bash
qiban_voice_service mock-asr 导航到清华大学
```

## 回放联调

假设刚才录到了一个 PCM：

```bash
qiban_voice_service play /data/qiban_voice/recordings/voice_xxx.pcm
```

板端串口再执行：

```bash
nxplayer
```

然后粘贴生成的 `play_*.cmd` 内容即可。

## mock ASR 联调

当真实 ASR 还没接上时，可以先验证“语音识别结果 -> 导航/地图”链路：

```bash
qiban_voice_service mock-asr 导航到清华大学
qiban_voice_service mock-asr 取消导航
qiban_voice_service mock-asr 放大地图
```

真实 ASR 回灌示例：

```bash
qiban_voice_service import-asr 导航到清华大学
qiban_voice_service import-asr 取消导航
```

## TTS 联调

先写一条待播报文本：

```bash
qiban_voice_service speak 前方五十米右转
```

当前行为：

- 文本会写到 `/data/qiban_voice/tts/last_request.txt`
- 状态会写到 `/data/qiban_voice_last_tts.json`
- 如果你后续把合成后的 PCM 放到 `/data/qiban_voice/tts/last_tts_16k_s16_mono.pcm`，再次执行 `speak` 就会生成可回放脚本

真实 TTS 产物入口约定：

1. 板端 `qiban_voice_service speak ...` 或 `announce-nav`
2. 板端写出 `/data/qiban_voice/tts/last_request.txt`
3. 主机侧或云侧 `qiban_tts_worker` 负责把真实 TTS 引擎产物归一化成 `16k/16bit/mono PCM`
4. 板端执行：

```bash
qiban_voice_service import-tts /data/tts/infer_out.pcm
qiban_voice_service play /data/qiban_voice/tts/last_tts_16k_s16_mono.pcm
```

也就是说：

- “谁来把 `last_request.txt` 变成 `last_tts_16k_s16_mono.pcm`”：
  不是板端 `qiban_voice_service`
- 约定由主机侧 / 云侧 TTS worker 负责
- 板端只负责发起请求、接收标准 PCM、触发播放

如果当前导航已经激活，也可以直接生成一条导航播报：

```bash
qiban_voice_service announce-nav
```

它会从 `/data/qiban_nav_state.json` 读取：

- 目的地
- 下一条转向提示
- 剩余距离
- 预计到达时间

然后自动拼成一条待播报文本，继续复用现有 TTS 请求链路。

## 现阶段推荐测试顺序

1. `qiban_voice_service record 4`
2. 用 `nxrecorder` 完成一次真实录音
3. `qiban_voice_service play <刚录下来的pcm>`
4. 用 `nxplayer` 验证喇叭播放
5. `qiban_voice_service mock-asr 导航到清华大学`
6. `qiban_voice_service announce-nav`
7. `qiban_voice_service speak 前方右转`

如果你已经接入主机侧 ASR，则可以把第 5 步替换成：

5. `qiban_voice_service listen 4`
6. 主机侧读取 `/data/qiban_voice/asr/last_request.json`
7. 主机侧回传 `qiban_voice_service import-asr 导航到清华大学`

这样可以把“录音链路、播放链路、导航桥接、TTS 请求链路”分开验证。

如果本机 `ASR / TTS` 已经装好，建议直接常驻运行：

```bash
./tools/audio/qiban_server_voice_loop.sh \
  --server-url http://<server_ip>:8787
```

这样板端发起并提交 `listen / speak / announce-nav` 后，主机侧会自动完成：

- 拉取录音 PCM
- 跑本机 `Vosk` 识别
- 回传 `recognized_text`
- 跑本机 `Edge TTS`
- 回传标准 PCM
