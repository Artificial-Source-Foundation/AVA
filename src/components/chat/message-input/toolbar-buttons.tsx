/**
 * Toolbar Buttons
 *
 * Left-side toolbar sub-groups separated by thin dividers:
 * [Plan/Act] [Agent/Chat] | [Permission] | [Checkpoint] [Undo]
 */

import { Bookmark, Bot, FileSearch, Shield, ShieldAlert, ShieldOff, Undo2, Zap } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'
import type { PermissionMode } from '../../../stores/settings'
import type { PermissionConfigEntry } from './types'

// ---------------------------------------------------------------------------
// Permission configuration (constant map)
// ---------------------------------------------------------------------------

export const PERMISSION_CONFIG: Record<PermissionMode, PermissionConfigEntry> = {
  ask: { icon: Shield, color: 'var(--text-muted)', label: 'Ask' },
  'auto-approve': { icon: ShieldAlert, color: 'var(--warning)', label: 'Auto' },
  bypass: { icon: ShieldOff, color: 'var(--error)', label: 'Bypass' },
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface ToolbarButtonsProps {
  isPlanMode: Accessor<boolean>
  togglePlanMode: () => void
  useAgentMode: Accessor<boolean>
  onToggleAgentMode: () => void
  isProcessing: Accessor<boolean>
  permissionMode: Accessor<PermissionMode>
  onCyclePermission: () => void
  messageCount: Accessor<number>
  onCreateCheckpoint: () => Promise<void>
  gitEnabled: Accessor<boolean>
  autoCommit: Accessor<boolean>
  onUndo: () => Promise<void>
  isUndoing: Accessor<boolean>
}

// ---------------------------------------------------------------------------
// Tiny divider between sub-groups
// ---------------------------------------------------------------------------

const Div: Component = () => <span class="w-px h-4 bg-[var(--border-subtle)] shrink-0" />

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ToolbarButtons: Component<ToolbarButtonsProps> = (props) => (
  <div class="flex items-center gap-2">
    {/* ── Mode toggles ── */}

    {/* Plan/Act toggle */}
    <button
      type="button"
      onClick={props.togglePlanMode}
      disabled={props.isProcessing()}
      class={`
        flex items-center gap-1 px-2 py-1
        text-[11px] font-medium rounded-[var(--radius-md)]
        transition-colors
        ${
          props.isPlanMode()
            ? 'bg-[var(--warning-subtle)] text-[var(--warning)] border border-[var(--warning)]'
            : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)]'
        }
        disabled:opacity-50 disabled:cursor-not-allowed
      `}
    >
      <FileSearch class="w-3 h-3" />
      {props.isPlanMode() ? 'Plan' : 'Act'}
    </button>

    {/* Agent/Chat toggle */}
    <button
      type="button"
      onClick={props.onToggleAgentMode}
      disabled={props.isProcessing()}
      class={`
        flex items-center gap-1 px-2 py-1
        text-[11px] font-medium rounded-[var(--radius-md)]
        transition-colors
        ${
          props.useAgentMode()
            ? 'bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent)]'
            : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)]'
        }
        disabled:opacity-50 disabled:cursor-not-allowed
      `}
      title={
        props.useAgentMode() ? 'Agent mode: Full autonomous loop' : 'Chat mode: Simple responses'
      }
    >
      {props.useAgentMode() ? <Bot class="w-3 h-3" /> : <Zap class="w-3 h-3" />}
      {props.useAgentMode() ? 'Agent' : 'Chat'}
    </button>

    <Div />

    {/* ── Permission ── */}
    {(() => {
      const cfg = PERMISSION_CONFIG[props.permissionMode()]
      const Icon = cfg.icon
      return (
        <button
          type="button"
          onClick={props.onCyclePermission}
          class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)] transition-colors"
          title={`Permissions: ${cfg.label} (click to cycle)`}
        >
          <Icon class="w-3 h-3" style={{ color: cfg.color }} />
          <span style={{ color: cfg.color }}>{cfg.label}</span>
        </button>
      )
    })()}

    <Div />

    {/* ── Actions ── */}

    {/* Checkpoint button */}
    <Show when={props.messageCount() > 0}>
      <button
        type="button"
        onClick={props.onCreateCheckpoint}
        disabled={props.isProcessing()}
        class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)] transition-colors text-[var(--text-secondary)] disabled:opacity-50"
        title="Save checkpoint"
      >
        <Bookmark class="w-3 h-3" />
      </button>
    </Show>

    {/* Undo button — visible when git auto-commit is enabled */}
    <Show when={props.gitEnabled() && props.autoCommit()}>
      <button
        type="button"
        onClick={props.onUndo}
        disabled={props.isProcessing() || props.isUndoing()}
        class="flex items-center gap-1 px-2 py-1 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] hover:border-[var(--accent-muted)] transition-colors text-[var(--text-secondary)] disabled:opacity-50"
        title="Undo last AI edit (git revert)"
      >
        <Undo2 class="w-3 h-3" />
      </button>
    </Show>
  </div>
)
