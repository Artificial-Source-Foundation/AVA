/**
 * OAuth Authentication Types
 * Based on OpenCode's auth plugin pattern and Plandex's claude_max implementation
 */

// ============================================================================
// OAuth Provider Types
// ============================================================================

/** Providers that support OAuth authentication */
export type OAuthProvider = 'openai' | 'google' | 'copilot'

// ============================================================================
// Token Types
// ============================================================================

/** OAuth token storage */
export interface OAuthTokens {
  accessToken: string
  refreshToken: string
  expiresAt: number // Timestamp in milliseconds
  provider: OAuthProvider
  /** For OpenAI - the ChatGPT account ID */
  accountId?: string
}

/** PKCE challenge data */
export interface PKCEChallenge {
  verifier: string
  challenge: string
}

// ============================================================================
// OAuth Configuration
// ============================================================================

/** OpenAI OAuth configuration */
export const OPENAI_OAUTH_CONFIG = {
  clientId: 'app_EMoamEEZ73f0CkXaXp7hrann',
  issuer: 'https://auth.openai.com',
  scopes: 'openid profile email offline_access',
  apiEndpoint: 'https://chatgpt.com/backend-api/codex/responses',
  callbackPort: 1455,
} as const

/** Google OAuth configuration (Antigravity/Gemini) */
export const GOOGLE_OAUTH_CONFIG = {
  clientId: '681255809395-oo8ft2oprdrnp9e3aqf6av3hmdib135j.apps.googleusercontent.com',
  clientSecret: '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',
  scopes: [
    'https://www.googleapis.com/auth/cloud-platform',
    'https://www.googleapis.com/auth/userinfo.email',
    'https://www.googleapis.com/auth/userinfo.profile',
  ],
  redirectUrl: 'https://codeassist.google.com/authcode',
  tokenUrl: 'https://oauth2.googleapis.com/token',
  authUrl: 'https://accounts.google.com/o/oauth2/v2/auth',
} as const

/** GitHub Copilot OAuth configuration */
export const COPILOT_OAUTH_CONFIG = {
  clientId: 'Ov23li8tweQw6odWQebz',
  scope: 'read:user',
  deviceCodeUrl: 'https://github.com/login/device/code',
  accessTokenUrl: 'https://github.com/login/oauth/access_token',
  verificationUrl: 'https://github.com/login/device',
  pollingMarginMs: 3000, // Safety buffer for polling
} as const

// ============================================================================
// Auth Result Types
// ============================================================================

/** Result from OAuth authorization */
export interface OAuthAuthorizationResult {
  url: string
  instructions: string
  method: 'auto' | 'code'
  /** Called after user completes authorization */
  callback: (code?: string) => Promise<OAuthTokenResult>
}

/** Result from OAuth token exchange */
export type OAuthTokenResult =
  | {
      type: 'success'
      accessToken: string
      refreshToken: string
      expiresAt: number
      accountId?: string
    }
  | {
      type: 'failed'
      error: string
    }

// ============================================================================
// Stored Auth Types
// ============================================================================

/** Stored authentication info - can be API key or OAuth */
export type StoredAuth =
  | {
      type: 'api-key'
      key: string
    }
  | {
      type: 'oauth'
      accessToken: string
      refreshToken: string
      expiresAt: number
      accountId?: string
    }
