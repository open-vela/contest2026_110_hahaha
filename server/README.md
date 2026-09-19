# Qiban Relay Server

`server/` 提供一套独立于 `openvela` 编译树的中转服务骨架，面向室外联调场景下的：

- 开发板上传 `ASR` 录音任务
- 开发板提交 `TTS` 文本任务
- 主机 `ASR/TTS worker` 拉取待处理任务
- 主机回传识别文本或合成音频
- 开发板与网页端查询设备最新 `GPS / 车辆遥测`

当前版本特性：

- 运行时：`FastAPI`
- 元数据存储：`SQLite`
- 音频文件存储：`server/data/uploads/jobs/`
- 部署形态：单机骨架，支持 `Docker Compose + Caddy` 公网部署

## 目录

- `app/main.py`：HTTP API 入口
- `app/storage.py`：SQLite 和文件存储逻辑
- `app/schemas.py`：请求/响应模型
- `requirements.txt`：Python 依赖
- `run.sh`：本地快速启动脚本
- `Dockerfile`：生产镜像
- `docker-compose.yml`：公网部署编排
- `Caddyfile`：HTTPS 反向代理
- `deploy.sh`：部署辅助脚本

## 快速启动

```bash
cd /home/xjx/contest2026_110_hahaha/server
./run.sh
```

默认监听：

```text
http://0.0.0.0:8787
```

可选环境变量：

```bash
export QIBAN_SERVER_HOST=0.0.0.0
export QIBAN_SERVER_PORT=8787
export QIBAN_SERVER_DATA_DIR=/tmp/qiban_server_data
export QIBAN_SERVER_DB_PATH=/tmp/qiban_server_data/qiban.sqlite3
```

## 公网部署

推荐部署形态：

- 一台有公网 `IP` 的 Linux 云主机
- 一个解析到这台主机的域名，例如 `relay.example.com`
- `Docker` + `Docker Compose`
- `Caddy` 自动申请 `Let's Encrypt` 证书

### 1. 主机前置条件

至少确认这些条件：

- 操作系统建议 `Ubuntu 22.04/24.04`
- 已安装 `docker` 和 `docker compose`
- 安全组或云防火墙放行 `80/tcp` 和 `443/tcp`
- 域名 `A` 记录已指向云主机公网 IP

### 2. 准备环境变量

```bash
cd /home/xjx/contest2026_110_hahaha/server
cp .env.example .env
```

编辑 `.env`：

```bash
QIBAN_SERVER_DOMAIN=relay.example.com
QIBAN_ACME_EMAIL=ops@example.com
QIBAN_SERVER_HOST=0.0.0.0
QIBAN_SERVER_PORT=8787
QIBAN_SERVER_DATA_DIR=/srv/qiban-relay/data
QIBAN_SERVER_DB_PATH=/srv/qiban-relay/data/qiban_relay.sqlite3
QIBAN_SERVER_DATA_DIR_HOST=./data
```

说明：

- `QIBAN_SERVER_DOMAIN`：公网域名，`Caddy` 会用它签发 HTTPS 证书
- `QIBAN_ACME_EMAIL`：证书通知邮箱
- `QIBAN_SERVER_DATA_DIR`：容器内持久化目录
- `QIBAN_SERVER_DATA_DIR_HOST`：宿主机挂载目录

### 3. 启动服务

```bash
./deploy.sh up
```

查看状态：

```bash
./deploy.sh ps
./deploy.sh logs
```

如果域名解析、80/443 端口和证书申请都正常，公网入口就是：

```text
https://relay.example.com
https://relay.example.com/healthz
```

### 4. 板端和主机 worker 改成公网地址

板端：

```bash
qiban_voice_service server-config https://relay.example.com r528-demo-001
```

主机 worker：

```bash
./tools/audio/qiban_server_voice_loop.sh \
  --server-url https://relay.example.com
```

### 5. 常见故障

- `证书申请失败`：通常是域名没有解析到当前主机，或 `80/tcp` 没放开
- `外网访问超时`：优先检查云厂商安全组、防火墙、运营商端口策略
- `任务能创建但文件下载失败`：检查 `server/data/` 挂载权限和磁盘空间
- `容器反复重启`：先看 `./deploy.sh logs`

### 6. 当前限制

这版部署已经能公网访问，但仍然是比赛阶段骨架，当前还没有：

- 设备鉴权
- worker 鉴权
- 对象存储
- 自动清理历史任务
- 数据库高可用

如果要长期暴露到公网，下一步应优先补鉴权和限流。

## 任务模型

统一任务状态：

- `pending`：待主机 worker 领取
- `processing`：worker 已 claim
- `done`：处理完成
- `failed`：处理失败

统一字段：

- `id`
- `job_type`：`asr` 或 `tts`
- `device_id`
- `request_id`
- `request_payload`
- `result_payload`
- `request_file_path`
- `result_file_path`

## API

### 1. 创建 ASR 任务

```bash
curl -X POST http://127.0.0.1:8787/api/asr/jobs \
  -F device_id=r528-demo-001 \
  -F request_id=listen-20260727-001 \
  -F duration_s=4 \
  -F sample_rate_hz=16000 \
  -F channels=1 \
  -F bits_per_sample=16 \
  -F metadata='{"source":"qiban_voice_service"}' \
  -F audio_file=@/tmp/voice_001.pcm
```

### 2. 创建 TTS 任务

```bash
curl -X POST http://127.0.0.1:8787/api/tts/jobs \
  -H 'Content-Type: application/json' \
  -d '{
    "device_id": "r528-demo-001",
    "request_id": "tts-20260727-001",
    "text": "前方五十米右转",
    "voice": "zh-CN-XiaoxiaoNeural",
    "metadata": {"source":"announce-nav"}
  }'
```

### 3. 主机 worker 领取任务

```bash
curl "http://127.0.0.1:8787/api/jobs/pending?job_type=asr&limit=1&claim=true&worker_id=host-asr-01"
curl "http://127.0.0.1:8787/api/jobs/pending?job_type=tts&limit=1&claim=true&worker_id=host-tts-01"
```

### 4. 下载任务文件

```bash
curl -O http://127.0.0.1:8787/api/jobs/<job_id>/request-file
curl -O http://127.0.0.1:8787/api/jobs/<job_id>/result-file
```

### 5. 主机 worker 回传结果

ASR 结果：

```bash
curl -X POST http://127.0.0.1:8787/api/jobs/<job_id>/result \
  -F status=done \
  -F result_payload='{"recognized_text":"导航到清华大学"}'
```

TTS 结果：

```bash
curl -X POST http://127.0.0.1:8787/api/jobs/<job_id>/result \
  -F status=done \
  -F result_payload='{"sample_rate_hz":16000,"format":"pcm_s16le_mono"}' \
  -F result_file=@/tmp/last_tts_16k_s16_mono.pcm
```

### 6. 上报设备遥测

```bash
curl -X POST http://127.0.0.1:8787/api/device/telemetry \
  -H 'Content-Type: application/json' \
  -d '{
    "device_id": "r528-demo-001",
    "timestamp": "2026-07-27T12:00:00Z",
    "longitude": 116.331457,
    "latitude": 40.003573,
    "speed_kmh": 23.5,
    "battery_percent": 78,
    "nav_active": true,
    "metadata": {"satellites": 12}
  }'
```

查询最新遥测：

```bash
curl http://127.0.0.1:8787/api/device/r528-demo-001/latest
```

## 与板端/主机脚本的衔接建议

建议下一步这样改造现有工具链：

1. 板端 `listen` 后，把 `PCM + last_request.json` 上传到 `/api/asr/jobs`
2. 板端 `speak / announce-nav` 后，把文本上传到 `/api/tts/jobs`
3. 主机侧新增 `server worker`，替换当前 `adb worker`
4. 板端定时轮询 `/api/jobs/{id}`，拿到 `recognized_text` 或下载 `result-file`

当前仓库已经补上的脚本入口：

- 板端：
  - `qiban_voice_service server-config http://<server_ip>:8787 r528-demo-001`
  - `qiban_voice_service submit-asr`
  - `qiban_voice_service poll-asr`
  - `qiban_voice_service poll-tts`
- 主机：
  - `./tools/audio/qiban_server_asr_local_worker.sh --server-url http://<server_ip>:8787`
  - `./tools/audio/qiban_server_tts_local_worker.sh --server-url http://<server_ip>:8787`
  - `./tools/audio/qiban_server_voice_loop.sh --server-url http://<server_ip>:8787`

这版骨架先解决“异网情况下的任务中转”。设备鉴权、对象存储、消息队列、自动过期清理后续再补。
