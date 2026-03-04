/**
 * Safe environment variable accessor
 *
 * Works in both Node.js and Tauri WebView contexts.
 * In Tauri, this requires the env-bridge polyfill to be loaded.
 */

/**
 * Get an environment variable value.
 * Works in both Node.js and Tauri environments.
 *
 * @param key - Environment variable name
 * @returns The value or undefined if not set
 */
export function getEnv(key: string): string | undefined {
  // Check if we're in a Node.js environment
  if (typeof process !== 'undefined' && process.env) {
    return process.env[key]
  }

  // In browser/Tauri without polyfill
  return undefined
}

/**
 * Check if an environment variable is set.
 *
 * @param key - Environment variable name
 * @returns true if the variable exists and is not empty
 */
export function hasEnv(key: string): boolean {
  const value = getEnv(key)
  return value !== undefined && value.length > 0
}
