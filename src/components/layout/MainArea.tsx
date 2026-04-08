/**
 * Main Area Component
 *
 * Chat-first layout. When no session is active, shows welcome state.
 * When viewing a subagent, shows SubagentDetailView instead of ChatView.
 */

import { Sparkles } from 'lucide-solid'
import { type Component, createMemo } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { usePlanOverlay } from '../../stores/planOverlayStore'
import { useSession } from '../../stores/session'
import type { ToolCall } from '../../types'
import { ChatView } from '../chat/ChatView'
import { PlanFullScreen } from '../chat/plan-viewer/PlanFullScreen'
import { SubagentDetailView } from '../chat/SubagentDetailView'
import { DashboardView } from '../dashboard/DashboardView'

export const MainArea: Component = () => {
  const { currentSession, messages } = useSession()
  const { dashboardVisible, viewingSubagentId, viewingPlanId, closePlanViewer } = useLayout()
  const planOverlay = usePlanOverlay()

  /** Find the tool call being viewed by scanning session messages */
  const viewedToolCall = createMemo((): ToolCall | undefined => {
    const id = viewingSubagentId()
    if (!id) return undefined
    for (const msg of messages()) {
      if (msg.toolCalls) {
        const tc = msg.toolCalls.find((tc) => tc.id === id)
        if (tc) return tc
      }
    }
    return undefined
  })

  /** Get the plan being viewed in full-screen mode */
  const viewedPlan = createMemo(() => {
    if (!viewingPlanId()) return null
    return planOverlay.activePlan()
  })

  return (
    <div class="flex flex-col h-full w-full min-w-0 bg-[var(--surface)]">
      {viewingPlanId() && viewedPlan() ? (
        <div class="flex-1 min-h-0 overflow-hidden">
          <PlanFullScreen
            plan={viewedPlan()!}
            onApprove={() => {
              planOverlay.executePlan()
              closePlanViewer()
            }}
            onRevise={() => {
              planOverlay.refinePlan()
              closePlanViewer()
            }}
            onClose={closePlanViewer}
          />
        </div>
      ) : viewingSubagentId() ? (
        <div class="flex-1 min-h-0 overflow-hidden">
          <SubagentDetailView toolCallId={viewingSubagentId()!} toolCall={viewedToolCall()} />
        </div>
      ) : dashboardVisible() ? (
        <div class="flex-1 min-h-0 overflow-hidden">
          <DashboardView />
        </div>
      ) : currentSession() ? (
        <div class="flex-1 min-h-0 overflow-hidden">
          <ChatView />
        </div>
      ) : (
        <WelcomeState />
      )}
    </div>
  )
}

/**
 * Welcome state shown when no session is active.
 */
const WelcomeState: Component = () => (
  <div class="flex-1 flex items-center justify-center h-full animate-fade-in">
    <div class="text-center animate-slide-up">
      <div
        class="
          w-16 h-16 mx-auto mb-6
          rounded-2xl
          bg-[var(--accent)]
          flex items-center justify-center
        "
        style={{ 'box-shadow': '0 0 16px rgba(139, 92, 246, 0.2)' }}
      >
        <Sparkles class="w-8 h-8 text-white" />
      </div>
      <h2 class="text-lg font-semibold text-[var(--text-primary)] mb-1">Welcome to AVA</h2>
      <p class="text-sm text-[var(--text-secondary)] mb-4">Start a new conversation to begin</p>
      <kbd class="px-2 py-1 text-xs text-[var(--text-muted)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]">
        Ctrl+N
      </kbd>
    </div>
  </div>
)
