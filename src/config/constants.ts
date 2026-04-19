/**
 * Application Constants
 * Centralized configuration values
 */

// Storage keys for localStorage
export const STORAGE_KEYS = {
  CREDENTIALS: 'ava_credentials',
  OAUTH_VERIFIER: 'ava_oauth_verifier',
  OAUTH_STATE: 'ava_oauth_state',
  LAST_SESSION: 'ava_last_session',
  LAST_SESSION_BY_PROJECT: 'ava_last_session_by_project',
  LAST_PROJECT: 'ava_last_project',
  SETTINGS: 'ava_settings',
  PANEL_SIZES: 'ava-panel-sizes',
  SIDEBAR_COLLAPSED: 'ava-sidebar-collapsed',
  LAYOUT_ACTIVITY: 'ava-layout-activity',
  LAYOUT_SIDEBAR_VISIBLE: 'ava-layout-sidebar-visible',
  LAYOUT_RIGHT_VISIBLE: 'ava-layout-right-visible',
  LAYOUT_BOTTOM_VISIBLE: 'ava-layout-bottom-visible',
  LAYOUT_BOTTOM_HEIGHT: 'ava-layout-bottom-height',
  LAYOUT_PROJECT_HUB_VISIBLE: 'ava-layout-project-hub-visible',
  SHORTCUTS: 'ava_shortcuts',
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
