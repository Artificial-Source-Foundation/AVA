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
  <div class="rounded-[var(--radius-xl)] border border-[var(--gray-5)] bg-[var(--gray-3)] p-6 space-y-4">
    <div class="flex items-start gap-3">
      <Show when={props.icon}>
        <div class="p-2 rounded-[var(--radius-md)] bg-[var(--accent-subtle)] text-[var(--accent)] flex-shrink-0">
          <Dynamic component={props.icon!} class="w-5 h-5" />
        </div>
      </Show>
      <div class="min-w-0">
        <h3 class="text-[16px] font-semibold text-[var(--text-primary)]">{props.title}</h3>
        <Show when={props.description}>
          <p class="text-[13px] text-[var(--gray-8)] mt-0.5">{props.description}</p>
        </Show>
      </div>
    </div>
    {props.children}
  </div>
)
