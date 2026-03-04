/**
 * Chat View Component
 *
 * Main chat container with session loading.
 * Premium layout with seamless message flow.
 * Includes tool approval dialog for agent mode.
 */

import { type Component, createEffect, createMemo, on, onCleanup, Show } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import {
  type ClipboardWatcher,
  createClipboardWatcher,
  looksLikeCode,
} from '../../services/clipboard-watcher'
import type { AIComment } from '../../services/file-watcher'
import { startFileWatcher, stopFileWatcher } from '../../services/file-watcher'
import { logInfo } from '../../services/logger'
import { useProject } from '../../stores/project'
import { useSettings } from '../../stores/settings'
import { useTeam } from '../../stores/team'
import { ApprovalDock } from './ApprovalDock'
import { GitControlStrip } from './GitControlStrip'
import { MessageInput } from './MessageInput'
import { MessageList } from './MessageList'
import { MessageQueueBar } from './MessageQueueBar'
import { TeamChatView } from './TeamChatView'
import { TeamStatusStrip } from './TeamStatusStrip'

export const ChatView: Component = () => {
  const { settings, addAutoApprovedTool } = useSettings()
  const { currentProject } = useProject()
  const agent = useAgent()
  const chat = useChat()
  const team = useTeam()

  // File watcher — start/stop based on settings + project directory
  const handleAIComment = (comment: AIComment) => {
    logInfo('chat', 'AI comment received', {
      filePath: comment.filePath,
      lineNumber: comment.lineNumber,
      type: comment.type,
    })
    const prefix = comment.type === 'execute' ? '' : '[Question] '
    const message = `${prefix}${comment.content}\n\n\`\`\`\n// File: ${comment.filePath}:${comment.lineNumber}\n${comment.context}\n\`\`\``
    void chat.sendMessage(message)
  }

  createEffect(
    on(
      () => [settings().behavior.fileWatcher, currentProject()?.directory] as const,
      ([enabled, dir]) => {
        if (enabled && dir && dir !== '~') {
          void startFileWatcher(dir, handleAIComment)
        } else {
          void stopFileWatcher()
        }
      }
    )
  )

  onCleanup(() => {
    void stopFileWatcher()
  })

  // Clipboard watcher — notify when code is detected in clipboard
  const { info } = useNotification()
  let clipboardWatcherInstance: ClipboardWatcher | undefined

  createEffect(
    on(
      () => settings().behavior.clipboardWatcher,
      (enabled) => {
        if (enabled) {
          clipboardWatcherInstance = createClipboardWatcher((text) => {
            if (looksLikeCode(text)) {
              info('Clipboard code detected', 'Add to context?')
            }
          })
          clipboardWatcherInstance.start()
        } else {
          clipboardWatcherInstance?.stop()
          clipboardWatcherInstance = undefined
        }
      }
    )
  )

  onCleanup(() => {
    clipboardWatcherInstance?.stop()
  })

  // Merge approval from both agent and chat modes
  const activeApproval = createMemo(() => chat.pendingApproval() || agent.pendingApproval())

  // Handle tool approval resolution
  const handleApprovalResolve = (approved: boolean, alwaysAllow?: boolean) => {
    const request = activeApproval()
    if (approved && alwaysAllow && request) {
      addAutoApprovedTool(request.toolName)
    }
    if (request) {
      logInfo('approval', 'Tool approval resolved', {
        toolName: request.toolName,
        approved,
        alwaysAllow: !!alwaysAllow,
      })
    }
    // Resolve whichever approval is active
    if (chat.pendingApproval()) {
      chat.resolveApproval(approved)
    } else {
      agent.resolveApproval(approved)
    }
  }

  return (
    <Show
      when={!team.selectedMemberId()}
      fallback={
        <TeamChatView
          onStopAgent={(id) => agent.stopAgent(id)}
          onSendMessage={(id, msg) => agent.sendTeamMessage(id, msg)}
        />
      }
    >
      <div class="flex flex-col h-full min-h-0 bg-[var(--surface)]">
        {/* Messages area */}
        <MessageList />

        {/* Inline tool approval dock */}
        <ApprovalDock request={activeApproval()} onResolve={handleApprovalResolve} />

        {/* Queued messages indicator */}
        <MessageQueueBar
          messages={chat.messageQueue()}
          onRemove={(i) => chat.removeFromQueue(i)}
          onClear={() => chat.clearQueue()}
        />

        {/* Team status strip (visible when team is active) */}
        <TeamStatusStrip />

        {/* Git controls + usage entry point */}
        <GitControlStrip />

        {/* Input area */}
        <MessageInput />
      </div>
    </Show>
  )
}
