/**
 * Environment Configuration
 * Validates and provides typed access to environment variables
 */

export interface EnvConfig {
  // OAuth configuration
  CODEX_CLIENT_ID?: string

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
export function getEnvConfig(): EnvConfig {
  return {
    CODEX_CLIENT_ID: import.meta.env.VITE_CODEX_CLIENT_ID,
    ANTHROPIC_API_KEY: import.meta.env.VITE_ANTHROPIC_API_KEY,
    OPENROUTER_API_KEY: import.meta.env.VITE_OPENROUTER_API_KEY,
    OPENAI_API_KEY: import.meta.env.VITE_OPENAI_API_KEY,
    DEBUG_MODE: import.meta.env.DEV || import.meta.env.VITE_DEBUG === 'true',
  }
}

/**
 * Check for required environment variables and log warnings
 * Called on app initialization
 */
export function validateEnv(): void {
  const config = getEnvConfig()
  const warnings: string[] = []

  // OAuth is optional but recommended
  if (!config.CODEX_CLIENT_ID) {
    warnings.push('VITE_CODEX_CLIENT_ID not set - OAuth login will be unavailable')
  }

  // API keys are optional (can be set via UI)
  // No warnings for missing API keys

  // Log warnings in development
  if (config.DEBUG_MODE && warnings.length > 0) {
    console.warn('[Estela] Environment warnings:')
    for (const w of warnings) {
      console.warn(`  - ${w}`)
    }
  }
}

/**
 * Check if app has any authentication method available
 */
export function hasAuthMethod(): boolean {
  const config = getEnvConfig()
  return !!(
    config.CODEX_CLIENT_ID ||
    config.ANTHROPIC_API_KEY ||
    config.OPENROUTER_API_KEY ||
    config.OPENAI_API_KEY
  )
}
