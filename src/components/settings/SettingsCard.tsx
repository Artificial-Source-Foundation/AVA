/**
 * Settings Card
 *
 * Reusable bento-style card for grouping settings sections.
 * Uses theme-aware CSS variables for proper light/dark mode support.
 * Header: icon + title (14px/500) + description (12px).
 */

import type { Component, JSX } from 'solid-js'
import { Show } from 'solid-js'

interface SettingsCardProps {
  icon?: Component<{ class?: string }>
  title: string
  description?: string
  children: JSX.Element
}

export const SettingsCard: Component<SettingsCardProps> = (props) => (
  <div
    style={{
      display: 'flex',
      'flex-direction': 'column',
      gap: '16px',
      background: 'var(--surface)',
      border: '1px solid var(--border-subtle)',
      'border-radius': '12px',
      padding: '20px',
    }}
  >
    <div class="flex items-center gap-2.5 min-w-0">
      <Show when={props.icon}>
        {(() => {
          const Icon = props.icon!
          return (
            <span class="shrink-0 w-4 h-4" style={{ color: 'var(--text-secondary)' }}>
              <Icon class="w-4 h-4" />
            </span>
          )
        })()}
      </Show>
      <div class="min-w-0">
        <h3
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '14px',
            'font-weight': '500',
            color: 'var(--text-primary)',
          }}
        >
          {props.title}
        </h3>
        <Show when={props.description}>
          <p
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '12px',
              color: 'var(--text-muted)',
              'margin-top': '2px',
            }}
          >
            {props.description}
          </p>
        </Show>
      </div>
    </div>
    {props.children}
  </div>
)
