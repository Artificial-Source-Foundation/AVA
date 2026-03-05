/**
 * @ava/extensions — Built-in extensions for AVA.
 *
 * All extensions use the same ExtensionAPI as community extensions.
 * Each extension has an activate() function and an ava-extension.json manifest.
 */

export { activate as activateAgentModes } from './agent-modes/src/index.js'
export { activate as activateCommander } from './commander/src/index.js'
export { activate as activateContext } from './context/src/index.js'
export { activate as activateDiff } from './diff/src/index.js'
export { activate as activateGit } from './git/src/index.js'
export { activate as activateHooks } from './hooks/src/index.js'
export { activate as activateInstructions } from './instructions/src/index.js'
export { activate as activateLsp } from './lsp/src/index.js'
export { activate as activateMcp } from './mcp/src/index.js'
export { activate as activateMemory } from './memory/src/index.js'
export { activate as activateModels } from './models/src/index.js'
export { activate as activatePermissions } from './permissions/src/index.js'
export { activate as activatePlugins } from './plugins/src/index.js'
export { activate as activatePrompts } from './prompts/src/index.js'
// Shared provider utilities
export {
  buildHttpError,
  buildOpenAIRequestBody,
  classifyHttpError,
  convertToolsToOpenAIFormat,
  createOpenAICompatClient,
  extractErrorMessage,
  parseRetryAfter,
  parseSSELines,
  readSSEStream,
  ToolCallBuffer,
} from './providers/_shared/src/index.js'
// Provider extensions
export { activate as activateAnthropic } from './providers/anthropic/src/index.js'
export { activate as activateCohere } from './providers/cohere/src/index.js'
export { activate as activateDeepSeek } from './providers/deepseek/src/index.js'
export { activate as activateGroq } from './providers/groq/src/index.js'
export { activate as activateMistral } from './providers/mistral/src/index.js'
export { activate as activateOllama } from './providers/ollama/src/index.js'
export { activate as activateOpenAI } from './providers/openai/src/index.js'
export { activate as activateOpenRouter } from './providers/openrouter/src/index.js'
export { activate as activateTogether } from './providers/together/src/index.js'
export { activate as activateXAI } from './providers/xai/src/index.js'
export { activate as activateRecall } from './recall/src/index.js'
export { activate as activateServer } from './server/src/index.js'
export { activate as activateSlashCommands } from './slash-commands/src/index.js'
export { activate as activateToolsExtended } from './tools-extended/src/index.js'
export { activate as activateValidator } from './validator/src/index.js'
