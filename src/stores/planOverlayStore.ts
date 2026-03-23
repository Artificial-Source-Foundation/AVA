import { createSignal } from 'solid-js'
import type { PlanData, PlanStep } from '../types/rust-ipc'

const [activePlan, setActivePlan] = createSignal<PlanData | null>(null)
const [isOpen, setIsOpen] = createSignal(false)
const [pendingExecution, setPendingExecution] = createSignal<{
  plan: PlanData
  mode: 'code' | 'praxis'
} | null>(null)
const [stepComments, setStepComments] = createSignal<Record<string, string>>({})
const [commentingStepId, setCommentingStepId] = createSignal<string | null>(null)
const [stepLabels, setStepLabels] = createSignal<Record<string, string[]>>({})
const [previousPlan, setPreviousPlan] = createSignal<PlanData | null>(null)
const [showDiff, setShowDiff] = createSignal(false)

// Load plan from URL hash on startup (plan sharing).
// Deferred to allow the SolidJS component tree to mount first.
if (typeof window !== 'undefined') {
  const hash = window.location.hash
  if (hash.startsWith('#plan=')) {
    setTimeout(() => {
      try {
        const encoded = hash.slice(6)
        const json = decodeURIComponent(escape(atob(encoded)))
        const plan = JSON.parse(json) as PlanData
        if (plan?.summary && Array.isArray(plan.steps)) {
          setActivePlan(plan)
          setIsOpen(true)
          history.replaceState(null, '', window.location.pathname)
        }
      } catch {
        // ignore malformed plan hash
      }
    }, 500)
  }
}

export function usePlanOverlay() {
  return {
    activePlan,
    isOpen,
    pendingExecution,
    stepComments,
    commentingStepId,
    stepLabels,
    previousPlan,
    showDiff,
    toggleDiff: () => setShowDiff((prev) => !prev),
    hasDiff: () => previousPlan() !== null,
    openPlan: (plan: PlanData) => {
      const current = activePlan()
      if (current) {
        setPreviousPlan(current)
      }
      setActivePlan(plan)
      setIsOpen(true)
    },
    closePlan: () => {
      setIsOpen(false)
      setTimeout(() => {
        setActivePlan(null)
        setStepComments({})
        setCommentingStepId(null)
        setStepLabels({})
        setPreviousPlan(null)
        setShowDiff(false)
      }, 200)
    },
    executePlan: (mode: 'code' | 'praxis') => {
      const plan = activePlan()
      if (plan) {
        setPendingExecution({ plan, mode })
        setIsOpen(false)
        setTimeout(() => {
          setActivePlan(null)
          setStepComments({})
          setCommentingStepId(null)
          setPreviousPlan(null)
          setShowDiff(false)
        }, 200)
      }
    },
    consumeExecution: () => {
      const exec = pendingExecution()
      setPendingExecution(null)
      return exec
    },
    refinePlan: () => {
      // Just close — agent stays in Plan mode
      setIsOpen(false)
      setTimeout(() => {
        setActivePlan(null)
        setStepComments({})
        setCommentingStepId(null)
        setStepLabels({})
      }, 200)
    },
    addStepComment: (stepId: string, comment: string) => {
      setStepComments((prev) => ({ ...prev, [stepId]: comment }))
    },
    toggleStepComment: (stepId: string) => {
      setCommentingStepId((prev) => (prev === stepId ? null : stepId))
    },
    clearComments: () => {
      setStepComments({})
      setCommentingStepId(null)
    },
    updateStep: (stepId: string, updates: Partial<PlanStep>) => {
      const plan = activePlan()
      if (!plan) return
      setActivePlan({
        ...plan,
        steps: plan.steps.map((s) => (s.id === stepId ? { ...s, ...updates } : s)),
      })
    },
    moveStep: (stepId: string, direction: 'up' | 'down') => {
      const plan = activePlan()
      if (!plan) return
      const idx = plan.steps.findIndex((s) => s.id === stepId)
      if (idx < 0) return
      const newIdx = direction === 'up' ? idx - 1 : idx + 1
      if (newIdx < 0 || newIdx >= plan.steps.length) return
      const newSteps = [...plan.steps]
      ;[newSteps[idx], newSteps[newIdx]] = [newSteps[newIdx], newSteps[idx]]
      setActivePlan({ ...plan, steps: newSteps })
    },
    addStepLabel: (stepId: string, labelId: string) => {
      setStepLabels((prev) => ({
        ...prev,
        [stepId]: [...(prev[stepId] || []), labelId].filter((v, i, a) => a.indexOf(v) === i),
      }))
    },
    removeStepLabel: (stepId: string, labelId: string) => {
      setStepLabels((prev) => ({
        ...prev,
        [stepId]: (prev[stepId] || []).filter((l) => l !== labelId),
      }))
    },
    toggleStepApproval: (stepId: string) => {
      const plan = activePlan()
      if (!plan) return
      setActivePlan({
        ...plan,
        steps: plan.steps.map((s) => (s.id === stepId ? { ...s, approved: !s.approved } : s)),
      })
    },
    /** Mark a step as complete (called when PlanStepComplete event arrives). */
    markStepComplete: (stepId: string) => {
      const plan = activePlan()
      if (!plan) return
      setActivePlan({
        ...plan,
        steps: plan.steps.map((s) => (s.id === stepId ? { ...s, approved: true } : s)),
      })
    },
  }
}
