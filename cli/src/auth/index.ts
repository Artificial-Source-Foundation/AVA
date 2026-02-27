/**
 * Authentication Module (moved from @ava/core)
 * Supports both API keys and OAuth for LLM providers
 */

export {
  type AuthStatus,
  completeOAuthFlow,
  getAccountId,
  getAuthStatus,
  getStoredAuth,
  getValidAccessToken,
  isAuthenticated,
  removeStoredAuth,
  setStoredAuth,
  startOAuthFlow,
} from './manager.js'
export * from './types.js'
