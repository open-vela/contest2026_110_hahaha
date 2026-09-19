# 应用层 OTA 服务实现记录

> 实现日期：2026-08-06
> 实现阶段：Phase 1 — 应用层 OTA（比赛期间）

---

## 一、实现概览

为 contest2026 骑伴 AI 智能电动车中控屏项目实现了完整的应用层 OTA（Over-The-Air）更新服务。该服务可以远程更新 9 个 C 服务二进制、QuickApp 前端和 AI Agent Skills，无需全量重刷固件。

### 核心决策

| 决策 | 选择 | 原因 |
|------|------|------|
| OTA 类型 | 应用层 OTA + 固件 OTA 组合 | 128MB NAND 空间有限，A/B 双分区多占 ~33MB |
| A/B 双分区 | 不采用（比赛阶段） | 改分区表需全量重刷，风险高，时间紧 |
| 固件 OTA | 复用已有 rcS.blboottee | 已有 ota.zip 验签+挂载+启动逻辑 |
| HTTP 客户端 | 纯 socket 实现 | 不依赖 curl，减少固件体积 |
| SHA-256 | 内置实现 | 无外部 crypto 依赖 |

---

## 二、文件清单

### 新增文件

```
app/qiban_ota_service/
├── qiban_ota_service_main.c    # OTA 服务主程序（~700行）
├── Makefile                    # NuttX 构建规则
├── Make.defs                   # 注册到 CONFIGURED_APPS
├── Kconfig                     # CONFIG 定义
├── CMakeLists.txt              # CMake 构建
└── README.md                   # 使用文档

tools/ota/
└── build_ota.py                # OTA 包构建脚本

server/data/ota/packages/latest/
└── manifest.json               # 示例 OTA manifest

docs/
├── OTA_DESIGN.md               # OTA 方案设计文档
└── OTA_IMPLEMENTATION.md       # 本文档
```

### 修改文件

| 文件 | 变更内容 |
|------|---------|
| `tools/openvela/enable_r528_qiban_mvp.py` | 新增 `CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE=y`，rcS 新增 `qiban_ota_service &` |
| `server/app/main.py` | 新增 3 个 OTA API 路由 |
| `server/app/schemas.py` | 新增 5 个 OTA Pydantic 模型 |
| `server/app/config.py` | 新增 `ota_dir`、`ota_packages_dir` 属性 |

---

## 三、设备端 OTA 服务

### 3.1 架构

```
qiban_ota_service (C 程序, NuttX 应用)
│
├── 守护进程模式 (daemon)
│   └── 每 300s 轮询 relay server
│
├── HTTP 客户端 (纯 socket)
│   ├── GET /api/ota/check     → 版本检查
│   └── GET /api/ota/app/download → 下载 OTA 包
│
├── SHA-256 校验 (内置实现)
│
├── 服务管理
│   ├── stop:  killall 9个服务
│   └── start: 重启服务
│
└── 状态持久化
    ├── /data/ota/status.json     → OTA 状态
    └── /data/app_ota/version.json → 当前版本
```

### 3.2 核心实现细节

**SHA-256 实现**（`qiban_ota_service_main.c`）
- 完整的 SHA-256 算法，无外部依赖
- 用于校验下载的 OTA 包完整性
- 函数：`sha256_init()` → `sha256_update()` → `sha256_final()`
- 文件级封装：`ota_sha256_file(path, hex_out)`

**HTTP 客户端**（纯 socket 实现）
- `ota_http_get()`: 发送 GET 请求，返回响应
- `ota_http_download()`: 下载文件，自动跳过 HTTP header
- 支持 HTTP/1.0 协议，Connection: close

**版本比较**（semver）
- `ota_parse_version()`: 解析 "major.minor.patch"
- `ota_compare_version()`: 比较两个版本号
- `ota_check_update()`: 检查服务器是否有新版本

**服务管理**
- `ota_stop_services()`: 通过 killall 停止 9 个服务
- `ota_start_services()`: 重启所有服务
- 服务列表：qiban_ui, qiban_ai_agent, qiban_nav_service, qiban_map_service, qiban_voice_service, qiban_vehicle_service, qiban_sensor_bridge, qiban_gps_receiver, qiban_wifi_bridge

**原子文件写入**
- 与项目其他服务一致的 `.tmp` + `rename()` 模式
- 防止断电导致文件损坏

### 3.3 CLI 命令

```bash
# 守护进程模式（默认，每 300s 轮询）
qiban_ota_service &

# 一次性版本检查
qiban_ota_service check

# 打印当前 OTA 状态
qiban_ota_service status

# 应用本地 OTA 包
qiban_ota_service apply /data/app_ota/download.zip

# 回滚到上一版本
qiban_ota_service rollback

# 帮助
qiban_ota_service help
```

### 3.4 环境变量配置

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `QIBAN_OTA_SERVER` | `10.0.0.1` | Relay server IP |
| `QIBAN_OTA_PORT` | `8787` | Relay server 端口 |
| `QIBAN_OTA_INTERVAL` | `300` | 轮询间隔（秒） |

### 3.5 文件布局

```
/data/app_ota/
├── version.json          # 当前版本 {"version": "1.0.0", "updated_at": "..."}
├── download.zip          # 下载的 OTA 包
├── staging/              # 临时解压目录
└── backup/               # 备份的旧版本二进制

/data/ota/
├── status.json           # OTA 状态（UI/Agent 可读取）
└── ota.zip               # 固件 OTA 包（bootloader 读取）
```

### 3.6 OTA 状态机

```
idle → checking → downloading → verifying → applying → done
                ↓                    ↓           ↓
              idle               failed       rollback
                                   ↓
                                 idle
```

---

## 四、云端 OTA API

### 4.1 新增端点

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/ota/check` | GET | 检查更新 |
| `/api/ota/app/download` | GET | 下载 OTA 包 |
| `/api/ota/device/status` | POST | 设备状态上报 |

### 4.2 版本检查 API

```
GET /api/ota/check?device_id=gemini-s1&app_version=1.0.0

Response:
{
  "device_id": "gemini-s1",
  "current_app_version": "1.0.0",
  "app_update": {
    "version": "1.1.0",
    "build_time": "2026-08-06T10:00:00Z",
    "download_url": "/api/ota/app/download?version=1.1.0",
    "sha256": "a1b2c3...",
    "size": 123456,
    "min_firmware_version": "1.0.0",
    "changelog": "Bug fixes and improvements"
  },
  "firmware_update": null
}
```

### 4.3 Pydantic 模型

```python
class OTAComponentInfo(BaseModel):
    name: str
    version: str
    sha256: str
    size: int

class OTAUpdateInfo(BaseModel):
    version: str
    build_time: str
    download_url: str
    sha256: str
    size: int
    min_firmware_version: str | None = None
    components: list[OTAComponentInfo] = []
    changelog: str = ""

class OTACheckResponse(BaseModel):
    device_id: str
    current_app_version: str
    app_update: OTAUpdateInfo | None = None
    firmware_update: OTAUpdateInfo | None = None

class OTADeviceStatusRequest(BaseModel):
    device_id: str
    current_fw_version: str = "0.0.0"
    current_app_version: str = "0.0.0"
    last_update_time: datetime | None = None
    state: str = "idle"

class OTADeviceStatusResponse(BaseModel):
    device_id: str
    registered_at: datetime
```

---

## 五、OTA 包构建工具

### 5.1 使用方法

```bash
python3 tools/ota/build_ota.py \
  --version 1.1.0 \
  --build-dir <openvela编译输出目录> \
  --out-dir ./ota_output \
  --min-fw 1.0.0 \
  --changelog "修复导航显示问题"
```

### 5.2 构建流程

1. 扫描编译输出目录，查找 9 个服务二进制
2. 计算每个组件的 SHA-256
3. 生成 `manifest.json`
4. 打包为 `app_ota.zip`
5. 计算 zip 文件的 SHA-256

### 5.3 OTA 包结构

```
app_ota.zip
├── bin/
│   ├── qiban_ui
│   ├── qiban_vehicle_service
│   ├── qiban_nav_service
│   ├── qiban_map_service
│   ├── qiban_voice_service
│   ├── qiban_ai_agent
│   ├── qiban_sensor_bridge
│   ├── qiban_gps_receiver
│   └── qiban_wifi_bridge
└── manifest.json
```

---

## 六、构建系统集成

### 6.1 Kconfig 配置

```
config LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE
    bool "Qiban OTA update service"
    default n
    ---help---
        Enable the application-layer OTA service for the contest 2026
        team 110 Qiban AI dashboard.
```

### 6.2 Makefile 配置

```makefile
PROGNAME  = qiban_ota_service
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 8192
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE)
MAINSRC   = qiban_ota_service_main.c
```

### 6.3 开机自启

`enable_r528_qiban_mvp.py` 会自动：
1. 在 defconfig 中添加 `CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE=y`
2. 在 rcS.nsh 中添加 `qiban_ota_service &`

启动顺序（rcS.nsh）：
```
qiban_vehicle_service serve &
qiban_nav_service watch &
qiban_gps_receiver &
qiban_wifi_bridge &
qiban_ai &
qiban_ota_service &    ← OTA 服务在 UI 之前启动
qiban_ui &
```

---

## 七、使用流程

### 7.1 首次部署

```bash
# 1. 启用所有服务（包括 OTA）
python3 tools/openvela/enable_r528_qiban_mvp.py --openvela-root .

# 2. 编译固件
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ -j8

# 3. 打包烧录
cd vendor/allwinnertech/lichee/ && source envsetup.sh && lunch_nuttx && pack
```

### 7.2 发布更新

```bash
# 1. 构建 OTA 包
python3 tools/ota/build_ota.py \
  --version 1.1.0 \
  --build-dir <编译输出目录> \
  --out-dir ./ota_output

# 2. 部署到服务器
cp ota_output/app_ota.zip server/data/ota/packages/latest/
cp ota_output/manifest.json server/data/ota/packages/latest/

# 3. 设备自动检查更新（每 300 秒）
# 或手动触发：qiban_ota_service check
```

### 7.3 紧急回滚

```bash
# 在设备上执行
qiban_ota_service rollback
```

---

## 八、R528 硬件平台研究（OTA 相关）

### 8.1 启动链路

```
BROM (芯片 ROM)
  → boot0 (SPL, bootloader0 分区, 1MB)
    → U-Boot 2018 (uboot-direct 分区, 4MB)
      → NuttX/openvela (bootloader 分区, 8MB)
        → rcS.nsh 启动各服务
```

### 8.2 NAND 分区表（128MB SPI NAND）

| 分区 | 大小 | 内容 |
|------|------|------|
| bootloader0 | 1 MB | boot0 SPL |
| boot0-direct | 1 MB | boot0 直接访问 |
| uboot-direct | 4 MB | U-Boot 直接访问 |
| reserved_1 | 1 MB | 保留 |
| sst | 1.25 MB | 安全存储 |
| bootloader | 8 MB | NuttX RTOS（单槽位） |
| res | 25 MB | 资源 (romfs) |
| usrdata | ~216 MB | 用户数据 (YAFFS2, /data) |

### 8.3 已有 OTA 基础设施（未启用）

| 组件 | 状态 | 说明 |
|------|------|------|
| rcS.blboottee OTA 逻辑 | 已存在 | 检查 /data/ota.zip → 验签 → 挂载 → 启动 |
| generate_ota_package.sh | 已存在 | 调用 gen_ota_zip.py |
| U-Boot CONFIG_ANDROID_AB | 未启用 | 有 android_ab.c 但 NAND defconfig 未激活 |
| SPI NOR A/B 分区 | 已存在 | rtosA/rtosB 各 4.25MB |
| pack_img.sh OTA 代码 | 已注释 | update_rtos + recovery-gz 镜像生成 |

### 8.4 未来产品化路线

如果需要更安全的固件 OTA，可以：
1. 启用 U-Boot `CONFIG_ANDROID_AB`
2. 修改分区表，添加 bootloader_a/b 双分区
3. 实现固件原子切换
4. 首次需要全量重刷

---

## 九、测试验证

### 9.1 编译验证

```bash
# 启用 OTA 服务
python3 tools/openvela/enable_r528_qiban_mvp.py --openvela-root . --check

# 预期输出
# Qiban R528 MVP is already enabled.
```

### 9.2 OTA 包构建验证

```bash
python3 tools/ota/build_ota.py --version 1.0.0 --build-dir <dir> --out-dir ./test_ota

# 预期输出
# [ok] qiban_ui: 123456 bytes, sha256=a1b2c3...
# [ok] qiban_vehicle_service: 45678 bytes, sha256=d4e5f6...
# ...
# OTA package: ./test_ota/app_ota.zip
#   Version: 1.0.0
#   Size: 234567 bytes
#   SHA-256: ...
#   Components: 9
```

### 9.3 设备端命令验证

```bash
# 检查版本
qiban_ota_service check

# 查看状态
qiban_ota_service status

# 预期输出
# {
#   "state": "idle",
#   "progress_pct": 0,
#   "current_version": "1.0.0",
#   "target_version": "",
#   "error_msg": "",
#   "last_check_time": "2026-08-06T10:00:00Z"
# }
```

---

## 十、已知限制

1. **HTTP 而非 HTTPS**：设备端 HTTP 客户端使用明文传输，生产环境需要 TLS
2. **无差分更新**：每次下载完整 OTA 包，未实现增量更新
3. **无签名验证**：应用层 OTA 仅用 SHA-256 校验完整性，未实现 AVB 签名
4. **服务重启中断**：更新期间服务会短暂停止，UI 会闪烁
5. **无并发保护**：多个 OTA 服务实例可能同时运行

---

## 十一、后续改进方向

### 阶段 2：固件 OTA（已实现）

**已实现：**
- `tools/ota/build_firmware_ota.py` — 固件 OTA 包构建脚本，支持两种模式：
  - **Recovery 模式**：生成 ota.zip（含 vela_ota.bin + ota.sh + firmware/），由 BL 的 rcS.blboottee 检测并启动 recovery 镜像执行更新
  - **Direct 模式**：生成 firmware.bin，由 qiban_ota_service 通过 MTD 直接写入 bootloader 分区
- `qiban_ota_service` 新增 3 个固件 OTA 命令：
  - `firmware_check` — 从服务器检查固件更新
  - `firmware_apply` — 下载 ota.zip 到 /data/ota.zip 并重启（BL 自动处理）
  - `firmware_local <zip>` — 将本地 ota.zip 复制到 /data/ota.zip 并重启
- Server 新增 2 个固件 OTA 端点：
  - `GET /api/ota/firmware/check` — 检查固件更新
  - `GET /api/ota/firmware/download` — 下载固件 ota.zip

**固件 OTA 流程（Recovery 模式）：**
```
1. qiban_ota_service firmware_apply
2. 下载 ota.zip → /data/ota.zip
3. 重启设备
4. BL (rcS.blboottee) 检测到 /data/ota.zip
5. zip_verify 验签 (AVB)
6. mount -t zipfs /data/ota.zip /ota
7. boot /ota/vela_ota.bin (OTA recovery NuttX 镜像)
8. rcS.ota 执行 /ota/ota.sh
9. ota.sh: dd if=/ota/firmware/nsh.fex of=/dev/bootloader
10. ota.sh: rm /data/ota.zip && reboot
11. 设备以新固件启动
```

**待完成：**
- sync `frameworks/system/ota` 框架（获取 gen_ota_zip.py 用于标准 OTA 包格式）
- 启用 pack_img.sh 中注释掉的 OTA 镜像生成代码
- 配置 AVB 签名密钥（当前 zip_verify 验签步骤会跳过或失败）

### 阶段 3：A/B 双分区（产品化）

- 重新设计 NAND 分区表（bootloader_a/b, res_a/b）
- 启用 U-Boot CONFIG_ANDROID_AB
- 实现固件原子切换
- 全量重刷后验证 A/B 切换

### 安全增强

- 添加 TLS 支持（mbedTLS 或 WolfSSL）
- 实现 AVB 签名验证
- 添加防降级保护（版本号单调递增）
- 实现断点续传（HTTP Range）

---

## 十二、参考文档

- `docs/OTA_DESIGN.md` — 完整 OTA 方案设计
- `app/qiban_ota_service/README.md` — OTA 服务使用说明
- `vendor/.../sys_partition.fex` — NAND 分区表
- `vendor/.../rcS.blboottee` — BL 启动 OTA 逻辑（检测 ota.zip → boot recovery）
- `vendor/.../rcS.ota` — OTA recovery 脚本（mount ota.zip → 执行 ota.sh）
- `vendor/.../generate_ota_package.sh` — OTA 包生成脚本
- `tools/ota/build_firmware_ota.py` — 固件 OTA 包构建工具（recovery + direct 模式）
- `tools/ota/build_ota.py` — 应用层 OTA 包构建工具
