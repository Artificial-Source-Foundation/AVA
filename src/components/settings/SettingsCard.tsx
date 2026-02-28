/**
 * Settings Card
 *
 * Reusable bento-style card for grouping settings sections.
 * Provides a glass-morphic raised surface with icon + title + description.
 */

import type { Component, JSX } from 'solid-js'
import { Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'

interface SettingsCardProps {
  icon?: Component<{ class?: string }>
  title: string
  description?: string
  children: JSX.Element
}

export const SettingsCard: Component<SettingsCardProps> = (props) => (
  <div class="rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] p-4 space-y-3">
    <div class="flex items-start gap-2.5">
      <Show when={props.icon}>
        <div class="p-1.5 rounded-[var(--radius-md)] bg-[var(--accent-subtle)] text-[var(--accent)] flex-shrink-0">
          <Dynamic component={props.icon!} class="w-4 h-4" />
        </div>
      </Show>
      <div class="min-w-0">
        <h3 class="text-[13px] font-semibold text-[var(--text-primary)]">{props.title}</h3>
        <Show when={props.description}>
          <p class="text-[11px] text-[var(--text-muted)] mt-0.5">{props.description}</p>
        </Show>
      </div>
    </div>
    {props.children}
  </div>
)
