# qiban_nav_service

`qiban_nav_service` 是 `骑伴 AI 智能电动车中控屏` 的板端导航入口。

当前第一版先聚焦 `M1`：

- 通过命令行输入目的地名称
- 支持附带终点经纬度直接导航
- 生成 `/data/qiban_nav_request.json` 与 `/data/qiban_nav_state.json`
- 调用 `qiban_map_service route ...` 拉取路线图
- 让 `qiban_ui` 能直接显示路线图和导航状态

当前支持的基础命令：

```bash
set QIBAN_AMAP_KEY <your-amap-key>
qiban_nav_service list
qiban_nav_service start 软件园二期
qiban_nav_service start 清华大学
qiban_nav_service start 北京大学东门
qiban_nav_service start 任意目的地 116.520481 39.986412
qiban_nav_service status
qiban_nav_service clear
```

说明：

- `start <目的地>`：优先匹配板端内置预置点表；未命中时自动调用高德地理编码接口解析坐标
- `start <目的地> <经度> <纬度>`：直接按命令行给定坐标导航

当前内置点已经包含：

- 软件园一期 / 软件园二期
- 中关村壹号
- 清华大学 / 清华东门
- 北京大学 / 北京大学东门 / 北大 / 北大东门

如果在线地理编码失败，可优先改用这些内置点，或退回到显式坐标模式。
