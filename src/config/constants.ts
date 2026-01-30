/**
 * Application Constants
 * Centralized configuration values
 */

// Storage keys for localStorage
export const STORAGE_KEYS = {
  CREDENTIALS: 'estela_credentials',
  OAUTH_VERIFIER: 'estela_oauth_verifier',
  OAUTH_STATE: 'estela_oauth_state',
  LAST_SESSION: 'estela_last_session',
  SETTINGS: 'estela_settings',
} as const

// Default values
export const DEFAULTS = {
  MODEL: 'claude-sonnet-4-20250514',
  SESSION_NAME: 'New Chat',
  MAX_TOKENS: 4096,
  TEMPERATURE: 0.7,
} as const

// Limits and constraints
export const LIMITS = {
  SESSION_NAME_MAX: 100,
  SESSION_NAME_MIN: 1,
  MESSAGE_PREVIEW_LENGTH: 80,
  MAX_SESSIONS_DISPLAY: 100,
  MESSAGE_MAX_LENGTH: 100000,
} as const

// UI timing constants (ms)
export const TIMING = {
  DEBOUNCE_DELAY: 300,
  AUTO_SCROLL_DELAY: 100,
  TOAST_DURATION: 3000,
  ANIMATION_DURATION: 200,
} as const

// Model configurations
export const MODELS = {
  ANTHROPIC: [
    { id: 'claude-sonnet-4-20250514', name: 'Claude 4 Sonnet', provider: 'anthropic' },
    { id: 'claude-opus-4-20250514', name: 'Claude 4 Opus', provider: 'anthropic' },
  ],
  OPENROUTER: [
    { id: 'anthropic/claude-sonnet-4', name: 'Claude 4 Sonnet (OR)', provider: 'openrouter' },
    { id: 'openai/gpt-4o', name: 'GPT-4o (OR)', provider: 'openrouter' },
  ],
} as const

// Session status values
export const SESSION_STATUS = {
  ACTIVE: 'active',
  COMPLETED: 'completed',
  ARCHIVED: 'archived',
} as const

export type SessionStatus = (typeof SESSION_STATUS)[keyof typeof SESSION_STATUS]
