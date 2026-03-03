/**
 * Polyfill for process.env in Tauri WebView context
 *
 * This module polyfills the Node.js process.env object for the Tauri desktop app.
 * It must be imported early in the app lifecycle (before any tool usage).
 *
 * Usage: import './env-bridge/polyfill'
 */

import { env, getEnvVar, initEnvBridge } from './env-bridge'

// Only polyfill if process.env is not already available
if (typeof window !== 'undefined' && (!window.process || !window.process.env)) {
  // Create process object if it doesn't exist
  if (!window.process) {
    window.process = {} as NodeJS.Process
  }

  // Set up the env proxy
  window.process.env = new Proxy({} as Record<string, string | undefined>, {
    get(_target, prop: string | symbol): string | undefined {
      if (typeof prop === 'string') {
        return env[prop]
      }
      return undefined
    },
    set(): boolean {
      console.warn('process.env is read-only in Tauri context')
      return false
    },
    has(_target, prop: string | symbol): boolean {
      if (typeof prop === 'string') {
        return env[prop] !== undefined
      }
      return false
    },
    ownKeys(): ArrayLike<string | symbol> {
      return Object.keys(env)
    },
    getOwnPropertyDescriptor(_target, prop: string | symbol): PropertyDescriptor | undefined {
      if (typeof prop === 'string') {
        const value = env[prop]
        if (value !== undefined) {
          return {
            value,
            writable: false,
            enumerable: true,
            configurable: true,
          }
        }
      }
      return undefined
    },
  })

  // Also expose getEnvVar for async access
  // @ts-expect-error - Adding custom property
  window.process.getEnvVar = getEnvVar

  // Initialize the bridge
  initEnvBridge().catch((err) => {
    console.error('Failed to initialize env bridge:', err)
  })
}

// Re-export for convenience
export { env, getEnvVar, initEnvBridge }
