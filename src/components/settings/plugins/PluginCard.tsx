/**
 * Plugin Card
 *
 * Compact card displaying plugin name, description, star rating (1-5), and review count.
 * Used in the plugin marketplace grid and search results.
 */

import { Puzzle, Star } from 'lucide-solid'
import { type Component, For } from 'solid-js'

export interface PluginCardPlugin {
  id: string
  name: string
  description: string
  rating: number
  reviewCount: number
  author: string
}

interface PluginCardProps {
  plugin: PluginCardPlugin
  onClick?: (pluginId: string) => void
}

export const PluginCard: Component<PluginCardProps> = (props) => {
  const fullStars = () => Math.round(props.plugin.rating)

  return (
    <button
      type="button"
      onClick={() => props.onClick?.(props.plugin.id)}
      class="w-full text-left px-3 py-2.5 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] hover:border-[var(--accent-muted)] transition-colors"
    >
      <div class="flex items-center gap-2 mb-1">
        <Puzzle class="w-3.5 h-3.5 flex-shrink-0 text-[var(--accent)]" />
        <span class="text-xs font-medium text-[var(--text-primary)] truncate">
          {props.plugin.name}
        </span>
      </div>

      <p class="text-[10px] text-[var(--text-muted)] line-clamp-2 mb-1.5">
        {props.plugin.description}
      </p>

      <div class="flex items-center justify-between">
        <div class="flex items-center gap-0.5">
          <For each={Array.from({ length: 5 }, (_, i) => i)}>
            {(i) => (
              <Star
                class={`w-3 h-3 ${
                  i < fullStars()
                    ? 'text-[var(--warning)] fill-[var(--warning)]'
                    : 'text-[var(--text-muted)]'
                }`}
              />
            )}
          </For>
          <span class="text-[9px] text-[var(--text-muted)] ml-1">({props.plugin.reviewCount})</span>
        </div>
        <span class="text-[9px] text-[var(--text-muted)]">{props.plugin.author}</span>
      </div>
    </button>
  )
}
