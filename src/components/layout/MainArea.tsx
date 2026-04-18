/**
 * Main Area Component
 *
 * Chat-first layout. When no session is active, shows welcome state.
 * When viewing a subagent, shows SubagentDetailView instead of ChatView.
 */

import { Sparkles } from 'lucide-solid'
import { type Component, createMemo } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useLayout } from '../../stores/layout'
import { type PlanAnnotation, usePlanOverlay } from '../../stores/planOverlayStore'
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
  const agent = useAgent()

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

  /** Serialize annotations to feedback text format (matches PlanOverlay pattern) */
  const serializeAnnotations = (annotations: PlanAnnotation[] | undefined): string => {
    if (!annotations || annotations.length === 0) return ''
    const parts: string[] = []
    for (const ann of annotations) {
      if (ann.type === 'deletion') {
        parts.push(`[DELETE] "${ann.originalText}"`)
      } else if (ann.type === 'comment') {
        parts.push(`[COMMENT on "${ann.originalText.slice(0, 60)}..."] ${ann.commentText ?? ''}`)
      } else if (ann.type === 'global_comment') {
        parts.push(`[GLOBAL COMMENT] ${ann.commentText ?? ''}`)
      }
    }
    return parts.join('\n')
  }

  return (
    <div class="flex flex-col h-full w-full min-w-0 bg-[var(--surface)]">
      {viewingPlanId() && viewedPlan() ? (
        <div class="flex-1 min-h-0 overflow-hidden">
          <PlanFullScreen
            plan={viewedPlan()!}
            previousPlan={planOverlay.previousPlan()}
            showDiff={planOverlay.showDiff()}
            hasDiff={planOverlay.hasDiff()}
            onToggleDiff={planOverlay.toggleDiff}
            onApprove={async () => {
              const plan = viewedPlan()
              if (plan && agent) {
                try {
                  // Resolve plan back to agent (matching PlanOverlay pattern)
                  await agent.resolvePlan('approved', plan, undefined, {})
                  // Only proceed if resolve succeeded
                  planOverlay.executePlan()
                  closePlanViewer()
                } catch {
                  // Keep viewer open on failure - user can retry
                }
              } else {
                // No agent/plan - proceed with local-only flow
                planOverlay.executePlan()
                closePlanViewer()
              }
            }}
            onRevise={async (annotations: PlanAnnotation[]) => {
              const plan = viewedPlan()
              const feedback = serializeAnnotations(annotations)
              if (plan && agent && feedback) {
                try {
                  // Send feedback with annotations back to agent
                  await agent.resolvePlan('rejected', undefined, feedback, {})
                  // Only proceed if resolve succeeded
                  planOverlay.refinePlan(annotations)
                  closePlanViewer()
                } catch {
                  // Keep viewer open on failure - user can retry
                }
              } else {
                // No agent/plan/feedback - proceed with local-only flow
                planOverlay.refinePlan(annotations)
                closePlanViewer()
              }
            }}
            onClose={() => {
              // Close fullscreen viewer AND underlying overlay so users return to chat
              closePlanViewer()
              planOverlay.closePlan()
            }}
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
