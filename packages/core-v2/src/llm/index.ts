export {
  createClient,
  getApiKey,
  getAuth,
  getRegisteredProviders,
  hasProvider,
  registerProvider,
  resetProviders,
  unregisterProvider,
} from './client.js'

export type {
  AuthInfo,
  AuthMethod,
  ChatMessage,
  Credentials,
  LLMClient,
  LLMProvider,
  ProviderConfig,
  StreamDelta,
  StreamError,
  TokenUsage,
  ToolDefinition,
  ToolUseBlock,
} from './types.js'
