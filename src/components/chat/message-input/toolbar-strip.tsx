/**
 * Toolbar Strip
 *
 * Unified strip below the textarea with model selector, reasoning toggle,
 * and plan/act slider. Permission badge and sandbox toggle show conditionally.
 */

import { Layers, PanelRight, Paperclip, Shield } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import type { useAgent } from '../../../hooks/useAgent'
import type { useChat } from '../../../hooks/useChat'
import { useLayout } from '../../../stores/layout'
import { useSandbox } from '../../../stores/sandbox'
import type { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { ModelSelector } from './model-selector'
import { StatusBar } from './status-bar'
import { PermissionBadge, PlanActSlider, ReasoningDropdown } from './toolbar-buttons'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Thin vertical divider between strip groups */
const StripDivider: Component = () => <span class="h-4 w-px shrink-0 bg-[var(--border-default)]" />

/** Small 28x28 icon button for the toolbar */
const ToolbarIconButton: Component<{
  icon: Component<{ class?: string }>
  title: string
  onClick?: () => void
}> = (props) => (
  <button
    type="button"
    onClick={() => props.onClick?.()}
    class="flex h-7 w-7 items-center justify-center rounded-[6px] text-[var(--text-muted)] transition-colors hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-secondary)]"
    title={props.title}
    aria-label={props.title}
  >
    <props.icon class="w-3.5 h-3.5" />
  </button>
)

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
    <div class="flex items-center justify-between gap-2 text-[var(--text-xs)] text-[var(--text-tertiary)] select-none min-w-0">
      {/* Left: Plan/Act toggle + divider + icon buttons */}
      <div class="flex items-center gap-1 min-w-0">
        <PlanActSlider
          isPlanMode={props.agent.isPlanMode}
          togglePlanMode={() => props.agent.togglePlanMode()}
          isProcessing={props.isProcessing}
        />

        <StripDivider />

        <ToolbarIconButton
          icon={Paperclip}
          title="Attach file"
          onClick={() => {
            const input = document.createElement('input')
            input.type = 'file'
            input.multiple = true
            input.onchange = () => {
              if (input.files) {
                window.dispatchEvent(new CustomEvent('ava:attach-files', { detail: input.files }))
              }
            }
            input.click()
          }}
        />
        <ToolbarIconButton
          icon={PanelRight}
          title="Toggle inspector"
          onClick={() => {
            const { toggleRightPanel } = useLayout()
            toggleRightPanel()
          }}
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
            class="inline-flex items-center gap-1 rounded-[var(--radius-md)] bg-[var(--warning-subtle)] p-1 text-[var(--warning)] transition-colors hover:border-[var(--warning-border)]"
            title="Sandbox mode ON (changes are queued)"
            aria-label="Toggle sandbox mode"
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
                aria-label={`Review ${sandbox.pendingCount()} sandbox changes`}
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
            aria-label="Continue plan execution in background"
          >
            <Layers class="w-2.5 h-2.5" />
            Background
          </button>
        </Show>

        {/* Background plan active badge */}
        <Show when={props.sessionStore.backgroundPlanActive()}>
          <StripDivider />
          <span class="inline-flex items-center gap-1 text-[var(--accent)]">
            <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse-subtle" />
            <span class="text-[var(--text-2xs)]">Plan running</span>
          </span>
        </Show>
      </div>

      {/* Right: model pill + think pill + status */}
      <div class="flex items-center gap-2 min-w-0">
        <ModelSelector
          onToggle={openModelBrowser}
          currentModelDisplay={props.currentModelDisplay}
        />

        <ReasoningDropdown
          effort={() => settings().generation.reasoningEffort}
          onCycle={props.handleCycleReasoning}
          available={props.modelSupportsReasoning}
        />

        <StatusBar
          stashSize={props.stashSize}
          isStreaming={props.chat.isStreaming}
          streamingTokenEstimate={props.chat.streamingTokenEstimate}
        />
      </div>
    </div>
  )
}
