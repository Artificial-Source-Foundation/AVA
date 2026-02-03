/**
 * Authentication Module
 * Supports both API keys and OAuth for LLM providers
 */

// OAuth providers
export { authorizeAnthropic, needsRefresh, refreshAnthropicToken } from './anthropic-oauth.js'
export { authorizeCopilot, refreshCopilotToken } from './copilot-oauth.js'
export { authorizeGoogle, refreshGoogleToken } from './google-oauth.js'
// Auth manager
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
export { authorizeOpenAI, refreshOpenAIToken } from './openai-oauth.js'
// PKCE utilities
export { generateCodeChallenge, generateCodeVerifier, generatePKCE, generateState } from './pkce.js'
// Types
export * from './types.js'
