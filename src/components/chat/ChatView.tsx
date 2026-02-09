/**
 * Chat View Component
 *
 * Main chat container with session loading.
 * Premium layout with seamless message flow.
 * Includes tool approval dialog for agent mode.
 */

import { type Component, createMemo } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useChat } from '../../hooks/useChat'
import { useSettings } from '../../stores/settings'
import { ToolApprovalDialog } from '../dialogs/ToolApprovalDialog'
import { ContextBar } from './ContextBar'
import { MessageInput } from './MessageInput'
import { MessageList } from './MessageList'

export const ChatView: Component = () => {
  const { addAutoApprovedTool } = useSettings()
  const agent = useAgent()
  const chat = useChat()

  // Merge approval from both agent and chat modes
  const activeApproval = createMemo(() => chat.pendingApproval() || agent.pendingApproval())

  // Handle tool approval resolution
  const handleApprovalResolve = (approved: boolean, alwaysAllow?: boolean) => {
    const request = activeApproval()
    if (approved && alwaysAllow && request) {
      addAutoApprovedTool(request.toolName)
    }
    // Resolve whichever approval is active
    if (chat.pendingApproval()) {
      chat.resolveApproval(approved)
    } else {
      agent.resolveApproval(approved)
    }
  }

  return (
    <div class="flex flex-col h-full bg-[var(--surface)]">
      {/* Messages area */}
      <MessageList />

      {/* Input area */}
      <MessageInput />

      {/* Context usage bar */}
      <ContextBar />

      {/* Tool Approval Dialog */}
      <ToolApprovalDialog request={activeApproval()} onResolve={handleApprovalResolve} />
    </div>
  )
}
