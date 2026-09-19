import router from "@system.router"
import { subscribeVehicleState } from "../../common/vehicle-state"

export default {
  private: {
    currentTime: "--:--",
    speed: "--",
    battery: "--%",
    tripDistance: "--.- km",
    rideDuration: "--:--",
    eta: "--:--",
    nextTurn: "状态同步中",
    destination: "目的地：--",
    warnings: [],
    quickActions: [],
    sourceLabel: ""
  },

  onInit() {
    this.unsubscribeVehicleState = subscribeVehicleState((snapshot) => {
      this.applyDashboardState(snapshot.dashboard)
    })
  },

  onDestroy() {
    if (this.unsubscribeVehicleState) {
      this.unsubscribeVehicleState()
      this.unsubscribeVehicleState = null
    }
  },

  applyDashboardState(dashboard) {
    this.currentTime = dashboard.currentTime
    this.speed = dashboard.speed
    this.battery = dashboard.battery
    this.tripDistance = dashboard.tripDistance
    this.rideDuration = dashboard.rideDuration
    this.eta = dashboard.eta
    this.nextTurn = dashboard.nextTurn
    this.destination = dashboard.destination
    this.warnings = dashboard.warnings
    this.quickActions = dashboard.quickActions
    this.sourceLabel = dashboard.sourceLabel
  },

  goToPage(target) {
    router.push({
      uri: target
    })
  }
}
