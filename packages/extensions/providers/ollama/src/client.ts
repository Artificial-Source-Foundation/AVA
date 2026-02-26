/**
 * Ollama Provider Client
 * Uses OpenAI-compatible Chat Completions API (local server).
 */

import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'

export const OllamaClient = createOpenAICompatClient({
  provider: 'ollama',
  displayName: 'Ollama',
  baseUrl: 'http://localhost:11434/v1',
  defaultModel: 'llama3.2',
  apiKeyHint: 'OLLAMA_HOST (no key required for local)',
})
