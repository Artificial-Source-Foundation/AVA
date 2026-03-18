/**
 * Session Item
 *
 * A single session row in the sidebar list, with rename/delete inline states.
 */

import { Check, Loader2, MessageSquare, Pencil, Trash2, X } from 'lucide-solid'
import { type Component, createEffect, createSignal, Show } from 'solid-js'
import type { SessionWithStats } from '../../../types'
import { formatDate, formatSessionName } from './session-utils'

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

  const startRename = (): void => {
    setIsRenaming(true)
    setRenameValue(props.session.name)
  }

  const submitRename = (): void => {
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
            <div class="flex items-center gap-1.5 density-px density-py rounded-[var(--radius-md)] bg-[var(--error-subtle)] border border-[var(--error)]">
              <Trash2 class="w-3 h-3 text-[var(--error)] flex-shrink-0" />
              <span class="text-[10px] text-[var(--error)] flex-1 truncate">Delete session?</span>
              <button
                type="button"
                onClick={() => {
                  props.onDelete(props.session.id)
                  setIsConfirmingDelete(false)
                }}
                class="p-1 rounded-[var(--radius-sm)] text-[var(--error)] hover:bg-[var(--error)] hover:text-white"
                title="Confirm delete"
                aria-label="Confirm delete"
              >
                <Check class="w-3 h-3" />
              </button>
              <button
                type="button"
                onClick={() => setIsConfirmingDelete(false)}
                class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]"
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
                if (e.key === 'Escape') setIsRenaming(false)
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
          group flex items-center w-full
          px-3 py-2.5 gap-2.5
          rounded-[var(--radius-xl)]
          text-left transition-colors cursor-pointer
          ${
            props.isActive
              ? 'bg-[var(--gray-3)] text-[var(--gray-12)]'
              : 'text-[var(--gray-9)] hover:bg-[var(--alpha-white-5)] hover:text-[var(--text-primary)]'
          }
        `}
      >
        <Show
          when={props.isBusy}
          fallback={
            <MessageSquare
              class={`w-3.5 h-3.5 flex-shrink-0 ${props.isActive ? 'text-[var(--accent)]' : ''}`}
            />
          }
        >
          <Loader2 class="w-3.5 h-3.5 flex-shrink-0 text-[var(--accent)] animate-spin" />
        </Show>
        <div class="flex-1 min-w-0 flex flex-col gap-[3px]">
          <div class="text-[13px] font-medium truncate">
            {formatSessionName(props.session.name)}
          </div>
          <div class="text-[11px] text-[var(--gray-8)] truncate flex items-center gap-1.5">
            <Show when={props.session.slug}>
              <span class="opacity-70">{props.session.slug}</span>
              <span class="opacity-30">|</span>
            </Show>
            <span>{formatDate(props.session.updatedAt)}</span>
            <Show when={props.session.messageCount > 0}>
              <span>
                {props.session.messageCount} msg{props.session.messageCount !== 1 ? 's' : ''}
              </span>
            </Show>
          </div>
        </div>

        {/* Hover actions */}
        <div class="flex items-center gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity flex-shrink-0">
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              startRename()
            }}
            class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]"
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
            class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--error-subtle)]"
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
