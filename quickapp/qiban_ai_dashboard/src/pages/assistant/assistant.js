import router from "@system.router"
import { subscribeVehicleState } from "../../common/vehicle-state"

export default {
  private: {
    micState: "待命中",
    latestIntent: "语义理解准备中",
    suggestions: [],
    reminders: [],
    boardSummary: ""
  },

  onInit() {
    this.unsubscribeVehicleState = subscribeVehicleState((snapshot) => {
      this.applyAssistantState(snapshot.assistant)
    })
  },

  onDestroy() {
    if (this.unsubscribeVehicleState) {
      this.unsubscribeVehicleState()
      this.unsubscribeVehicleState = null
    }
  },

  applyAssistantState(assistant) {
    this.micState = assistant.micState
    this.latestIntent = assistant.latestIntent
    this.suggestions = assistant.suggestions
    this.reminders = assistant.reminders
    this.boardSummary = assistant.boardSummary
  },

  goBack() {
    router.back()
  }
}
