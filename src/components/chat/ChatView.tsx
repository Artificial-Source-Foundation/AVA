/**
 * Chat View Component
 *
 * Main chat container with session loading.
 * Premium layout with seamless message flow.
 * Includes tool approval dialog for agent mode.
 *
 * When a ChatModeOverrides config is provided (e.g. HQ Director mode),
 * the same UI path renders with alternative data sources and actions.
 */

import { type Component, createEffect, createMemo, on, onCleanup, Show } from 'solid-js'
import { type ChatModeOverrides, ChatModeProvider } from '../../contexts/chat-mode'
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
import { ChatTitleBar } from './ChatTitleBar'
import { ChatViewShell } from './ChatViewShell'
import { MessageInput } from './MessageInput'
import { MessageList } from './MessageList'
import { PlanDock } from './PlanDock'
import { QuestionDock } from './QuestionDock'
import { TeamChatView } from './TeamChatView'
import { TeamStatusStrip } from './TeamStatusStrip'

export const ChatView: Component<{ config?: ChatModeOverrides }> = (props) => {
  const { settings, addAutoApprovedTool } = useSettings()
  const { currentProject } = useProject()
  const agent = useAgent()
  const chat = useChat()
  const team = useTeam()

  const isOverrideMode = () => !!props.config

  // File watcher — start/stop based on settings + project directory
  // Skipped in override modes (e.g. HQ Director) since they don't own a project session.
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
      () =>
        [settings().behavior.fileWatcher, currentProject()?.directory, isOverrideMode()] as const,
      ([enabled, dir, overrideMode]) => {
        void stopFileWatcher()
        if (!overrideMode && enabled && dir && dir !== '~') {
          void startFileWatcher(dir, handleAIComment)
        }
      }
    )
  )

  onCleanup(() => {
    void stopFileWatcher()
  })

  // Clipboard watcher — skipped in override mode
  const { info } = useNotification()
  let clipboardWatcherInstance: ClipboardWatcher | undefined

  createEffect(
    on(
      () => [settings().behavior.clipboardWatcher, isOverrideMode()] as const,
      ([enabled, overrideMode]) => {
        clipboardWatcherInstance?.stop()
        clipboardWatcherInstance = undefined
        if (!overrideMode && enabled) {
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

  // Merge approval from both agent and chat modes
  const activeApproval = createMemo(() => chat.pendingApproval() || agent.pendingApproval())

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
    if (chat.pendingApproval()) {
      chat.resolveApproval(approved)
    } else {
      agent.resolveApproval(approved, alwaysAllow)
    }
  }

  const cfg = () => props.config

  const shell = () => (
    <ChatViewShell
      header={cfg()?.header ?? <ChatTitleBar />}
      messages={<MessageList />}
      docks={
        cfg()?.hideDocks ? undefined : (
          <>
            <PlanDock />
            <ApprovalDock request={activeApproval()} onResolve={handleApprovalResolve} />
            <QuestionDock request={agent.pendingQuestion()} onResolve={handleQuestionResolve} />
          </>
        )
      }
      status={cfg() ? undefined : <TeamStatusStrip />}
      input={<MessageInput />}
    />
  )

  return (
    <Show
      when={cfg()}
      fallback={
        <Show
          when={!team.selectedMemberId()}
          fallback={
            <TeamChatView
              onStopAgent={(id) => agent.stopAgent(id)}
              onSendMessage={(id, msg) => agent.sendTeamMessage(id, msg)}
            />
          }
        >
          {shell()}
        </Show>
      }
    >
      {(config) => <ChatModeProvider value={config()}>{shell()}</ChatModeProvider>}
    </Show>
  )
}
