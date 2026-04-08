/**
 * Chat View Component
 *
 * Main chat container with session loading.
 * Premium layout with seamless message flow.
 * Includes tool approval dialog for agent mode.
 */

import { type Component, createEffect, createMemo, on, onCleanup } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import { useAgent } from '../../hooks/useAgent'
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
import { ApprovalDock } from './ApprovalDock'
import { ChatTitleBar } from './ChatTitleBar'
import { ChatViewShell } from './ChatViewShell'
import { MessageInput } from './MessageInput'
import { MessageList } from './MessageList'
import { PlanDock } from './PlanDock'
import { QuestionDock } from './QuestionDock'

export const ChatView: Component = () => {
  const { settings, addAutoApprovedTool } = useSettings()
  const { currentProject } = useProject()
  const agent = useAgent()

  // File watcher — start/stop based on settings + project directory
  const handleAIComment = (comment: AIComment) => {
    logInfo('chat', 'AI comment received', {
      filePath: comment.filePath,
      lineNumber: comment.lineNumber,
      type: comment.type,
    })
    const prefix = comment.type === 'execute' ? '' : '[Question] '
    const message = `${prefix}${comment.content}\n\n\`\`\`\n// File: ${comment.filePath}:${comment.lineNumber}\n${comment.context}\n\`\`\``
    void agent.run(message)
  }

  createEffect(
    on(
      () => [settings().behavior.fileWatcher, currentProject()?.directory] as const,
      ([enabled, dir]) => {
        void stopFileWatcher()
        if (enabled && dir && dir !== '~') {
          void startFileWatcher(dir, handleAIComment)
        }
      }
    )
  )

  onCleanup(() => {
    void stopFileWatcher()
  })

  const { info } = useNotification()
  let clipboardWatcherInstance: ClipboardWatcher | undefined

  createEffect(
    on(
      () => settings().behavior.clipboardWatcher,
      (enabled) => {
        clipboardWatcherInstance?.stop()
        clipboardWatcherInstance = undefined
        if (enabled) {
          clipboardWatcherInstance = createClipboardWatcher((text) => {
            if (looksLikeCode(text)) {
              info('Clipboard code detected', 'Add to context?')
            }
          })
          clipboardWatcherInstance.start()
        }
      }
    )
  )

  onCleanup(() => {
    clipboardWatcherInstance?.stop()
  })

  const activeApproval = createMemo(() => agent.pendingApproval())

  // Handle question resolution
  const handleQuestionResolve = (answer: string) => {
    const request = agent.pendingQuestion()
    if (request) {
      logInfo('question', 'Agent question answered', {
        questionId: request.id,
        hasAnswer: !!answer,
      })
    }
    agent.resolveQuestion(answer)
  }

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
    agent.resolveApproval(approved, alwaysAllow)
  }

  const shell = () => (
    <ChatViewShell
      header={<ChatTitleBar />}
      messages={<MessageList />}
      docks={
        <>
          <PlanDock />
          <ApprovalDock request={activeApproval()} onResolve={handleApprovalResolve} />
          <QuestionDock request={agent.pendingQuestion()} onResolve={handleQuestionResolve} />
        </>
      }
      input={<MessageInput />}
    />
  )

  return shell()
}
