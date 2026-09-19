# contest2026 OTA 方案设计

## 一、现状分析

### 1.1 硬件平台

| 项目 | 值 |
|------|-----|
| SoC | 全志 R528 双核 Cortex-A7 |
| 开发板 | 润芯微 Gemini-S1 |
| Flash | 128MB SPI NAND (`storage_type=5`) |
| 文件系统 | YAFFS2 (挂载于 `/data`) |
| 屏幕 | 2.8 寸 SPI LCD (240x320) |

### 1.2 当前启动链路

```
BROM (芯片ROM)
  → boot0 (SPL, bootloader0 分区, 1MB)
    → U-Boot 2018 (uboot-direct 分区, 4MB)
      → NuttX/openvela (bootloader 分区, 8MB)
        → rcS.nsh 启动各服务
```

### 1.3 当前 NAND 分区表 (128MB)

```
+------------------+----------+------------------------------------------+
| 分区             | 大小     | 内容                                     |
+------------------+----------+------------------------------------------+
| bootloader0      | 1 MB     | boot0 SPL                                |
| boot0-direct     | 1 MB     | boot0 直接访问                           |
| uboot-direct     | 4 MB     | U-Boot 直接访问                          |
| reserved_1       | 1 MB     | 保留                                     |
| sst              | 1.25 MB  | 安全存储                                 |
| bootloader       | 8 MB     | NuttX RTOS 固件 (nsh.fex) ← 单槽位     |
| res              | 25 MB    | 资源 (romfs: ap.fex, 字体, logo)        |
| usrdata          | ~216 MB  | 用户数据 (YAFFS2, 挂载 /data)           |
+------------------+----------+------------------------------------------+
```

**问题**: `bootloader` 和 `res` 分区都是单槽位，无 A/B 冗余。任何更新失败都会导致设备变砖。

### 1.4 已有 OTA 基础设施（未启用）

| 组件 | 位置 | 状态 |
|------|------|------|
| openvela OTA 框架 | `frameworks/system/ota` (openvela.xml:181) | 未 sync 到本地 |
| OTA 包生成脚本 | `vendor/.../build/generate_ota_package.sh` | 存在但未使用 |
| gen_ota_zip.py | `frameworks/ota/tools/gen_ota_zip.py` | 需 sync OTA 框架 |
| BL 启动脚本 OTA 支持 | `rcS.blboottee` (79-103行) | 已有 ota.zip 挂载+验签逻辑 |
| U-Boot A/B 框架 | `android_ab.c` (436行) | `CONFIG_ANDROID_AB` 未启用 |
| 构建配置引用 | `build_r528s3-evb4.config` | `config_list=bootloader,ota,ap` |
| pack 脚本 OTA 镜像 | `pack_img.sh:1203-1270` | 已注释掉 |
| SPI NOR A/B 分区 | `sys_partition_nor.fex` | 有 rtosA/rtosB，仅 NOR 模式 |

### 1.5 需要 OTA 更新的组件

| 组件 | 更新方式 | 大小估算 | 说明 |
|------|---------|---------|------|
| NuttX 内核 + 9个C服务 | 固件镜像 | ~2-4 MB | 核心固件 |
| QuickApp RPK | 运行时热替换 | ~100-500 KB | JS/UX/CSS，已有 vapp 热加载 |
| AI Agent Skills | 编译进固件或运行时加载 | ~50-100 KB | 10个 skill 文件 |
| rcS.nsh 启动脚本 | 随固件 | ~2 KB | 开机启动序列 |
| 字体/资源 | romfs (res 分区) | ~5-15 MB | 中文字体、logo 等 |

---

## 二、方案选型

### 方案对比

| 维度 | 方案 A: U-Boot A/B 双分区 | 方案 B: Recovery OTA (ota.zip) | 方案 C: 应用层 OTA |
|------|--------------------------|-------------------------------|-------------------|
| **安全性** | ★★★★★ 原子切换，失败可回滚 | ★★★☆☆ 单镜像，失败需重刷 | ★★★★☆ 仅更新应用，固件不动 |
| **实现复杂度** | ★★★☆☆ 需改分区表+U-Boot配置 | ★★☆☆☆ 利用已有基础设施 | ★☆☆☆☆ 最简单 |
| **需要 repartition** | 是 (需重刷) | 否 | 否 |
| **固件更新** | 支持 | 支持 | 不支持 |
| **应用更新** | 支持 (随固件) | 支持 (随固件) | 支持 |
| **更新失败恢复** | 自动回滚到上一版本 | 需要重新烧录 | 固件不受影响 |
| **128MB NAND 压力** | 高 (需双份固件) | 低 | 最低 |
| **适合场景** | 正式产品发布 | 比赛/快速迭代 | 开发阶段 |

### 推荐：方案 B + C 组合

**理由**：

1. **比赛时间紧迫** — 方案 A 需要修改分区表，首次需要全量重刷，风险高
2. **128MB NAND 空间有限** — 双固件分区会占用额外 8MB+25MB = 33MB，对 128MB NAND 压力大
3. **已有基础设施** — `rcS.blboottee` 已有 OTA 验签+挂载逻辑，`generate_ota_package.sh` 已存在
4. **应用层 OTA 覆盖日常迭代** — 比赛期间最频繁改动的是 UI、AI Agent、QuickApp，这些可以通过应用层 OTA 更新
5. **固件 OTA 作为补充** — 需要更新内核/驱动时，通过 ota.zip 方式更新

---

## 三、详细设计

### 3.1 整体架构

```
┌─────────────────────────────────────────────────────────┐
│                    云端 OTA 服务器                        │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ 固件 OTA 包   │  │ 应用 OTA 包   │  │ 版本管理 API   │  │
│  │ (ota.zip)    │  │ (app_ota.zip) │  │ /api/ota/*    │  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬───────┘  │
└─────────┼─────────────────┼───────────────────┼──────────┘
          │ HTTP/HTTPS       │ HTTP/HTTPS        │
          ▼                  ▼                   ▼
┌─────────────────────────────────────────────────────────┐
│                   Gemini-S1 设备端                       │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │              qiban_ota_service (新增)             │    │
│  │  ┌────────────┐  ┌─────────────┐  ┌──────────┐  │    │
│  │  │ 版本检查    │  │ 下载管理     │  │ 更新执行  │  │    │
│  │  │ check_ver() │  │ download()  │  │ apply()  │  │    │
│  │  └────────────┘  └─────────────┘  └──────────┘  │    │
│  └────────────────────────┬────────────────────────┘    │
│                           │                              │
│          ┌────────────────┼────────────────┐             │
│          ▼                ▼                ▼             │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐     │
│  │ 固件 OTA      │ │ 应用 OTA      │ │ 状态上报      │     │
│  │ /data/ota.zip│ │ /data/app/    │ │ /data/ota/   │     │
│  │ → 重启生效    │ │ → 热替换生效   │ │ status.json  │     │
│  └──────────────┘ └──────────────┘ └──────────────┘     │
└─────────────────────────────────────────────────────────┘
```

### 3.2 应用层 OTA（高频，比赛期间主要使用）

#### 3.2.1 更新内容

```
/data/app_ota/
├── manifest.json          # 版本清单
├── qiban_ui              # LVGL 仪表盘二进制
├── qiban_ai_agent        # AI Agent 二进制
├── qiban_nav_service     # 导航服务二进制
├── qiban_map_service     # 地图服务二进制
├── qiban_voice_service   # 语音服务二进制
├── qiban_vehicle_service # 车辆状态服务二进制
├── qiban_sensor_bridge   # 传感器桥接二进制
├── qiban_gps_receiver    # GPS 接收服务二进制
├── qiban_wifi_bridge     # WiFi 桥接服务二进制
├── skills/               # AI Agent Skills
│   ├── skill_overspeed.c.bin
│   ├── skill_low_battery.c.bin
│   └── ...
└── quickapp/
    └── release.rpk       # QuickApp 前端包
```

#### 3.2.2 manifest.json 格式

```json
{
  "version": "1.2.0",
  "build_time": "2026-08-06T10:00:00Z",
  "min_firmware_version": "1.0.0",
  "components": {
    "qiban_ui": {
      "version": "1.2.0",
      "sha256": "a1b2c3...",
      "size": 123456
    },
    "qiban_ai_agent": {
      "version": "1.1.0",
      "sha256": "d4e5f6...",
      "size": 45678
    }
  }
}
```

#### 3.2.3 更新流程

```
1. qiban_ota_service 轮询云端 /api/ota/check?device_id=xxx&fw_ver=1.0.0&app_ver=1.1.0
2. 服务器返回 { "app_update": { "url": "...", "version": "1.2.0", "sha256": "..." } }
3. 下载 app_ota.zip 到 /data/app_ota/download.zip
4. 校验 SHA256
5. 解压到 /data/app_ota/staging/
6. 校验 manifest.json 中每个组件的 hash
7. 原子替换：
   - 停止旧服务 (kill qiban_ui, qiban_ai, etc.)
   - 替换 /data/app_ota/ 中的二进制
   - 重启服务
8. 如果启动失败 (watchdog 超时)，回滚到旧版本
```

#### 3.2.4 关键实现

```c
/* qiban_ota_service 概要 */
#define QIBAN_OTA_DIR        "/data/app_ota"
#define QIBAN_OTA_DOWNLOAD   "/data/app_ota/download.zip"
#define QIBAN_OTA_STAGING    "/data/app_ota/staging"
#define QIBAN_OTA_STATUS     "/data/ota/status.json"
#define QIBAN_OTA_SERVER     "https://ota.example.com"
#define QIBAN_OTA_CHECK_INTERVAL_SEC  300  /* 5分钟检查一次 */

/* OTA 状态机 */
enum qiban_ota_state {
    OTA_IDLE,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_APPLYING,
    OTA_REBOOT_PENDING,
    OTA_DONE,
    OTA_FAILED,
    OTA_ROLLBACK,
};

/* 应用层 OTA 不需要重启，直接替换二进制并重启服务 */
static int apply_app_ota(const char *staging_dir);
static int rollback_app_ota(void);
static int verify_component(const char *path, const char *expected_sha256);
```

### 3.3 固件层 OTA（低频，内核/驱动变更时使用）

#### 3.3.1 利用已有 rcS.blboottee 基础设施

`rcS.blboottee` 已经实现了：

```bash
# 已有逻辑 (rcS.blboottee:79-103)
if [ -e /data/ota.zip ]; then
    zip_verify /data/ota.zip /etc/key.avb    # AVB 签名验证
    mount -t zipfs -o /data/ota.zip /ota      # 挂载 OTA 包
    boot /ota/vela_ota.bin                    # 启动 OTA 镜像
fi
```

#### 3.3.2 固件 OTA 包结构

```
ota.zip
├── vela_ota.bin          # 完整 NuttX 固件镜像 (LZ4 压缩)
├── manifest.json         # 版本信息
├── res.fex               # 资源分区镜像 (可选)
└── META-INF/
    └── avb/
        └── hash.sha256   # AVB 签名数据
```

#### 3.3.3 固件 OTA 流程

```
1. qiban_ota_service 检测到固件更新
2. 下载 ota.zip 到 /data/ota.zip
3. 校验文件完整性 (SHA256)
4. 设置标志位 /data/ota_pending = 1
5. 重启设备
6. BL 启动时 rcS.blboottee 检测到 /data/ota.zip
7. zip_verify 验签
8. 挂载 ota.zip，启动 vela_ota.bin
9. OTA 镜像执行更新：
   - 写入 bootloader 分区
   - 写入 res 分区 (如有)
   - 清除 /data/ota.zip
   - 设置 /data/ota_success = 1
   - 再次重启
10. 正常启动，确认 ota_success 标志
```

### 3.4 云端 OTA 服务设计

#### 3.4.1 API 接口

复用已有的 `server/` 基础设施 (FastAPI + SQLite)，新增 OTA 模块：

```
GET  /api/ota/check
     ?device_id=gemini-s1-001
     &fw_version=1.0.0
     &app_version=1.1.0
     → { "firmware_update": {...}, "app_update": {...}, "no_update": null }

GET  /api/ota/firmware/latest
     → 固件 OTA 包下载

GET  /api/ota/app/latest
     → 应用 OTA 包下载

POST /api/ota/device/{id}/status
     Body: { "current_fw": "1.0.0", "current_app": "1.1.0", "last_update": "..." }
     → 上报设备当前版本
```

#### 3.4.2 版本号规则

```
固件版本: <major>.<minor>.<patch> (如 1.0.0)
应用版本: <major>.<minor>.<patch> (如 1.2.0)
版本比较: 语义化版本 (semver)
最低固件要求: 应用 OTA 包可声明 min_firmware_version
```

### 3.5 安全设计

| 安全措施 | 固件 OTA | 应用 OTA |
|---------|---------|---------|
| 传输加密 | HTTPS | HTTPS |
| 包签名 | AVB (已有) | SHA256 + HMAC |
| 完整性校验 | zip_verify (已有) | manifest.json 逐组件校验 |
| 回滚保护 | 版本号单调递增 | 版本号单调递增 |
| 防降级 | 检查 version > current | 检查 version > current |

### 3.6 断电恢复策略

| 阶段 | 断电后果 | 恢复方式 |
|------|---------|---------|
| 下载中 | 下载中断 | 重新下载，支持断点续传 (HTTP Range) |
| 校验中 | 校验中断 | 删除临时文件，重新开始 |
| 应用 OTA 替换中 | 服务可能异常 | watchdog 超时 → 自动回滚旧版本 |
| 固件 OTA 写入中 | 固件损坏 | ota.zip 仍在 /data，重启后重试 |
| 固件 OTA 写入完成 | 正常 | 清除 ota.zip，标记成功 |

---

## 四、分区表变更（可选，未来产品化时考虑）

如果后续需要更安全的固件 OTA，可将 NAND 分区改为 A/B 双分区：

```
+------------------+----------+------------------------------------------+
| 分区             | 大小     | 内容                                     |
+------------------+----------+------------------------------------------+
| bootloader0      | 1 MB     | boot0 SPL                                |
| boot0-direct     | 1 MB     | boot0 直接访问                           |
| uboot-direct     | 4 MB     | U-Boot 直接访问                          |
| reserved_1       | 1 MB     | 保留                                     |
| sst              | 1.25 MB  | 安全存储                                 |
| bootloader_a     | 4 MB     | NuttX Slot A ← 缩小到 4MB              |
| bootloader_b     | 4 MB     | NuttX Slot B ← 新增                     |
| res_a            | 12 MB    | 资源 Slot A ← 缩小                      |
| res_b            | 12 MB    | 资源 Slot B ← 新增                      |
| usrdata          | ~187 MB  | 用户数据 (YAFFS2) ← 缩小               |
+------------------+----------+------------------------------------------+
```

**注意**: 此方案需要：
1. 启用 U-Boot `CONFIG_ANDROID_AB`
2. 修改 `sys_partition.fex`
3. 首次更新需要全量重刷
4. `/data` 空间减少 ~29MB

**建议比赛阶段不采用此方案，仅作为产品化路线图。**

---

## 五、实施计划

### 阶段 1: 应用层 OTA (比赛期间) — 1-2 天

- [x] 创建 `app/qiban_ota_service/` — OTA 服务主体
- [x] 实现版本检查 (HTTP GET 轮询)
- [x] 实现应用包下载 + SHA256 校验
- [x] 实现服务停止 → 替换 → 重启 流程
- [x] 实现 watchdog 回滚机制 (rollback 命令)
- [x] 在 `server/app/main.py` 新增 OTA API (`/api/ota/check`, `/api/ota/app/download`, `/api/ota/device/status`)
- [x] 在 `enable_r528_qiban_mvp.py` 中启用 OTA 服务
- [x] 创建 `tools/ota/build_ota.py` OTA 包构建脚本
- [ ] 测试: 修改一个服务 → 打包 → 部署 → 设备自动更新

### 阶段 2: 固件 OTA (比赛后/产品化) — 3-5 天

- [x] 创建 `tools/ota/build_firmware_ota.py` 固件 OTA 包构建脚本
- [x] 实现两种模式：recovery 模式 (ota.zip) + direct 模式 (MTD 直写)
- [x] 在 `qiban_ota_service` 中集成固件 OTA 逻辑 (`firmware_check`, `firmware_apply`, `firmware_local`)
- [x] 在 `server/app/main.py` 新增固件 OTA API (`/api/ota/firmware/check`, `/api/ota/firmware/download`)
- [x] 利用已有 rcS.blboottee OTA 机制 (检测 /data/ota.zip → 验签 → boot recovery)
- [ ] sync `frameworks/system/ota` 框架代码 (获取 gen_ota_zip.py)
- [ ] 启用 `pack_img.sh` 中注释掉的 OTA 镜像生成
- [ ] 配置 AVB 签名密钥
- [ ] 端到端测试: 构建固件 OTA 包 → 下载 → 重启 → BL 自动更新

### 阶段 3: A/B 双分区 (产品化) — 1-2 周

- [ ] 重新设计 NAND 分区表
- [ ] 启用 U-Boot `CONFIG_ANDROID_AB`
- [ ] 实现 `ab_select_slot()` 选择逻辑
- [ ] 实现固件原子切换
- [ ] 全量重刷后验证 A/B 切换

---

## 六、文件清单

### 已新增的文件 (阶段1+2)

```
app/qiban_ota_service/
├── Makefile
├── Make.defs
├── Kconfig
├── CMakeLists.txt
├── README.md
└── qiban_ota_service_main.c      # OTA 服务主程序 (含固件OTA: firmware_check/apply/local)

tools/ota/
├── build_ota.py                  # 应用层 OTA 包构建脚本
└── build_firmware_ota.py         # 固件 OTA 包构建脚本 (recovery + direct 两种模式)

server/data/ota/packages/
├── latest/manifest.json          # 应用 OTA manifest
└── firmware/                     # 固件 OTA 包存放目录

docs/
├── OTA_DESIGN.md                 # OTA 方案设计文档
└── OTA_IMPLEMENTATION.md         # 实现记录文档
```

### 已修改的文件 (阶段1+2)

```
tools/openvela/enable_r528_qiban_mvp.py   # 添加 OTA 服务 CONFIG + rcS 启动
server/app/main.py                         # 注册 OTA 路由 (app + firmware)
server/app/schemas.py                      # 新增 OTA 数据模型
server/app/config.py                       # 新增 OTA 目录配置
```

### 可以复用的现有文件

```
vendor/.../build/generate_ota_package.sh   # OTA 包生成 (已有)
vendor/.../rcS.blboottee                   # BL 启动 OTA 逻辑 (已有，检测 /data/ota.zip)
vendor/.../rcS.ota                         # OTA recovery 脚本 (已有，执行 /ota/ota.sh)
vendor/.../sys_partition.fex               # 分区表 (已有)
openvela.xml:181                           # OTA 框架引用 (已有)
```
