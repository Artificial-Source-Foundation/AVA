/**
 * ServerCard Component
 *
 * Card displaying an MCP server with transport badge, tool count,
 * scope, command preview, and enable/delete controls.
 */

import { Trash2 } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { ScopeBadge, type ScopeBadgeScope } from './ScopeBadge'
import { Toggle } from './Toggle'

export type ServerTransport = 'stdio' | 'sse' | 'http'

export interface ServerCardProps {
  /** Server display name */
  name: string
  /** Transport type */
  transport: ServerTransport
  /** Number of tools exposed */
  toolCount: number
  /** Command preview (for stdio servers) */
  command?: string
  /** Scope: global (~/.ava/mcp.json) or local (.ava/mcp.json) */
  scope: ScopeBadgeScope
  /** Whether the server is enabled */
  enabled: boolean
  /** Toggle enable/disable */
  onToggle: (enabled: boolean) => void
  /** Delete handler */
  onDelete?: () => void
  /** Additional CSS classes */
  class?: string
}

const transportLabels: Record<ServerTransport, string> = {
  stdio: 'STDIO',
  sse: 'SSE',
  http: 'HTTP',
}

export const ServerCard: Component<ServerCardProps> = (props) => {
  return (
    <div
      class={`
        flex flex-col gap-3
        rounded-[var(--radius-lg)]
        border border-[var(--card-border)]
        bg-[var(--card-background)]
        p-4
        ${props.class ?? ''}
      `}
    >
      {/* Header: name + badges + toggle + delete */}
      <div class="flex items-center justify-between gap-3">
        <div class="flex items-center gap-2 min-w-0">
          <span class="text-[13px] font-medium text-[var(--text-primary)] truncate">
            {props.name}
          </span>

          {/* Transport badge */}
          <span
            class="
              px-1.5 py-px
              rounded-[var(--radius-sm)]
              text-[10px] font-medium
              bg-[var(--surface-raised)] text-[var(--text-tertiary)]
            "
          >
            {transportLabels[props.transport]}
          </span>

          <ScopeBadge scope={props.scope} />
        </div>

        <div class="flex items-center gap-2">
          <Toggle
            checked={props.enabled}
            onChange={props.onToggle}
            aria-label={`Enable ${props.name}`}
          />
          <Show when={props.onDelete}>
            <button
              type="button"
              onClick={() => props.onDelete?.()}
              class="
                p-1 rounded-[var(--radius-sm)]
                text-[var(--text-muted)]
                hover:text-[var(--error)] hover:bg-[var(--error-subtle)]
                transition-colors duration-[var(--duration-fast)]
                cursor-pointer
              "
              aria-label={`Delete ${props.name}`}
            >
              <Trash2 size={14} />
            </button>
          </Show>
        </div>
      </div>

      {/* Info row: tool count + command */}
      <div class="flex items-center gap-3 text-[11px] text-[var(--text-tertiary)]">
        <span>
          {props.toolCount} {props.toolCount === 1 ? 'tool' : 'tools'}
        </span>
        <Show when={props.command}>
          <span class="truncate font-[var(--font-mono)] text-[10px] text-[var(--text-muted)]">
            {props.command}
          </span>
        </Show>
      </div>
    </div>
  )
}
