export {
  buildHttpError,
  classifyHttpError,
  extractErrorMessage,
  parseRetryAfter,
} from './errors.js'
export { injectNoopToolIfNeeded } from './litellm.js'
export type { OpenAICompatProviderConfig, OpenAIStreamEvent } from './openai-compat.js'
export {
  buildOpenAIRequestBody,
  convertMessagesToOpenAI,
  convertToolsToOpenAIFormat,
  createOpenAICompatClient,
  enforceAlternatingRoles,
  filterEmptyContentBlocks,
  ToolCallBuffer,
} from './openai-compat.js'
export type { SSELine } from './sse.js'
export { parseSSELines, readSSEStream } from './sse.js'
