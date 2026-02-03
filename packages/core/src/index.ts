/**
 * @estela/core
 * Core business logic for Estela - LLM client, tools, types
 */

// Authentication (API key + OAuth)
export * from './auth/index.js'
// Context management (token tracking, compaction)
export * from './context/index.js'
// Diff tracking
export * from './diff/index.js'
// Git snapshots
export * from './git/index.js'
// LLM client
export * from './llm/index.js'
// Model registry
export * from './models/index.js'
// Permission system
export * from './permissions/index.js'
// Platform abstraction
export * from './platform.js'
// Session management
export * from './session/index.js'
// Tools
export * from './tools/index.js'
// Types
export * from './types/index.js'
