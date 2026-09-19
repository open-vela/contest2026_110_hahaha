# qiban_vehicle_service

板端车辆状态服务骨架，用于给 `骑伴 AI 智能电动车中控屏` 提供本地运行的数据源和调试入口。

## 当前目标

- 在 openvela 板端提供一个可执行程序 `qiban_vehicle_service`
- 先模拟速度、电量、里程、骑行时长、导航剩余距离等关键状态
- 支持导出状态文件，作为后续接入快应用界面、`ai_agent` 和自定义 Skill 的桥接点
- 如果板级已注册 `/dev/adc0`，优先读取 `GPADC` 作为电量输入
- 支持从 `/data/qiban_inputs/` 读取外部采集器写入的传感器值，作为后续接入 UART / BMS / GNSS 的统一入口

## 运行方式

先在 `menuconfig` 中启用：

```text
LVX_USE_DEMO_CONTEST2026_110_QIBAN_VEHICLE_SERVICE
```

启用后可在 `nsh` 中使用：

```bash
qiban_vehicle_service once
qiban_vehicle_service once --verbose
qiban_vehicle_service monitor
qiban_vehicle_service monitor [samples] [interval_ms] --verbose
qiban_vehicle_service export /data/qiban_vehicle_state.json
qiban_vehicle_service serve
qiban_vehicle_service serve [path] [interval_ms] --verbose
```

`once`、`monitor` 和 `serve` 默认不再向串口打印速度、电量等遥测值，避免 UI 首页运行时刷屏；需要手动调试时加 `--verbose` 恢复输出。

建议先在 `nsh` 中验证 ADC 是否已注册：

```bash
ls /dev/adc*
```

如果后续有独立采集线程、串口解析程序或板级守护进程，只需要向下面这些文件写入整数值，`qiban_vehicle_service serve` 就会自动接入：

```text
/data/qiban_inputs/speed_kmh
/data/qiban_inputs/battery_percent
/data/qiban_inputs/nav_remaining_m
/data/qiban_inputs/ride_duration_min
/data/qiban_inputs/total_distance_km_x10
```

优先级说明：

- `battery_percent` 文件存在时，优先使用文件值
- 否则尝试读取 `/dev/adc0`
- 都不可用时回退到内置模拟状态

## 后续扩展方向

- 将模拟状态替换为真实 `GPIO / ADC / UART` 采集
- 将状态文件或内存状态接入 `ai_agent`
- 根据超速、低电量、疲劳骑行等条件触发本地 Skill
