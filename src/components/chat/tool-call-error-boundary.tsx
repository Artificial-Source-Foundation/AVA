/**
 * Tool Call Error Boundary
 *
 * Wraps ToolCallGroup so malformed data doesn't crash the entire chat.
 * Uses SolidJS ErrorBoundary with a minimal fallback.
 */

import { AlertCircle } from 'lucide-solid'
import type { ParentComponent } from 'solid-js'
import { ErrorBoundary } from 'solid-js'

export const ToolCallErrorBoundary: ParentComponent = (props) => {
  return (
    <ErrorBoundary
      fallback={(err) => (
        <div class="flex items-center gap-2 px-3 py-2 my-1 text-xs text-[var(--text-muted)] bg-[var(--surface-raised)] rounded-[var(--radius-md)] border border-[var(--border-subtle)]">
          <AlertCircle class="w-3.5 h-3.5 text-[var(--error)] flex-shrink-0" />
          <span>Failed to render tool call</span>
          <span class="text-[var(--text-muted)] opacity-60 truncate">
            {err instanceof Error ? err.message : 'Unknown error'}
          </span>
        </div>
      )}
    >
      {props.children}
    </ErrorBoundary>
  )
}
