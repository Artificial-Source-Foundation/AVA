/**
 * Session Branch Tree
 *
 * Collapsible tree visualization of session branches.
 * Shows parent-child relationships from fork/branch operations.
 */

import { ChevronRight, GitBranch } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { SessionWithStats } from '../../types'

interface SessionBranchTreeProps {
  roots: SessionWithStats[]
  childMap: Map<string, SessionWithStats[]>
  currentSessionId: string | undefined
  onSelect: (id: string) => void
}

const TreeNode: Component<{
  session: SessionWithStats
  childMap: Map<string, SessionWithStats[]>
  currentSessionId: string | undefined
  onSelect: (id: string) => void
  depth: number
}> = (props) => {
  const children = () => props.childMap.get(props.session.id) ?? []
  const hasChildren = () => children().length > 0
  const [expanded, setExpanded] = createSignal(true)
  const isCurrent = () => props.session.id === props.currentSessionId

  return (
    <div>
      <button
        type="button"
        onClick={() => props.onSelect(props.session.id)}
        class={`w-full flex items-center gap-1.5 px-2 py-1.5 text-left rounded-[var(--radius-md)] transition-colors ${
          isCurrent()
            ? 'bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent-muted)]'
            : 'hover:bg-[var(--surface-hover)] text-[var(--text-secondary)]'
        }`}
        style={{ 'padding-left': `${props.depth * 16 + 8}px` }}
      >
        <Show when={hasChildren()} fallback={<span class="w-4" />}>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              setExpanded((v) => !v)
            }}
            class="p-0.5 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] transition-transform"
          >
            <ChevronRight
              class="w-3 h-3 text-[var(--text-muted)] transition-transform"
              classList={{ 'rotate-90': expanded() }}
            />
          </button>
        </Show>
        <Show when={props.depth > 0}>
          <GitBranch class="w-3 h-3 text-[var(--text-muted)] flex-shrink-0" />
        </Show>
        <span class="text-[12px] truncate flex-1">{props.session.name}</span>
        <Show when={props.session.messageCount > 0}>
          <span class="text-[9px] text-[var(--text-muted)] tabular-nums">
            {props.session.messageCount}
          </span>
        </Show>
      </button>
      <Show when={expanded() && hasChildren()}>
        <For each={children()}>
          {(child) => (
            <TreeNode
              session={child}
              childMap={props.childMap}
              currentSessionId={props.currentSessionId}
              onSelect={props.onSelect}
              depth={props.depth + 1}
            />
          )}
        </For>
      </Show>
    </div>
  )
}

export const SessionBranchTree: Component<SessionBranchTreeProps> = (props) => (
  <div class="space-y-0.5">
    <For each={props.roots}>
      {(root) => (
        <TreeNode
          session={root}
          childMap={props.childMap}
          currentSessionId={props.currentSessionId}
          onSelect={props.onSelect}
          depth={0}
        />
      )}
    </For>
    <Show when={props.roots.length === 0}>
      <p class="text-[11px] text-[var(--text-muted)] text-center py-4">No sessions</p>
    </Show>
  </div>
)
