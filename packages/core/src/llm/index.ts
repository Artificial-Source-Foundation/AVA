/**
 * @ava/core LLM Module
 */

export {
  type AuthInfo,
  createClient,
  getApiKey,
  getAuth,
  getEditorModelConfig,
  getWeakModelConfig,
  type LLMClient,
  registerClient,
} from './client.js'

// Import providers to register them with the client factory
import './providers/index.js'
