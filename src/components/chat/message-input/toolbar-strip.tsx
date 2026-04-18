/**
 * Toolbar Strip
 *
 * Unified strip below the textarea with model selector, reasoning toggle,
 * and plan/act slider. Permission badge and sandbox toggle show conditionally.
 */

import { Layers, PanelRight, Paperclip, Shield } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import type { useAgent } from '../../../hooks/useAgent'
import { useLayout } from '../../../stores/layout'
import { useSandbox } from '../../../stores/sandbox'
import type { useSession } from '../../../stores/session'
import { useSettings } from '../../../stores/settings'
import { ComposerToolbarDivider, ComposerToolbarRow } from './composer-toolbar-row'
import { ModelSelector } from './model-selector'
import { PermissionBadge, PlanActSlider, ReasoningDropdown } from './toolbar-buttons'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

  // Input
  isProcessing: Accessor<boolean>

  // Store refs
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
    <ComposerToolbarRow
      left={
        <>
          <PlanActSlider
            isPlanMode={props.agent.isPlanMode}
            togglePlanMode={() => props.agent.togglePlanMode()}
            isProcessing={props.isProcessing}
          />

          <ComposerToolbarDivider />

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
            <ComposerToolbarDivider />
            <PermissionBadge
              permissionMode={() => settings().permissionMode}
              onCyclePermission={cyclePermissionMode}
            />
          </Show>

          {/* Sandbox mode toggle — only when enabled */}
          <Show when={sandbox.sandboxEnabled()}>
            <ComposerToolbarDivider />
            <fieldset
              class="inline-flex items-center gap-1 rounded-[var(--radius-md)] bg-[var(--warning-subtle)] p-1 text-[var(--warning)]"
              aria-label="Sandbox mode controls"
            >
              <button
                type="button"
                onClick={() => sandbox.toggleSandbox()}
                class="inline-flex items-center gap-1 text-[var(--warning)] transition-colors hover:text-[var(--warning-hover)] focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--warning)] focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--surface)] rounded px-1"
                title="Toggle sandbox mode ON (changes are queued)"
                aria-label="Toggle sandbox mode"
                aria-pressed={sandbox.sandboxEnabled()}
              >
                <Shield class="w-3 h-3" />
                <span class="text-[var(--text-2xs)]">Sandbox</span>
              </button>
              <Show when={sandbox.pendingCount() > 0}>
                <button
                  type="button"
                  onClick={() => sandbox.openReview()}
                  class="px-1 py-0.5 text-[9px] font-medium bg-[var(--warning)] text-white rounded-full min-w-[16px] text-center hover:bg-[var(--warning-hover)] transition-colors focus:outline-none focus-visible:ring-2 focus-visible:ring-white focus-visible:ring-offset-1 focus-visible:ring-offset-[var(--warning)]"
                  title={`${sandbox.pendingCount()} pending change(s) — click to review`}
                  aria-label={`Review ${sandbox.pendingCount()} sandbox changes`}
                >
                  {sandbox.pendingCount()}
                </button>
              </Show>
            </fieldset>
          </Show>

          {/* Run in Background button */}
          <Show
            when={
              props.agent.isPlanMode() &&
              props.isProcessing() &&
              !props.sessionStore.backgroundPlanActive()
            }
          >
            <ComposerToolbarDivider />
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
            <ComposerToolbarDivider />
            <span class="inline-flex items-center gap-1 text-[var(--accent)]">
              <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse-subtle" />
              <span class="text-[var(--text-2xs)]">Plan running</span>
            </span>
          </Show>
        </>
      }
      right={
        <>
          <ModelSelector
            onToggle={openModelBrowser}
            currentModelDisplay={props.currentModelDisplay}
          />

          <ReasoningDropdown
            effort={() => settings().generation.reasoningEffort}
            onCycle={props.handleCycleReasoning}
            available={props.modelSupportsReasoning}
          />
        </>
      }
    />
  )
}
