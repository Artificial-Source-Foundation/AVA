/**
 * Toolbar Strip
 *
 * Unified strip below the textarea with model selector, reasoning toggle,
 * and plan/act slider. Permission badge and sandbox toggle show conditionally.
 */

import { Layers, Shield } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import type { useAgent } from '../../../hooks/useAgent'
import type { useChat } from '../../../hooks/useChat'
import { useLayout } from '../../../stores/layout'
import { useSandbox } from '../../../stores/sandbox'
import type { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { PlanBranchSelector } from '../PlanBranchSelector'
import { ModelSelector } from './model-selector'
import { StatusBar } from './status-bar'
import {
  DelegationToggle,
  PermissionBadge,
  PlanActSlider,
  ReasoningDropdown,
} from './toolbar-buttons'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Thin vertical divider between strip groups */
const StripDivider: Component = () => <span class="w-px h-4 bg-[var(--border-subtle)] shrink-0" />

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface ToolbarStripProps {
  // Model/reasoning
  currentModelDisplay: Accessor<string>
  modelSupportsReasoning: Accessor<boolean>
  handleCycleReasoning: () => void
  toggleDelegation: () => void

  // Input
  isProcessing: Accessor<boolean>
  stashSize: Accessor<number>

  // Store refs
  chat: ReturnType<typeof useChat>
  agent: ReturnType<typeof useAgent>
  sessionStore: ReturnType<typeof useSession>
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ToolbarStrip: Component<ToolbarStripProps> = (props) => {
  const { settings, cyclePermissionMode } = useSettings()
  const { openModelBrowser } = useLayout()
  const sandbox = useSandbox()

  return (
    <div class="flex items-center justify-between gap-2 text-[var(--text-2xs)] text-[var(--text-tertiary)] font-[var(--font-ui-mono)] select-none overflow-x-auto flex-wrap min-w-0">
      {/* Left: model + thinking + plan/act + permission */}
      <div class="flex items-center gap-2 flex-wrap min-w-0">
        <ModelSelector
          onToggle={openModelBrowser}
          currentModelDisplay={props.currentModelDisplay}
        />

        <ReasoningDropdown
          effort={() => settings().generation.reasoningEffort}
          onCycle={props.handleCycleReasoning}
          available={props.modelSupportsReasoning}
        />

        <StripDivider />

        <PlanActSlider
          isPlanMode={props.agent.isPlanMode}
          togglePlanMode={() => props.agent.togglePlanMode()}
          isProcessing={props.isProcessing}
        />

        <Show when={props.agent.isPlanMode()}>
          <PlanBranchSelector
            isPlanMode={props.agent.isPlanMode}
            messages={props.sessionStore.messages}
            onMessagesChange={(msgs) => props.sessionStore.setMessages(msgs)}
          />
        </Show>

        <DelegationToggle
          enabled={() => settings().generation.delegationEnabled}
          onToggle={props.toggleDelegation}
        />

        {/* Permission badge — only when non-default */}
        <Show when={settings().permissionMode !== 'ask'}>
          <StripDivider />
          <PermissionBadge
            permissionMode={() => settings().permissionMode}
            onCyclePermission={cyclePermissionMode}
          />
        </Show>

        {/* Sandbox mode toggle — only when enabled */}
        <Show when={sandbox.sandboxEnabled()}>
          <StripDivider />
          <button
            type="button"
            onClick={() => sandbox.toggleSandbox()}
            class="inline-flex items-center gap-1 p-1 rounded-[var(--radius-md)] transition-colors text-[var(--warning)] bg-[var(--warning-subtle)]"
            title="Sandbox mode ON (changes are queued)"
          >
            <Shield class="w-3 h-3" />
            <span class="text-[var(--text-2xs)]">Sandbox</span>
            <Show when={sandbox.pendingCount() > 0}>
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation()
                  sandbox.openReview()
                }}
                class="px-1 py-0.5 text-[9px] font-medium bg-[var(--warning)] text-white rounded-full min-w-[16px] text-center"
                title={`${sandbox.pendingCount()} pending change(s) — click to review`}
              >
                {sandbox.pendingCount()}
              </button>
            </Show>
          </button>
        </Show>

        {/* Run in Background button */}
        <Show
          when={
            props.agent.isPlanMode() &&
            props.isProcessing() &&
            !props.sessionStore.backgroundPlanActive()
          }
        >
          <StripDivider />
          <button
            type="button"
            onClick={() => props.sessionStore.startBackgroundPlan()}
            class="inline-flex items-center gap-1 text-[var(--text-2xs)] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            title="Continue plan execution in background"
          >
            <Layers class="w-2.5 h-2.5" />
            Background
          </button>
        </Show>

        {/* Background plan active badge */}
        <Show when={props.sessionStore.backgroundPlanActive()}>
          <StripDivider />
          <span class="inline-flex items-center gap-1 text-[var(--accent)]">
            <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
            <span class="text-[var(--text-2xs)]">Plan running</span>
          </span>
        </Show>
      </div>

      {/* Right: token info */}
      <StatusBar
        stashSize={props.stashSize}
        isStreaming={props.chat.isStreaming}
        streamingTokenEstimate={props.chat.streamingTokenEstimate}
      />
    </div>
  )
}
