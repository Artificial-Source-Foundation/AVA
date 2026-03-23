import { createSignal } from 'solid-js'
import type { PlanData } from '../types/rust-ipc'

const [activePlan, setActivePlan] = createSignal<PlanData | null>(null)
const [isOpen, setIsOpen] = createSignal(false)

export function usePlanOverlay() {
  return {
    activePlan,
    isOpen,
    openPlan: (plan: PlanData) => {
      setActivePlan(plan)
      setIsOpen(true)
    },
    closePlan: () => {
      setIsOpen(false)
      setTimeout(() => setActivePlan(null), 200)
    },
  }
}
