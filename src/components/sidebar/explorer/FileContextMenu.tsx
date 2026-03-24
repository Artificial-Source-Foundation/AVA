/**
 * File Context Menu Component
 *
 * Right-click context menu for the sidebar explorer.
 * Provides "Open in editor" and "Toggle read-only" actions.
 */

import { ExternalLink, Lock } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { EditorInfo } from '../../../services/ide-integration'

// ============================================================================
// Types
// ============================================================================

export interface ContextMenuState {
  x: number
  y: number
  path: string
  isDir: boolean
}

// ============================================================================
// Component
// ============================================================================

export const FileContextMenu: Component<{
  state: ContextMenuState | null
  editors: EditorInfo[]
  onOpenIn: (editorCommand: string, filePath: string) => void
  onToggleReadOnly: (filePath: string) => void
  isReadOnly: (filePath: string) => boolean
  onClose: () => void
}> = (props) => {
  const handleClickOutside = () => props.onClose()

  return (
    <Show when={props.state}>
      {(state) => (
        <>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: invisible backdrop for dismiss */}
          <div role="presentation" class="fixed inset-0 z-50" onClick={handleClickOutside} />
          <div
            class="fixed z-50 min-w-[160px] py-1 rounded-md shadow-lg border border-[var(--border-subtle)] bg-[var(--surface-raised)]"
            style={{ left: `${state().x}px`, top: `${state().y}px` }}
          >
            {/* Read-only toggle (files only) */}
            <Show when={!state().isDir}>
              <button
                type="button"
                onClick={() => {
                  props.onToggleReadOnly(state().path)
                  props.onClose()
                }}
                class="w-full flex items-center gap-2 px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] cursor-pointer text-left"
              >
                <Lock class="w-3 h-3 flex-shrink-0" />
                {props.isReadOnly(state().path) ? 'Remove read-only' : 'Mark as read-only'}
              </button>
              <Show when={props.editors.length > 0}>
                <div class="my-0.5 border-t border-[var(--border-subtle)]" />
              </Show>
            </Show>

            <Show
              when={props.editors.length > 0}
              fallback={
                <Show when={state().isDir}>
                  <div class="px-3 py-1.5 text-[var(--text-2xs)] text-[var(--text-muted)]">
                    No editors detected
                  </div>
                </Show>
              }
            >
              <div class="px-3 py-1 text-[var(--text-2xs)] text-[var(--text-muted)] uppercase tracking-wider">
                Open in
              </div>
              <For each={props.editors}>
                {(editor) => (
                  <button
                    type="button"
                    onClick={() => {
                      props.onOpenIn(editor.command, state().path)
                      props.onClose()
                    }}
                    class="w-full flex items-center gap-2 px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] cursor-pointer text-left"
                  >
                    <ExternalLink class="w-3 h-3 flex-shrink-0" />
                    {editor.name}
                  </button>
                )}
              </For>
            </Show>
          </div>
        </>
      )}
    </Show>
  )
}
