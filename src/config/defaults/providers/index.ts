/**
 * Provider Registry
 *
 * Aggregates all provider definitions into the default list.
 * Order here determines display order in the UI.
 */

import type { LLMProviderConfig } from '../provider-defaults'
import { alibaba } from './alibaba'
import { anthropic } from './anthropic'
import { copilot } from './copilot'
import { gemini } from './gemini'
import { inception } from './inception'
import { kimi } from './kimi'
import { minimax } from './minimax'
import { ollama } from './ollama'
import { openai } from './openai'
import { openrouter } from './openrouter'
import { zai } from './zai'

export const defaultProviders: LLMProviderConfig[] = [
  anthropic,
  openai,
  gemini,
  ollama,
  openrouter,
  copilot,
  inception,
  alibaba,
  zai,
  kimi,
  minimax,
]
