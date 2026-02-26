/**
 * Google Gemini Provider Client
 * Uses OpenAI-compatible Chat Completions API.
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const GoogleClient = createOpenAICompatClient({
  provider: 'google',
  displayName: 'Google Gemini',
  baseUrl: 'https://generativelanguage.googleapis.com/v1beta',
  defaultModel: 'gemini-2.0-flash',
  apiKeyHint: 'AVA_GOOGLE_API_KEY',
  endpoint: '/openai/chat/completions',
})
