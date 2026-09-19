import router from "@system.router"
import { subscribeVehicleState } from "../../common/vehicle-state"

export default {
  private: {
    destination: "--",
    remainingDistance: "--.- km",
    eta: "--:--",
    nextTurn: "导航同步中",
    routeSteps: [],
    routeStatus: ""
  },

  onInit() {
    this.unsubscribeVehicleState = subscribeVehicleState((snapshot) => {
      this.applyNavigationState(snapshot.navigation)
    })
  },

  onDestroy() {
    if (this.unsubscribeVehicleState) {
      this.unsubscribeVehicleState()
      this.unsubscribeVehicleState = null
    }
  },

  applyNavigationState(navigation) {
    this.destination = navigation.destination
    this.remainingDistance = navigation.remainingDistance
    this.eta = navigation.eta
    this.nextTurn = navigation.nextTurn
    this.routeSteps = navigation.routeSteps
    this.routeStatus = navigation.routeStatus
  },

  goBack() {
    router.back()
  }
}
