import type { AgentEvent } from '@ava/core-v2/agent'

export interface AgentV2Options {
  goal: string
  provider: string
  model: string
  maxTurns: number
  timeout: number
  cwd: string
  verbose: boolean
  yolo: boolean
  json: boolean
  resume: string | null
  praxis: boolean
}

export interface PromptsModule {
  addPromptSection: (s: { name: string; priority: number; content: string }) => () => void
  buildSystemPrompt: (model?: string) => string
}

export type RetryEvent = Extract<AgentEvent, { type: 'retry' }>
export type DoomLoopEvent = Extract<AgentEvent, { type: 'doom-loop' }>
export type CompactingEvent = Extract<AgentEvent, { type: 'context:compacting' }>
