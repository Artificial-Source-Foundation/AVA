/**
 * Environment Variable Bridge for Tauri Desktop App
 *
 * In Tauri's WebView context, `process.env` is undefined (unlike Node.js).
 * This module provides a bridge to read environment variables from the Rust backend.
 *
 * Usage:
 *   import { env } from './env-bridge'
 *   const apiKey = env.TAVILY_API_KEY
 *
 * Or polyfill process.env globally:
 *   import './env-bridge/polyfill'
 *   // Now process.env is available globally
 */

import { invoke } from '@tauri-apps/api/core'

// Cache for environment variables to avoid repeated IPC calls
const envCache = new Map<string, string | undefined>()

// Track which env vars we've already fetched to avoid duplicate requests
const pendingFetches = new Map<string, Promise<string | null>>()

/**
 * Fetch an environment variable from the Rust backend.
 * Only specific env vars are allowed (see src-tauri/src/commands/env.rs).
 *
 * @param name - Environment variable name
 * @returns The value or undefined if not set/not allowed
 */
export async function getEnvVar(name: string): Promise<string | undefined> {
  // Check cache first
  if (envCache.has(name)) {
    return envCache.get(name)
  }

  // Check if we already have a pending fetch
  if (pendingFetches.has(name)) {
    const result = await pendingFetches.get(name)!
    return result ?? undefined
  }

  // Fetch from Rust backend
  const fetchPromise = invoke<string | null>('get_env_var', { name })
    .then((value) => {
      envCache.set(name, value ?? undefined)
      pendingFetches.delete(name)
      return value
    })
    .catch((err) => {
      console.warn(`Failed to get env var ${name}:`, err)
      envCache.set(name, undefined)
      pendingFetches.delete(name)
      return null
    })

  pendingFetches.set(name, fetchPromise)
  const result = await fetchPromise
  return result ?? undefined
}

/**
 * Synchronous version that returns cached value or undefined.
 * Use getEnvVar() for the initial fetch, then use this for subsequent reads.
 *
 * @param name - Environment variable name
 * @returns Cached value or undefined
 */
export function getEnvVarSync(name: string): string | undefined {
  return envCache.get(name)
}

/**
 * Pre-fetch multiple environment variables in parallel.
 * Useful for initializing the environment at app startup.
 *
 * @param names - Array of environment variable names
 */
export async function prefetchEnvVars(names: string[]): Promise<void> {
  await Promise.all(names.map((name) => getEnvVar(name)))
}

/**
 * Clear the environment variable cache.
 * Useful if environment variables might change during runtime.
 */
export function clearEnvCache(): void {
  envCache.clear()
}

/**
 * Create a proxy object that acts like process.env.
 * This allows usage like: env.TAVILY_API_KEY
 */
export const env = new Proxy({} as Record<string, string | undefined>, {
  get(_target, prop: string | symbol): string | undefined {
    if (typeof prop === 'string') {
      return getEnvVarSync(prop)
    }
    return undefined
  },
  set(): boolean {
    throw new Error('process.env is read-only in Tauri context')
  },
  has(_target, prop: string | symbol): boolean {
    if (typeof prop === 'string') {
      return getEnvVarSync(prop) !== undefined
    }
    return false
  },
})

/**
 * Common environment variables to prefetch at startup.
 * These are the env vars most commonly used by tools.
 */
export const COMMON_ENV_VARS = [
  // LLM Providers
  'ANTHROPIC_API_KEY',
  'OPENAI_API_KEY',
  'GOOGLE_API_KEY',
  'GROQ_API_KEY',
  'MISTRAL_API_KEY',
  'DEEPSEEK_API_KEY',
  'XAI_API_KEY',
  'COHERE_API_KEY',
  'TOGETHER_API_KEY',
  'OPENROUTER_API_KEY',
  // Search tools
  'TAVILY_API_KEY',
  'EXA_API_KEY',
  'SERP_API_KEY',
  'BING_API_KEY',
  // AVA specific
  'AVA_DEBUG',
  'AVA_LOG_LEVEL',
]

/**
 * Initialize the environment bridge.
 * Call this at app startup to prefetch common env vars.
 */
export async function initEnvBridge(): Promise<void> {
  await prefetchEnvVars(COMMON_ENV_VARS)
}
