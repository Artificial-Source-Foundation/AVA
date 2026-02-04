/**
 * LLM Provider Implementations
 * Import this to register all providers with the client factory
 */

// Import providers to trigger registration via registerClient()
import './anthropic.js'
import './openrouter.js'
import './openai.js'
import './google.js'
import './glm.js'
import './kimi.js'

// New providers
import './mistral.js'
import './groq.js'
import './deepseek.js'
import './xai.js'
import './cohere.js'
import './together.js'
import './ollama.js'

// Re-export utility functions from ollama
export { isOllamaAvailable, listOllamaModels } from './ollama.js'

// Note: GitHub Copilot provider not implemented yet
// It requires special handling via the Copilot API
