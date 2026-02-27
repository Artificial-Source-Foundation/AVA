/**
 * Chat View Component
 *
 * Main chat container with session loading.
 * Premium layout with seamless message flow.
 * Includes tool approval dialog for agent mode.
 */

import { type Component, createEffect, createMemo, on, onCleanup } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import type { AIComment } from '../../services/file-watcher'
import { startFileWatcher, stopFileWatcher } from '../../services/file-watcher'
import { logInfo } from '../../services/logger'
import { useProject } from '../../stores/project'
import { useSettings } from '../../stores/settings'
import { ToolApprovalDialog } from '../dialogs/ToolApprovalDialog'
import { ApprovalStateBar } from './ApprovalStateBar'
import { GitControlStrip } from './GitControlStrip'
import { MessageInput } from './MessageInput'
import { MessageList } from './MessageList'

export const ChatView: Component = () => {
  const { settings, addAutoApprovedTool } = useSettings()
  const { currentProject } = useProject()
  const agent = useAgent()
  const chat = useChat()

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
    <div class="flex flex-col h-full min-h-0 bg-[var(--surface)]">
      <ApprovalStateBar
        request={activeApproval()}
        onApprove={() => handleApprovalResolve(true)}
        onReject={() => handleApprovalResolve(false)}
      />

      {/* Messages area */}
      <MessageList />

      {/* Git controls + usage entry point */}
      <GitControlStrip />

      {/* Input area */}
      <MessageInput />

      {/* Tool Approval Dialog */}
      <ToolApprovalDialog request={activeApproval()} onResolve={handleApprovalResolve} />
    </div>
  )
}
