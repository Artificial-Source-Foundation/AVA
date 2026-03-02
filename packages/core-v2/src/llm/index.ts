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

export {
  normalizeMessages,
  normalizeSystemPosition,
  stripThinkingBlocks,
} from './normalize.js'

export type {
  AuthInfo,
  AuthMethod,
  ChatMessage,
  ContentBlock,
  Credentials,
  LLMClient,
  LLMProvider,
  MessageContent,
  ProviderConfig,
  StreamDelta,
  StreamError,
  TextBlock,
  TokenUsage,
  ToolDefinition,
  ToolResultBlock,
  ToolUseBlock,
} from './types.js'
