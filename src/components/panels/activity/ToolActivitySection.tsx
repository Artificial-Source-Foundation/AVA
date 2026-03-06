/**
 * Tool Activity Section
 *
 * Shows live tool calls from the useAgent hook with a collapsible timeline.
 */

import { Bot, ChevronDown, Wrench } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { type ToolActivity, useAgent } from '../../../hooks/useAgent'
import { toolIcons, toolStatusIcons } from './activity-config'

export const ToolActivitySection: Component = () => {
  const agent = useAgent()
  const [showToolActivity, setShowToolActivity] = createSignal(true)

  return (
    <Show when={agent.isRunning() || agent.toolActivity().length > 0}>
      <div class="border-b border-[var(--border-subtle)]">
        <button
          type="button"
          onClick={() => setShowToolActivity(!showToolActivity())}
          class="
            w-full flex items-center justify-between
            px-4 py-2
            text-left
            hover:bg-[var(--surface-raised)]
            transition-colors
          "
        >
          <div class="flex items-center gap-2">
            <Wrench class="w-4 h-4 text-[var(--accent)]" />
            <span class="text-sm font-medium text-[var(--text-primary)]">Tool Activity</span>
            <Show when={agent.isRunning()}>
              <span class="px-1.5 py-0.5 font-[var(--font-ui-mono)] text-[10px] tracking-wide bg-[var(--accent-subtle)] text-[var(--accent)] rounded-[var(--radius-sm)]">
                Live
              </span>
            </Show>
          </div>
          <ChevronDown
            class={`
              w-4 h-4 text-[var(--text-muted)]
              transition-transform duration-200
              ${showToolActivity() ? '' : '-rotate-90'}
            `}
          />
        </button>

        <Show when={showToolActivity()}>
          <div class="px-4 pb-3 space-y-2">
            {/* Current thought */}
            <Show when={agent.currentThought() && agent.isRunning()}>
              <div class="p-2 bg-[var(--surface-sunken)] rounded-[var(--radius-md)] text-xs">
                <div class="flex items-center gap-1.5 text-[var(--text-muted)] mb-1">
                  <Bot class="w-3 h-3" />
                  <span>Thinking...</span>
                </div>
                <p class="text-[var(--text-secondary)] line-clamp-2">
                  {agent.currentThought().slice(-200)}
                </p>
              </div>
            </Show>

            {/* Tool calls timeline */}
            <For each={agent.toolActivity().slice(-10).reverse()}>
              {(tool: ToolActivity) => {
                const StatusIcon = toolStatusIcons[tool.status]
                const ToolIcon = toolIcons[tool.name] || Wrench
                const statusColors = {
                  pending: 'var(--text-muted)',
                  running: 'var(--accent)',
                  success: 'var(--success)',
                  error: 'var(--error)',
                }
                const statusBgs = {
                  pending: 'var(--surface-raised)',
                  running: 'var(--accent-subtle)',
                  success: 'var(--success-subtle)',
                  error: 'var(--error-subtle)',
                }

                return (
                  <div
                    class={`
                      flex items-start gap-2 p-2
                      rounded-[var(--radius-md)]
                      border
                      ${
                        tool.status === 'running'
                          ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                          : 'border-[var(--border-subtle)] bg-[var(--surface)]'
                      }
                    `}
                  >
                    <div
                      class="p-1.5 rounded-[var(--radius-sm)] flex-shrink-0"
                      style={{ background: statusBgs[tool.status] }}
                    >
                      <ToolIcon class="w-3.5 h-3.5" style={{ color: statusColors[tool.status] }} />
                    </div>
                    <div class="flex-1 min-w-0">
                      <div class="flex items-center justify-between gap-2">
                        <span class="font-[var(--font-ui-mono)] text-[12px] tracking-wide font-medium text-[var(--text-primary)]">
                          {tool.name}
                        </span>
                        <div class="flex items-center gap-1">
                          <StatusIcon
                            class={`w-3 h-3 ${tool.status === 'running' ? 'animate-spin' : ''}`}
                            style={{ color: statusColors[tool.status] }}
                          />
                          <Show when={tool.durationMs}>
                            <span class="font-[var(--font-ui-mono)] text-[10px] text-[var(--text-muted)] tabular-nums">
                              {tool.durationMs! < 1000
                                ? `${tool.durationMs}ms`
                                : `${(tool.durationMs! / 1000).toFixed(1)}s`}
                            </span>
                          </Show>
                        </div>
                      </div>
                      <Show when={tool.output && tool.status === 'success'}>
                        <p class="text-xs text-[var(--text-secondary)] mt-1 line-clamp-1">
                          {tool.output!.slice(0, 100)}
                        </p>
                      </Show>
                      <Show when={tool.error}>
                        <p class="text-xs text-[var(--error)] mt-1 line-clamp-1">{tool.error}</p>
                      </Show>
                    </div>
                  </div>
                )
              }}
            </For>

            <Show when={agent.toolActivity().length === 0 && !agent.currentThought()}>
              <p class="text-xs text-[var(--text-muted)] text-center py-2">No tool activity yet</p>
            </Show>
          </div>
        </Show>
      </div>
    </Show>
  )
}
