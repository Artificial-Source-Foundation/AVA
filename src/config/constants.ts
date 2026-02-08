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
  LAST_PROJECT: 'estela_last_project',
  SETTINGS: 'estela_settings',
  PANEL_SIZES: 'estela-panel-sizes',
  SIDEBAR_COLLAPSED: 'estela-sidebar-collapsed',
  LAYOUT_ACTIVITY: 'estela-layout-activity',
  LAYOUT_SIDEBAR_VISIBLE: 'estela-layout-sidebar-visible',
  LAYOUT_RIGHT_VISIBLE: 'estela-layout-right-visible',
  LAYOUT_BOTTOM_VISIBLE: 'estela-layout-bottom-visible',
  LAYOUT_BOTTOM_HEIGHT: 'estela-layout-bottom-height',
  SHORTCUTS: 'estela_shortcuts',
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
