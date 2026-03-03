import type { ChatMessage } from '@ava/core-v2/llm'

export type HistoryProcessor = (messages: ChatMessage[]) => ChatMessage[]
