# Qiban AI Agent

板端 AI 智能体框架，为"骑伴 AI 智能电动车中控屏"提供主动智能服务。

## 架构

```
┌─────────────────────────────────────────────────┐
│                qiban_ai_agent                    │
│                                                  │
│  JSON Reader ──▶ Trigger Evaluator ──▶ Action    │
│  (1s poll)       (per skill)           Executor  │
│       ▲              │                   │       │
│  /data/*.json   Skill Registry    voice_cmd.json │
│                 (static array)    ui_cmd.json     │
│  CLI Interface ◀── NSH builtin commands          │
└─────────────────────────────────────────────────┘
```

## 骑行场景 Skill

| Skill | 触发条件 | 优先级 |
|-------|---------|--------|
| overspeed | 速度 > 25 km/h | HIGH |
| low_battery | 电量 ≤ 20% | HIGH |
| fatigue | 连续骑行 ≥ 45 分钟 | NORMAL |
| nav_arrival | 到达目的地 | HIGH |
| nav_turn | 距转向点 ≤ 200m | NORMAL |
| speed_trend | 持续减速 5s+ | LOW |
| ride_summary | 停车 ≥ 2 分钟 | INFO |
| route_suggest | 无导航骑行 5min+ | LOW |
| weather_safety | 雨雪/大风天气 | NORMAL |
| emergency_stop | 2s 内速度骤降 >12km/h | EMERGENCY |

## CLI 命令

```bash
qiban_ai                  # 启动 Agent
qiban_ai status           # 状态概览
qiban_ai skill list       # Skill 列表
qiban_ai skill info <id>  # Skill 详情
qiban_ai skill enable <id>   # 启用 Skill
qiban_ai skill disable <id>  # 禁用 Skill
qiban_ai skill fire <id>     # 手动触发
qiban_ai skill set <id> <param> <value>  # 修改配置
qiban_ai state            # 当前车辆状态
qiban_ai history          # 触发历史
```

## IPC 机制

Agent 通过 JSON 文件与现有服务通信：

- **读取**: `/data/qiban_vehicle_state.json`、`/data/qiban_nav_state.json`、`/data/qiban_location_state.json`
- **写入**: `/data/qiban_voice_cmd.json`、`/data/qiban_ui_cmd.json`、`/data/qiban_nav_cmd.json`、`/data/qiban_alert_cmd.json`

## 构建

在 defconfig 中启用：
```
CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_AI_AGENT=y
```

或使用 enable 脚本：
```bash
python3 tools/openvela/enable_r528_qiban_mvp.py
```
