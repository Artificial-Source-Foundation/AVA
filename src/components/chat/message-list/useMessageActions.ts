/**
 * Message action handlers hook
 *
 * Encapsulates delete, rollback, rewind, and branch confirmation logic
 * used by MessageList.
 */

import { type Accessor, createSignal } from 'solid-js'
import type { useNotification } from '../../../contexts/notification'
import type { Message } from '../../../types'

export interface UseMessageActionsOptions {
  messages: Accessor<Message[]>
  lastMessageId: Accessor<string | null>
  rollbackToMessage: (messageId: string) => Promise<void>
  branchAtMessage: (messageId: string) => Promise<void>
  revertFilesAfter: (messageId: string) => Promise<number>
  notifySuccess: ReturnType<typeof useNotification>['success']
  notifyError: ReturnType<typeof useNotification>['error']
}

export interface DeleteTarget {
  messageId: string
  isLast: boolean
}

export interface MessageActionsAPI {
  deleteTarget: Accessor<DeleteTarget | null>
  setDeleteTarget: (target: DeleteTarget | null) => void
  rewindTarget: Accessor<string | null>
  setRewindTarget: (id: string | null) => void
  handleDeleteRequest: (messageId: string) => void
  handleDeleteConfirm: () => Promise<void>
  handleBranch: (messageId: string) => Promise<void>
  handleRewindConversationOnly: () => Promise<void>
  handleRewindAndRevert: () => Promise<void>
}

export function useMessageActions(opts: UseMessageActionsOptions): MessageActionsAPI {
  const [deleteTarget, setDeleteTarget] = createSignal<DeleteTarget | null>(null)
  const [rewindTarget, setRewindTarget] = createSignal<string | null>(null)

  const handleDeleteRequest = (messageId: string): void => {
    setDeleteTarget({ messageId, isLast: messageId === opts.lastMessageId() })
  }

  const handleDeleteConfirm = async (): Promise<void> => {
    const target = deleteTarget()
    if (!target) return
    setDeleteTarget(null)
    await opts.rollbackToMessage(target.messageId)
  }

  const handleBranch = async (messageId: string): Promise<void> => {
    try {
      await opts.branchAtMessage(messageId)
      opts.notifySuccess('Conversation branched')
    } catch (error) {
      opts.notifyError(
        'Conversation branch unavailable',
        error instanceof Error ? error.message : 'Could not branch this conversation.'
      )
    }
  }

  const handleRewindConversationOnly = async (): Promise<void> => {
    const msgId = rewindTarget()
    if (!msgId) return
    setRewindTarget(null)
    // Keep messages up to and including the target
    const msgs = opts.messages()
    const index = msgs.findIndex((m) => m.id === msgId)
    if (index === -1) return
    // Delete everything after this message
    const nextMsg = msgs[index + 1]
    if (nextMsg) await opts.rollbackToMessage(nextMsg.id)
    opts.notifySuccess('Conversation rewound')
  }

  const handleRewindAndRevert = async (): Promise<void> => {
    const msgId = rewindTarget()
    if (!msgId) return
    setRewindTarget(null)
    const reverted = await opts.revertFilesAfter(msgId)
    const msgs = opts.messages()
    const index = msgs.findIndex((m) => m.id === msgId)
    if (index === -1) return
    const nextMsg = msgs[index + 1]
    if (nextMsg) await opts.rollbackToMessage(nextMsg.id)
    opts.notifySuccess(`Rewound${reverted > 0 ? ` and reverted ${reverted} file(s)` : ''}`)
  }

  return {
    deleteTarget,
    setDeleteTarget,
    rewindTarget,
    setRewindTarget,
    handleDeleteRequest,
    handleDeleteConfirm,
    handleBranch,
    handleRewindConversationOnly,
    handleRewindAndRevert,
  }
}
