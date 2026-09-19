const fetch = require("@system.fetch")

const SPEED_PATTERN = [18, 22, 27, 24, 31, 26, 19, 21]
const DEFAULT_TICK_INTERVAL_MS = 5000
const VEHICLE_STATE_SCHEMA_VERSION = 1
const MOCK_STATE_SOURCE = "mock:qiban_vehicle_service"
const BOARD_STATE_SOURCE = "board:qiban_vehicle_state"
const BOARD_STATE_FETCH_TIMEOUT_MS = 1500
const BOARD_STATE_PATH_CANDIDATES = [
  "file:///data/qiban_vehicle_state.json",
  "/data/qiban_vehicle_state.json"
]

const ROUTE_PLAN = {
  destination: "软件园二期",
  totalDistanceM: 4800,
  steps: [
    { thresholdM: 3800, distance: "180 米", detail: "右转进入学院路" },
    { thresholdM: 2500, distance: "1.2 公里", detail: "直行通过创业大道路口" },
    { thresholdM: 900, distance: "2.6 公里", detail: "靠左进入软件大道辅路" },
    { thresholdM: 0, distance: "终点", detail: "到达软件园二期东门" }
  ]
}

const QUICK_ACTIONS = [
  { title: "开始导航", target: "/pages/navigation" },
  { title: "语音助手", target: "/pages/assistant" },
  { title: "骑行日志", target: "/pages/assistant" },
  { title: "安全模式", target: "/pages/assistant" }
]

let listeners = []
let timer = null
let tick = 0
let snapshot = null
let fetchInFlight = false

const DEFAULT_BOARD_STATE = {
  schema_version: VEHICLE_STATE_SCHEMA_VERSION,
  source: MOCK_STATE_SOURCE,
  generated_at_epoch_s: 0,
  speed_kmh: 18,
  battery_percent: 78,
  remaining_range_km: 34,
  ride_duration_min: 0,
  total_distance_km_x10: 12,
  nav_remaining_m: ROUTE_PLAN.totalDistanceM,
  alerts: {
    overspeed: false,
    low_battery: false,
    fatigue: false
  }
}

function padNumber(value) {
  return String(value).padStart(2, "0")
}

function formatClock(date) {
  return `${padNumber(date.getHours())}:${padNumber(date.getMinutes())}`
}

function formatDuration(totalMinutes) {
  const hours = Math.floor(totalMinutes / 60)
  const minutes = totalMinutes % 60
  return `${padNumber(hours)}:${padNumber(minutes)}`
}

function formatDistanceKm(distanceM) {
  return `${(distanceM / 1000).toFixed(1)} km`
}

function formatTotalDistance(totalDistanceKmX10) {
  const integerPart = Math.floor(totalDistanceKmX10 / 10)
  const decimalPart = totalDistanceKmX10 % 10
  return `${integerPart}.${decimalPart} km`
}

function toNonNegativeInteger(value) {
  if (typeof value === "number" && Number.isInteger(value) && value >= 0) {
    return value
  }

  if (typeof value === "string" && value.trim() !== "") {
    const parsedValue = Number(value)
    if (Number.isInteger(parsedValue) && parsedValue >= 0) {
      return parsedValue
    }
  }

  return null
}

function toBoolean(value) {
  return value === true
}

function parseLegacyDistanceKmX10(value) {
  if (typeof value !== "string" || value.trim() === "") {
    return null
  }

  const parsedValue = Number(value)
  if (!Number.isFinite(parsedValue) || parsedValue < 0) {
    return null
  }

  return Math.round(parsedValue * 10)
}

function cloneAlerts(alerts) {
  return {
    overspeed: !!alerts.overspeed,
    low_battery: !!alerts.low_battery,
    fatigue: !!alerts.fatigue
  }
}

function cloneBoardState(boardState) {
  return {
    schema_version: boardState.schema_version,
    source: boardState.source,
    generated_at_epoch_s: boardState.generated_at_epoch_s,
    speed_kmh: boardState.speed_kmh,
    battery_percent: boardState.battery_percent,
    remaining_range_km: boardState.remaining_range_km,
    ride_duration_min: boardState.ride_duration_min,
    total_distance_km_x10: boardState.total_distance_km_x10,
    nav_remaining_m: boardState.nav_remaining_m,
    alerts: cloneAlerts(boardState.alerts)
  }
}

function parseRawBoardState(rawState) {
  if (typeof rawState === "string") {
    try {
      return JSON.parse(rawState)
    } catch (error) {
      return null
    }
  }

  if (!rawState || typeof rawState !== "object") {
    return null
  }

  return rawState
}

function normalizeBoardState(rawState) {
  const parsedState = parseRawBoardState(rawState)

  if (!parsedState) {
    return null
  }

  const totalDistanceKmX10 =
    toNonNegativeInteger(parsedState.total_distance_km_x10) !== null
      ? toNonNegativeInteger(parsedState.total_distance_km_x10)
      : parseLegacyDistanceKmX10(parsedState.total_distance_km)
  const speedKmh = toNonNegativeInteger(parsedState.speed_kmh)
  const batteryPercent = toNonNegativeInteger(parsedState.battery_percent)
  const remainingRangeKm = toNonNegativeInteger(parsedState.remaining_range_km)
  const rideDurationMin = toNonNegativeInteger(parsedState.ride_duration_min)
  const navRemainingM = toNonNegativeInteger(parsedState.nav_remaining_m)
  const generatedAtEpochS = toNonNegativeInteger(parsedState.generated_at_epoch_s)
  const alerts = parsedState.alerts && typeof parsedState.alerts === "object" ? parsedState.alerts : {}

  if (
    speedKmh === null ||
    batteryPercent === null ||
    batteryPercent > 100 ||
    remainingRangeKm === null ||
    rideDurationMin === null ||
    totalDistanceKmX10 === null ||
    navRemainingM === null
  ) {
    return null
  }

  return {
    schema_version: toNonNegativeInteger(parsedState.schema_version) || VEHICLE_STATE_SCHEMA_VERSION,
    source: typeof parsedState.source === "string" && parsedState.source ? parsedState.source : BOARD_STATE_SOURCE,
    generated_at_epoch_s:
      generatedAtEpochS === null ? Math.floor(Date.now() / 1000) : generatedAtEpochS,
    speed_kmh: speedKmh,
    battery_percent: batteryPercent,
    remaining_range_km: remainingRangeKm,
    ride_duration_min: rideDurationMin,
    total_distance_km_x10: totalDistanceKmX10,
    nav_remaining_m: navRemainingM,
    alerts: {
      overspeed: toBoolean(alerts.overspeed),
      low_battery: toBoolean(alerts.low_battery),
      fatigue: toBoolean(alerts.fatigue)
    }
  }
}

function stepBoardState(boardState, currentTick) {
  boardState.speed_kmh = SPEED_PATTERN[currentTick % SPEED_PATTERN.length]
  boardState.ride_duration_min += 1
  boardState.total_distance_km_x10 += Math.floor(boardState.speed_kmh / 12)

  if (boardState.nav_remaining_m > 0) {
    boardState.nav_remaining_m -= boardState.speed_kmh * 8
    if (boardState.nav_remaining_m < 0) {
      boardState.nav_remaining_m = 0
    }
  }

  if (currentTick > 0 && currentTick % 4 === 0 && boardState.battery_percent > 5) {
    boardState.battery_percent -= 1
  }

  if (boardState.battery_percent > 0) {
    boardState.remaining_range_km = Math.floor(boardState.battery_percent / 2)
  }

  boardState.alerts.overspeed = boardState.speed_kmh > 25
  boardState.alerts.low_battery = boardState.battery_percent <= 20
  boardState.alerts.fatigue = boardState.ride_duration_min >= 45
}

function createMockBoardState(currentTick) {
  const boardState = cloneBoardState(DEFAULT_BOARD_STATE)
  let index = 0

  while (index <= currentTick) {
    stepBoardState(boardState, index)
    index += 1
  }

  boardState.generated_at_epoch_s = Math.floor(Date.now() / 1000)
  return boardState
}

function createWarnings(boardState) {
  const warnings = []

  if (boardState.alerts.overspeed) {
    warnings.push({
      title: "超速提醒",
      detail: `当前速度 ${boardState.speed_kmh} km/h，建议控制在 25 km/h 内`
    })
  }

  if (boardState.alerts.low_battery) {
    warnings.push({
      title: "续航提醒",
      detail: `预计剩余里程 ${boardState.remaining_range_km} km，请尽快安排充电`
    })
  }

  if (boardState.alerts.fatigue) {
    warnings.push({
      title: "疲劳骑行提醒",
      detail: `已连续骑行 ${boardState.ride_duration_min} 分钟，建议短暂休息`
    })
  }

  if (!warnings.length) {
    warnings.push({
      title: "状态正常",
      detail: "当前未触发主动告警，车辆与导航状态稳定"
    })
  }

  return warnings
}

function createRouteSteps(boardState) {
  let currentIndex = ROUTE_PLAN.steps.length - 1

  if (boardState.nav_remaining_m > 0) {
    ROUTE_PLAN.steps.some((step, index) => {
      const nextStep = ROUTE_PLAN.steps[index + 1]

      if (boardState.nav_remaining_m > step.thresholdM) {
        currentIndex = index
        return true
      }

      if (nextStep && boardState.nav_remaining_m > nextStep.thresholdM) {
        currentIndex = index + 1
        return true
      }

      return false
    })
  }

  return ROUTE_PLAN.steps.map((step, index) => {
    let status = "待经过"

    if (boardState.nav_remaining_m === 0 && index === ROUTE_PLAN.steps.length - 1) {
      status = "已到达"
    } else if (index < currentIndex) {
      status = "已通过"
    } else if (index === currentIndex) {
      status = "当前路段"
    }

    return {
      distance: step.distance,
      detail: step.detail,
      status
    }
  })
}

function createAssistantSuggestions(boardState) {
  const suggestions = [
    "帮我导航到软件园二期",
    "查询剩余电量",
    "打开安全模式",
    "记录本次骑行"
  ]

  if (boardState.alerts.low_battery) {
    suggestions[1] = "查找附近充电点"
  }

  if (boardState.alerts.overspeed) {
    suggestions[2] = "开启限速提醒"
  }

  return suggestions
}

function createLatestIntent(boardState) {
  if (boardState.alerts.low_battery) {
    return "帮我查最近的充电点"
  }

  if (boardState.alerts.overspeed) {
    return "提醒我保持安全车速"
  }

  return "帮我导航到软件园二期"
}

function createReminderItems(boardState) {
  return createWarnings(boardState).map((warning) => {
    return {
      title: warning.title,
      detail: warning.detail
    }
  })
}

function getNextTurn(routeSteps) {
  const arrivedStep = routeSteps.find((step) => step.status === "已到达")
  if (arrivedStep) {
    return arrivedStep.detail
  }

  const activeStep = routeSteps.find((step) => step.status === "当前路段")
  if (activeStep) {
    return `前方 ${activeStep.distance}${activeStep.detail}`
  }

  if (routeSteps.length) {
    return routeSteps[routeSteps.length - 1].detail
  }

  return "导航准备中"
}

function createSnapshotFromBoardState(boardState, now) {
  const eta = new Date(now.getTime() + Math.max(boardState.nav_remaining_m, 600) * 120)
  const routeSteps = createRouteSteps(boardState)
  const warnings = createWarnings(boardState)

  return {
    boardState: cloneBoardState(boardState),
    dashboard: {
      currentTime: formatClock(now),
      speed: String(boardState.speed_kmh),
      battery: `${boardState.battery_percent}%`,
      tripDistance: formatTotalDistance(boardState.total_distance_km_x10),
      rideDuration: formatDuration(boardState.ride_duration_min),
      eta: formatClock(eta),
      nextTurn: getNextTurn(routeSteps),
      destination: `目的地：${ROUTE_PLAN.destination}`,
      warnings,
      quickActions: QUICK_ACTIONS,
      sourceLabel: `状态源：${boardState.source}`
    },
    navigation: {
      destination: ROUTE_PLAN.destination,
      remainingDistance: formatDistanceKm(boardState.nav_remaining_m),
      eta: formatClock(eta),
      nextTurn: getNextTurn(routeSteps),
      routeSteps,
      routeStatus: boardState.nav_remaining_m === 0 ? "已到达终点" : "板端状态驱动中"
    },
    assistant: {
      micState: boardState.alerts.overspeed ? "安全提醒已激活" : "待命中",
      latestIntent: createLatestIntent(boardState),
      suggestions: createAssistantSuggestions(boardState),
      reminders: createReminderItems(boardState),
      boardSummary: `速度 ${boardState.speed_kmh} km/h，电量 ${boardState.battery_percent}%，剩余 ${boardState.remaining_range_km} km`
    }
  }
}

function createSnapshot(currentTick) {
  const boardState = createMockBoardState(currentTick)
  return createSnapshotFromBoardState(boardState, new Date())
}

function notifyListeners(nextSnapshot) {
  snapshot = nextSnapshot
  listeners.forEach((listener) => {
    listener(snapshot)
  })
}

function requestBoardState(path) {
  return new Promise((resolve, reject) => {
    fetch.fetch({
      url: path,
      method: "GET",
      timeout: BOARD_STATE_FETCH_TIMEOUT_MS,
      responseType: "json",
      success: (response) => {
        resolve(response)
      },
      fail: (data, code) => {
        reject({ data, code, path })
      }
    })
  })
}

function fetchBoardStateFromCandidates(index = 0) {
  if (index >= BOARD_STATE_PATH_CANDIDATES.length) {
    return Promise.reject(new Error("board state unavailable"))
  }

  return requestBoardState(BOARD_STATE_PATH_CANDIDATES[index]).then((response) => {
    const normalizedState = normalizeBoardState(response && response.data)
    if (!normalizedState) {
      throw new Error(`invalid board state from ${BOARD_STATE_PATH_CANDIDATES[index]}`)
    }

    return normalizedState
  }).catch(() => {
    return fetchBoardStateFromCandidates(index + 1)
  })
}

function syncVehicleState() {
  if (fetchInFlight) {
    return
  }

  fetchInFlight = true

  fetchBoardStateFromCandidates().then((boardState) => {
    notifyListeners(createSnapshotFromBoardState(boardState, new Date()))
  }).catch(() => {
    notifyListeners(createSnapshot(tick))
    tick += 1
  }).then(() => {
    fetchInFlight = false
  })
}

function startTimerIfNeeded() {
  if (timer || !listeners.length) {
    return
  }

  syncVehicleState()
  timer = setInterval(syncVehicleState, DEFAULT_TICK_INTERVAL_MS)
}

function stopTimerIfIdle() {
  if (listeners.length || !timer) {
    return
  }

  clearInterval(timer)
  timer = null
  fetchInFlight = false
}

export function subscribeVehicleState(listener) {
  if (typeof listener !== "function") {
    return function noop() {}
  }

  listeners.push(listener)

  snapshot = createSnapshot(tick)
  listener(snapshot)

  startTimerIfNeeded()

  return function unsubscribe() {
    listeners = listeners.filter((current) => current !== listener)
    stopTimerIfIdle()
  }
}

export function getVehicleSnapshot() {
  if (!snapshot) {
    snapshot = createSnapshot(tick)
  }

  return snapshot
}
