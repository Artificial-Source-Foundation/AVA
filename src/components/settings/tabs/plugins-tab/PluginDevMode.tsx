/**
 * Plugin Dev Mode Panel
 *
 * Toggle hot-reload watching for an installed plugin, shows live log output.
 */

import { Code2 } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'

export type DevModeStatus = 'idle' | 'watching' | 'reloading'

export interface PluginDevModeProps {
  pluginId: Accessor<string>
  isDevMode: Accessor<boolean>
  status: Accessor<DevModeStatus>
  logs: Accessor<string[]>
  onToggle: (pluginId: string) => void
}

export const PluginDevMode: Component<PluginDevModeProps> = (props) => {
  return (
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-md)] bg-[var(--surface)] p-3 space-y-2">
      <div class="flex items-center justify-between">
        <div class="flex items-center gap-2">
          <Code2 class="w-3.5 h-3.5 text-[var(--text-muted)]" />
          <span class="text-[11px] text-[var(--text-primary)]">Dev Mode</span>
          <Show when={props.isDevMode()}>
            <span
              class={`px-1.5 py-0.5 text-[9px] rounded-full ${
                props.status() === 'reloading'
                  ? 'bg-[var(--warning-subtle)] text-[var(--warning)]'
                  : 'bg-[var(--success-subtle)] text-[var(--success)]'
              }`}
            >
              {props.status() === 'reloading' ? 'Reloading...' : 'Watching...'}
            </span>
          </Show>
        </div>
        <button
          type="button"
          onClick={() => props.onToggle(props.pluginId())}
          class={`relative w-8 h-[18px] rounded-full transition-colors flex-shrink-0 ${
            props.isDevMode() ? 'bg-[var(--accent)]' : 'bg-[var(--alpha-white-10)]'
          }`}
          aria-label={`${props.isDevMode() ? 'Disable' : 'Enable'} dev mode`}
        >
          <span
            class={`absolute top-[2px] w-[14px] h-[14px] rounded-full bg-white shadow-sm transition-transform ${
              props.isDevMode() ? 'translate-x-[16px]' : 'translate-x-[2px]'
            }`}
          />
        </button>
      </div>
      <p class="text-[10px] text-[var(--text-muted)]">
        Watches plugin files and auto-reloads on change.
      </p>
      <Show when={props.logs().length > 0}>
        <div class="bg-[var(--gray-1)] rounded-[var(--radius-sm)] p-2 max-h-24 overflow-y-auto">
          <pre class="text-[9px] text-[var(--text-muted)] font-mono whitespace-pre-wrap">
            {props.logs().join('\n')}
          </pre>
        </div>
      </Show>
    </div>
  )
}
