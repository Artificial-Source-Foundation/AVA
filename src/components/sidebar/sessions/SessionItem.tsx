/**
 * Session Item — Windsurf/Cascade style
 *
 * Two-line layout: title on line 1, relative timestamp on line 2.
 * Clean design with no hover action icons — use right-click context menu instead.
 * Active session gets a subtle background highlight.
 */

import { Check, Trash2, X } from 'lucide-solid'
import { type Component, createEffect, createSignal, Show } from 'solid-js'
import type { SessionWithStats } from '../../../types'
import { formatRelativeTimeVerbose, formatSessionName } from './session-utils'

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
            <div class="flex items-center gap-1.5 px-2 py-1.5 rounded-[var(--radius-md)] bg-[var(--error-subtle)] border border-[var(--error)]">
              <Trash2 class="w-3 h-3 text-[var(--error)] flex-shrink-0" />
              <span class="text-[var(--text-2xs)] text-[var(--error)] flex-1 truncate">
                Delete?
              </span>
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
          <div class="px-1 py-0.5">
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
        class="relative flex flex-col w-full rounded-[6px] text-left transition-colors cursor-pointer"
        classList={{
          'hover:bg-[rgba(255,255,255,0.04)]': !props.isActive,
        }}
        style={{
          padding: '8px 10px',
          background: props.isActive ? '#ffffff12' : undefined,
          border: props.isActive ? '1px solid #ffffff0a' : '1px solid transparent',
        }}
      >
        {/* Line 1: Session title */}
        <div class="flex items-center gap-1.5">
          <Show when={props.isBusy}>
            <span class="flex-shrink-0 w-1.5 h-1.5 rounded-full bg-[#0A84FF] animate-pulse-subtle" />
          </Show>
          <span
            class="flex-1 min-w-0 truncate"
            style={{
              'font-size': '13px',
              'font-weight': props.isActive ? '500' : undefined,
              color: props.isActive ? '#F5F5F7' : '#86868B',
            }}
          >
            {formatSessionName(props.session.name)}
          </span>
        </div>

        {/* Line 2: Relative timestamp (subtitle) */}
        <span class="mt-0.5 truncate" style={{ 'font-size': '11px', color: '#48484A' }}>
          {formatRelativeTimeVerbose(props.session.updatedAt)}
        </span>
      </div>
    </Show>
  )
}
