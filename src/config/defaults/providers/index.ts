/**
 * Provider Registry
 *
 * Aggregates all provider definitions into the default list.
 * Order here determines display order in the UI.
 */

import type { LLMProviderConfig } from '../provider-defaults'
import { alibaba } from './alibaba'
import { anthropic } from './anthropic'
import { cliAgents } from './cli-agents'
import { cohere } from './cohere'
import { copilot } from './copilot'
import { deepseek } from './deepseek'
import { glm } from './glm'
import { google } from './google'
import { groq } from './groq'
import { kimi } from './kimi'
import { mistral } from './mistral'
import { ollama } from './ollama'
import { openai } from './openai'
import { openrouter } from './openrouter'
import { together } from './together'
import { xai } from './xai'

export const defaultProviders: LLMProviderConfig[] = [
  anthropic,
  openai,
  google,
  copilot,
  openrouter,
  alibaba,
  xai,
  mistral,
  groq,
  deepseek,
  cohere,
  together,
  kimi,
  glm,
  ollama,
  cliAgents,
]
