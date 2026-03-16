/**
 * Extension Loader — Stub
 *
 * Extensions are now handled by the Rust backend.
 * This module is retained as a stub for backward compatibility.
 */

// Re-export plugin-related functions for backward compatibility
export { loadInstalledPlugins, reloadPlugin, watchPluginDirectory } from './plugin-loader'

export const BUILT_IN_EXTENSION_COUNT = 0

export async function loadAllExtensions(): Promise<() => void> {
  return () => {}
}
