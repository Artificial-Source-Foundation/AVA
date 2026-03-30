/**
 * Chat Mode Context
 *
 * Provides data/action overrides so that alternative chat modes (e.g. HQ Director)
 * can render through the exact same ChatView → MessageList → MessageInput path
 * as normal chat, differing only in where data comes from and where sends go.
 */

import { type Accessor, createContext, type JSX, useContext } from 'solid-js'
import type { ThinkingSegment } from '../hooks/use-rust-agent'
import type { Message, ToolCall } from '../types'

export interface ChatModeOverrides {
  /** Identifies this mode for conditional UI (badges, labels). */
  mode: 'director'

  // ── Data sources ─────────────────────────────────────────────────────
  messages: Accessor<Message[]>
  isLoading: Accessor<boolean>
  isStreaming: Accessor<boolean>
  liveMessageId: Accessor<string | null>
  streamingContent: Accessor<string>
  streamingToolCalls: Accessor<ToolCall[]>
  streamingThinkingSegments: Accessor<ThinkingSegment[] | undefined>
  streamStartedAt: Accessor<number | null>

  // ── Actions ──────────────────────────────────────────────────────────
  sendMessage: (content: string) => void | Promise<void>
  cancelStream?: () => void

  // ── Behavior ─────────────────────────────────────────────────────────
  /** When true, edit/delete/branch/rewind are disabled in the message list. */
  readOnly: boolean

  // ── UI slots ─────────────────────────────────────────────────────────
  /** Replaces ChatTitleBar when provided. */
  header?: JSX.Element
  /** Injected above messages (e.g. HQ status cards). */
  topContent?: Accessor<JSX.Element>
  /** When true, PlanDock/ApprovalDock/QuestionDock are hidden. */
  hideDocks?: boolean
  /** Overrides composer placeholder text. */
  placeholder?: Accessor<string>
  /** Overrides the model display label in the toolbar. */
  modelDisplay?: Accessor<string>
  /** Extra toolbar content appended after model selector. */
  toolbarExtra?: Accessor<JSX.Element>
}

const ChatModeContext = createContext<ChatModeOverrides | undefined>()

export const ChatModeProvider = ChatModeContext.Provider

export function useChatMode(): ChatModeOverrides | undefined {
  return useContext(ChatModeContext)
}
