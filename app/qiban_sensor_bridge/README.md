# qiban_sensor_bridge

`qiban_sensor_bridge` 是 `骑伴 AI 智能电动车中控屏` 的外部采集入口桥接程序。

它读取串口或标准输入中的 `key=value` 文本行，并把值原子写入
`/data/qiban_inputs/`，供 `qiban_vehicle_service` 继续聚合并导出
`/data/qiban_vehicle_state.json`。

支持字段：

- `speed_kmh`
- `battery_percent`
- `nav_remaining_m`
- `ride_duration_min`
- `total_distance_km_x10`

当前位置另走独立状态文件：

- `qiban_sensor_bridge location <longitude> <latitude> [accuracy_m]`
- 写入 `/data/qiban_location_state.json`
- 当前 `qiban_ui` 会在地图页用红点显示这个位置

示例：

```bash
qiban_sensor_bridge serve /dev/ttyS1
qiban_sensor_bridge serve -
qiban_sensor_bridge write speed_kmh 26
qiban_sensor_bridge location 112.938814 28.228209
qiban_sensor_bridge location 116.494600 39.987600 20
echo "battery_percent=81" | qiban_sensor_bridge serve -
```
