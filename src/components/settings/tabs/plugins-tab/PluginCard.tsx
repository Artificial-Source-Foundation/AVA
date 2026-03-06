/**
 * Plugin Card
 *
 * Renders a single plugin row in the list (with install/enable/uninstall actions)
 * and a featured-plugin card variant for the featured grid.
 */

import { Download, Globe, Puzzle, RefreshCw, Star, Trash2 } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { usePlugins } from '../../../../stores/plugins'
import type { PluginCatalogItem, PluginScope } from '../../../../types/plugin'
import { formatDownloads, sourceLabel } from './plugin-utils'

// ---------------------------------------------------------------------------
// Featured Card (compact, for the featured grid)
// ---------------------------------------------------------------------------

export interface FeaturedPluginCardProps {
  plugin: PluginCatalogItem
  onSelect: (id: string) => void
}

export const FeaturedPluginCard: Component<FeaturedPluginCardProps> = (props) => (
  <button
    type="button"
    onClick={() => props.onSelect(props.plugin.id)}
    class="text-left px-2.5 py-2 rounded-[var(--radius-md)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] hover:border-[var(--accent-muted)] transition-colors"
  >
    <div class="flex items-center gap-1.5 mb-0.5">
      <Puzzle class="w-3 h-3 text-[var(--accent)]" />
      <span class="text-[11px] text-[var(--text-primary)]">{props.plugin.name}</span>
      <span class="text-[9px] text-[var(--text-muted)]">v{props.plugin.version}</span>
    </div>
    <div class="mb-0.5 flex items-center gap-1 text-[9px] text-[var(--text-muted)]">
      <span class="uppercase">{props.plugin.source}</span>
      <span>&bull;</span>
      <span
        class={props.plugin.trust === 'verified' ? 'text-[var(--success)]' : 'text-[var(--accent)]'}
      >
        {props.plugin.trust}
      </span>
    </div>
    <p class="text-[10px] text-[var(--text-muted)] line-clamp-2">{props.plugin.description}</p>
  </button>
)

// ---------------------------------------------------------------------------
// Plugin List Card (full row)
// ---------------------------------------------------------------------------

export interface PluginCardProps {
  plugin: PluginCatalogItem
  isSelected: boolean
  onSelect: (id: string) => void
  onInstall: (id: string) => void
}

export const PluginCard: Component<PluginCardProps> = (props) => {
  const plugins = usePlugins()

  const state = (): {
    installed: boolean
    enabled: boolean
    sourceType?: string
    sourceUrl?: string
    scope?: string
    version?: string
  } => plugins.pluginState()[props.plugin.id] ?? { installed: false, enabled: false }
  const pending = (): string | null => plugins.pendingAction(props.plugin.id)
  const isBusy = (): boolean => pending() !== null
  const error = (): string | undefined => plugins.errorFor(props.plugin.id)

  return (
    <div
      class={`w-full flex items-center gap-2.5 px-2.5 py-2 rounded-[var(--radius-md)] border ${props.isSelected ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]' : 'border-[var(--border-subtle)] bg-[var(--surface)]'}`}
    >
      <div class="flex-1 min-w-0">
        <button
          type="button"
          onClick={() => props.onSelect(props.plugin.id)}
          class="w-full flex items-center gap-2.5 min-w-0 text-left"
        >
          <Puzzle class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]" />
          <div class="flex-1 min-w-0">
            <p class="text-xs text-[var(--text-primary)]">{props.plugin.name}</p>
            <div class="flex items-center gap-1 text-[9px] text-[var(--text-muted)]">
              <span>v{props.plugin.version}</span>
              <span>&bull;</span>
              <span class="uppercase">{props.plugin.source}</span>
              <Show when={state().sourceType && state().sourceType !== 'catalog'}>
                <span>&bull;</span>
                <span
                  class={
                    state().sourceType === 'git' ? 'text-[var(--accent)]' : 'text-[var(--warning)]'
                  }
                >
                  {sourceLabel(state().sourceType)}
                </span>
              </Show>
              <span>&bull;</span>
              <span
                class={
                  props.plugin.trust === 'verified'
                    ? 'text-[var(--success)]'
                    : 'text-[var(--accent)]'
                }
              >
                {props.plugin.trust}
              </span>
              <Show when={props.plugin.downloads}>
                <span>&bull;</span>
                <span class="inline-flex items-center gap-0.5">
                  <Download class="w-2.5 h-2.5" />
                  {formatDownloads(props.plugin.downloads)}
                </span>
              </Show>
              <Show when={props.plugin.rating}>
                <span>&bull;</span>
                <span class="inline-flex items-center gap-0.5 text-[var(--warning)]">
                  <Star class="w-2.5 h-2.5" />
                  {props.plugin.rating?.toFixed(1)}
                </span>
              </Show>
            </div>
            <p class="text-[10px] text-[var(--text-muted)]">{props.plugin.description}</p>
            <Show when={state().sourceUrl}>
              <p class="text-[9px] text-[var(--text-muted)] truncate">{state().sourceUrl}</p>
            </Show>
          </div>
        </button>
        <Show when={error()}>
          <div class="mt-0.5 flex items-center gap-2">
            <p class="text-[10px] text-[var(--error)]">{error()}</p>
            <button
              type="button"
              onClick={() => {
                void plugins.retry(props.plugin.id)
              }}
              disabled={isBusy() || plugins.failedAction(props.plugin.id) === null}
              class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] border border-[var(--border-default)] text-[var(--text-secondary)] disabled:opacity-50"
            >
              Retry
            </button>
            <button
              type="button"
              onClick={() => {
                void plugins.recover(props.plugin.id)
              }}
              disabled={isBusy()}
              class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] border border-[var(--error)] text-[var(--error)] disabled:opacity-50"
            >
              Recover
            </button>
          </div>
        </Show>
      </div>
      <Show
        when={state().installed}
        fallback={
          <button
            type="button"
            onClick={() => props.onInstall(props.plugin.id)}
            disabled={isBusy()}
            class="px-2 py-1 text-[10px] text-white bg-[var(--accent)] rounded-[var(--radius-md)] disabled:opacity-60"
          >
            {pending() === 'install' ? 'Installing...' : 'Install'}
          </button>
        }
      >
        {/* Scope toggle */}
        <button
          type="button"
          onClick={(e) => {
            e.stopPropagation()
            const current = state().scope || 'global'
            const next: PluginScope = current === 'global' ? 'project' : 'global'
            plugins.setPluginScope(props.plugin.id, next)
          }}
          class="p-1 text-[var(--text-muted)] hover:text-[var(--text-secondary)] rounded-[var(--radius-sm)] transition-colors"
          title={
            (state().scope || 'global') === 'global'
              ? 'Global scope (click for project)'
              : 'Project scope (click for global)'
          }
        >
          <Globe class="w-3 h-3" />
        </button>
        <button
          type="button"
          onClick={(e) => {
            e.stopPropagation()
            plugins.clearError(props.plugin.id)
            void plugins.toggleEnabled(props.plugin.id)
          }}
          disabled={isBusy()}
          class={`px-2 py-1 text-[10px] rounded-[var(--radius-md)] border disabled:opacity-60 ${state().enabled ? 'text-[var(--success)] border-[var(--success)]' : 'text-[var(--text-muted)] border-[var(--border-default)]'}`}
        >
          {pending() === 'toggle' ? 'Updating...' : state().enabled ? 'Enabled' : 'Disabled'}
        </button>
        <button
          type="button"
          onClick={(e) => {
            e.stopPropagation()
            plugins.clearError(props.plugin.id)
            void plugins.uninstall(props.plugin.id)
          }}
          disabled={isBusy()}
          class="p-1.5 text-[var(--error)] hover:bg-[var(--error-subtle)] rounded-[var(--radius-sm)] disabled:opacity-60"
          aria-label="Uninstall plugin"
        >
          <Show when={pending() === 'uninstall'} fallback={<Trash2 class="w-3.5 h-3.5" />}>
            <RefreshCw class="w-3.5 h-3.5 animate-spin" />
          </Show>
        </button>
      </Show>
    </div>
  )
}
