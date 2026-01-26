/**
 * Delta9 Exports
 *
 * Named exports for programmatic use.
 * Import from 'delta9/exports' instead of 'delta9' to access these.
 *
 * NOTE: These are NOT exported from index.ts because OpenCode treats
 * all exports as plugin instances and tries to call them.
 */

// Mission state management
export { MissionState } from './mission/index.js'

// Configuration
export { loadConfig, getConfig, reloadConfig, clearConfigCache } from './lib/config.js'
export { DEFAULT_CONFIG } from './types/config.js'

// Logging
export { createLogger, type Logger } from './lib/logger.js'

// Agents
export { commanderAgent, operatorAgent, validatorAgent } from './agents/index.js'

// Tools
export { createDelta9Tools } from './tools/index.js'

// Types (re-export everything)
export * from './types/index.js'
