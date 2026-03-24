/**
 * Session Item
 *
 * Compact single-line session row: title... timestamp
 * With optional status dot for busy sessions.
 * Hover reveals rename/delete actions.
 */

import { Check, Pencil, Trash2, X } from 'lucide-solid'
import { type Component, createEffect, createSignal, Show } from 'solid-js'
import type { SessionWithStats } from '../../../types'
import { formatRelativeTime, formatSessionName } from './session-utils'

export interface SessionItemProps {
  session: SessionWithStats
  isActive: boolean
  isBusy: boolean
  onSelect: () => void
  onRename: (id: string, name: string) => void
  onDelete: (id: string) => void
  onContextMenu: (e: MouseEvent, id: string) => void
  renameRequestId?: string
  renameRequestSeq?: number
  onRenameRequestHandled?: () => void
}

export const SessionItem: Component<SessionItemProps> = (props) => {
  const [isRenaming, setIsRenaming] = createSignal(false)
  const [renameValue, setRenameValue] = createSignal('')
  const [isConfirmingDelete, setIsConfirmingDelete] = createSignal(false)
  const [lastHandledRenameRequest, setLastHandledRenameRequest] = createSignal(0)

  // Tracks whether the current rename was cancelled via Escape so that the
  // subsequent onBlur event does not save the unwanted value.
  let renameCancelled = false

  const startRename = (): void => {
    renameCancelled = false
    setIsRenaming(true)
    setRenameValue(props.session.name)
  }

  const cancelRename = (): void => {
    renameCancelled = true
    setIsRenaming(false)
  }

  const submitRename = (): void => {
    if (renameCancelled) return
    const newName = renameValue().trim()
    if (newName) props.onRename(props.session.id, newName)
    setIsRenaming(false)
  }

  createEffect(() => {
    const requestId = props.renameRequestId
    const requestSeq = props.renameRequestSeq ?? 0
    if (!requestId || requestSeq === 0) return
    if (requestId !== props.session.id) return
    if (requestSeq === lastHandledRenameRequest()) return

    setLastHandledRenameRequest(requestSeq)
    startRename()
    props.onRenameRequestHandled?.()
  })

  return (
    <Show
      when={!isRenaming() && !isConfirmingDelete()}
      fallback={
        <Show
          when={isRenaming()}
          fallback={
            /* Delete confirmation row */
            <div class="flex items-center gap-1.5 px-2 py-1.5 mx-1 rounded-[var(--radius-md)] bg-[var(--error-subtle)] border border-[var(--error)]">
              <Trash2 class="w-3 h-3 text-[var(--error)] flex-shrink-0" />
              <span class="text-[10px] text-[var(--error)] flex-1 truncate">Delete?</span>
              <button
                type="button"
                onClick={() => {
                  props.onDelete(props.session.id)
                  setIsConfirmingDelete(false)
                }}
                class="p-0.5 rounded-[var(--radius-sm)] text-[var(--error)] hover:bg-[var(--error)] hover:text-white"
                title="Confirm delete"
                aria-label="Confirm delete"
              >
                <Check class="w-3 h-3" />
              </button>
              <button
                type="button"
                onClick={() => setIsConfirmingDelete(false)}
                class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]"
                title="Cancel"
                aria-label="Cancel"
              >
                <X class="w-3 h-3" />
              </button>
            </div>
          }
        >
          {/* Rename input */}
          <div class="px-2 py-1">
            <input
              type="text"
              value={renameValue()}
              onInput={(e) => setRenameValue(e.currentTarget.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') submitRename()
                if (e.key === 'Escape') cancelRename()
              }}
              onBlur={() => submitRename()}
              autofocus
              class="
                w-full px-2 py-1 text-xs
                bg-[var(--input-background)]
                border border-[var(--accent)]
                rounded-[var(--radius-sm)]
                text-[var(--text-primary)]
                focus:outline-none
              "
            />
          </div>
        </Show>
      }
    >
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        onClick={() => props.onSelect()}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ' ') props.onSelect()
        }}
        onContextMenu={(e) => props.onContextMenu(e, props.session.id)}
        class={`
          group relative flex items-center w-full
          py-1.5 px-2 gap-2
          rounded-[var(--radius-md)]
          text-left transition-colors cursor-pointer
          ${
            props.isActive
              ? 'bg-[var(--alpha-white-8)] text-[var(--text-primary)]'
              : 'text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-primary)]'
          }
        `}
      >
        {/* Active indicator — subtle left border */}
        <Show when={props.isActive}>
          <span
            class="
              absolute left-0 top-1 bottom-1 w-[2px]
              rounded-r-full bg-[var(--accent)]
            "
          />
        </Show>

        {/* Status dot for busy sessions */}
        <Show when={props.isBusy}>
          <span class="flex-shrink-0 w-1.5 h-1.5 rounded-full bg-[var(--accent)] animate-pulse" />
        </Show>

        {/* Session title */}
        <span class="flex-1 min-w-0 text-[13px] truncate">
          {formatSessionName(props.session.name)}
        </span>

        {/* Relative timestamp — hidden when hover actions show */}
        <span class="text-[11px] text-[var(--text-muted)] flex-shrink-0 group-hover:hidden">
          {formatRelativeTime(props.session.updatedAt)}
        </span>

        {/* Hover actions */}
        <div class="hidden group-hover:flex items-center gap-0.5 flex-shrink-0">
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              startRename()
            }}
            class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]"
            title="Rename"
            aria-label="Rename session"
          >
            <Pencil class="w-3 h-3" />
          </button>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              setIsConfirmingDelete(true)
            }}
            class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--error-subtle)]"
            title="Delete"
            aria-label="Delete session"
          >
            <Trash2 class="w-3 h-3" />
          </button>
        </div>
      </div>
    </Show>
  )
}
