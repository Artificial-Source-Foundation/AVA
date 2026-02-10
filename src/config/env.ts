/**
 * Environment Configuration
 * Validates and provides typed access to environment variables
 */

interface EnvConfig {
  // API keys (for development - prefer UI-based storage)
  ANTHROPIC_API_KEY?: string
  OPENROUTER_API_KEY?: string
  OPENAI_API_KEY?: string

  // Feature flags
  DEBUG_MODE: boolean
}

/**
 * Get validated environment configuration
 */
function getEnvConfig(): EnvConfig {
  return {
    ANTHROPIC_API_KEY: import.meta.env.VITE_ANTHROPIC_API_KEY,
    OPENROUTER_API_KEY: import.meta.env.VITE_OPENROUTER_API_KEY,
    OPENAI_API_KEY: import.meta.env.VITE_OPENAI_API_KEY,
    DEBUG_MODE: import.meta.env.DEV || import.meta.env.VITE_DEBUG === 'true',
  }
}

/**
 * Check for required environment variables and log warnings.
 * OAuth client IDs are hardcoded in oauth.ts — no env vars needed.
 * API keys are optional (can be set via UI).
 */
export function validateEnv(): void {
  // Currently no required env vars — API keys and OAuth are configured in UI.
  // Keep this function as a hook for future env validation.
  getEnvConfig()
}
