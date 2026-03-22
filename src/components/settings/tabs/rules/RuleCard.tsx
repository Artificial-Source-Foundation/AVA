/**
 * Rule Card + Activation Badge
 *
 * Displays a single custom rule with toggle, edit, and delete actions.
 */

import { Scale, Trash2 } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { CustomRule, RuleActivationMode } from '../../../../stores/settings/settings-types'

// ============================================================================
// Activation Badge
// ============================================================================

const ACTIVATION_COLORS: Record<RuleActivationMode, { bg: string; text: string; border: string }> =
  {
    always: {
      bg: 'var(--accent-subtle)',
      text: 'var(--accent)',
      border: 'var(--accent-muted)',
    },
    auto: {
      bg: 'var(--success-subtle, var(--alpha-white-3))',
      text: 'var(--success, var(--text-secondary))',
      border: 'var(--success, var(--border-subtle))',
    },
    manual: {
      bg: 'var(--alpha-white-3)',
      text: 'var(--text-muted)',
      border: 'var(--border-subtle)',
    },
  }

export const ActivationBadge: Component<{ mode: RuleActivationMode }> = (props) => {
  const colors = () => ACTIVATION_COLORS[props.mode]
  return (
    <span
      class="px-1 py-0.5 text-[var(--settings-text-caption)] rounded uppercase font-semibold tracking-wider"
      style={{
        background: colors().bg,
        color: colors().text,
        border: `1px solid ${colors().border}`,
      }}
    >
      {props.mode}
    </span>
  )
}

// ============================================================================
// Rule Card
// ============================================================================

export const RuleCard: Component<{
  rule: CustomRule
  onToggle: () => void
  onEdit: () => void
  onDelete: () => void
}> = (props) => (
  // biome-ignore lint/a11y/useSemanticElements: card has nested buttons (delete, toggle)
  <div
    role="button"
    tabIndex={0}
    onClick={props.onEdit}
    onKeyDown={(e) => e.key === 'Enter' && props.onEdit()}
    class={`flex items-start gap-3 px-3 py-2.5 rounded-[var(--radius-md)] border transition-colors cursor-pointer ${
      props.rule.enabled
        ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]'
        : 'border-[var(--border-subtle)] bg-[var(--surface)] hover:bg-[var(--alpha-white-3)]'
    }`}
  >
    <Scale
      class={`w-4 h-4 mt-0.5 flex-shrink-0 ${
        props.rule.enabled ? 'text-[var(--accent)]' : 'text-[var(--text-muted)]'
      }`}
    />
    <div class="flex-1 min-w-0">
      <div class="flex items-center gap-2 flex-wrap">
        <span class="text-xs font-medium text-[var(--text-primary)]">{props.rule.name}</span>
        <ActivationBadge mode={props.rule.activation} />
        <For each={props.rule.globs}>
          {(glob) => (
            <span class="px-1.5 py-0.5 text-[var(--settings-text-caption)] rounded-[var(--radius-sm)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-muted)] font-mono">
              {glob}
            </span>
          )}
        </For>
      </div>
      <Show when={props.rule.description}>
        <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] mt-0.5">
          {props.rule.description}
        </p>
      </Show>
    </div>
    <div class="flex items-center gap-1 flex-shrink-0 mt-0.5">
      <button
        type="button"
        onClick={(e) => {
          e.stopPropagation()
          props.onDelete()
        }}
        class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--alpha-white-5)] transition-colors"
        title="Delete rule"
      >
        <Trash2 class="w-3 h-3" />
      </button>
      <button
        type="button"
        onClick={(e) => {
          e.stopPropagation()
          props.onToggle()
        }}
        style={{ width: '28px', height: '16px' }}
        class={`relative rounded-full transition-colors flex-shrink-0 ${
          props.rule.enabled ? 'bg-[var(--accent)]' : 'bg-[var(--alpha-white-10)]'
        }`}
        aria-label={`${props.rule.enabled ? 'Disable' : 'Enable'} ${props.rule.name}`}
      >
        <span
          style={{
            width: '12px',
            height: '12px',
            top: '2px',
            left: props.rule.enabled ? '14px' : '2px',
          }}
          class="absolute rounded-full bg-white shadow-sm transition-[left] duration-150"
        />
      </button>
    </div>
  </div>
)
