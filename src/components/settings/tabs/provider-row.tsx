import { ChevronRight } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { ProviderRowExpanded } from './provider-row-expanded'
import type { ProviderRowProps } from './providers-tab-types'

export const ProviderRow: Component<ProviderRowProps> = (props) => {
  const statusColor = () => {
    if (props.provider.status === 'connected') return 'var(--success)'
    if (props.provider.status === 'error') return 'var(--error)'
    return 'var(--text-muted)'
  }

  return (
    <div>
      <div class="flex items-center justify-between py-2 group">
        <button type="button" onClick={() => props.onExpand()} class="flex-1 min-w-0 text-left">
          <div class="flex items-center gap-1.5">
            <span class="text-xs text-[var(--text-secondary)]">{props.provider.name}</span>
            <span class="w-1.5 h-1.5 rounded-full" style={{ background: statusColor() }} />
          </div>
          <p class="text-[10px] text-[var(--text-muted)] truncate">{props.provider.description}</p>
        </button>
        <div class="flex items-center gap-1.5">
          <button
            type="button"
            onClick={() => props.onToggle?.(!props.provider.enabled)}
            class={`w-9 h-5 rounded-full transition-colors flex-shrink-0 flex items-center ${props.provider.enabled ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'}`}
            aria-label={`Toggle ${props.provider.name}`}
          >
            <span
              class={`w-4 h-4 rounded-full bg-white shadow-sm transition-transform duration-150 ${props.provider.enabled ? 'translate-x-[18px]' : 'translate-x-[2px]'}`}
            />
          </button>
          <ChevronRight
            class={`w-3.5 h-3.5 text-[var(--text-muted)] transition-transform duration-150 ${props.isExpanded ? 'rotate-90' : ''}`}
          />
        </div>
      </div>

      <Show when={props.isExpanded}>
        <ProviderRowExpanded {...props} />
      </Show>
    </div>
  )
}
