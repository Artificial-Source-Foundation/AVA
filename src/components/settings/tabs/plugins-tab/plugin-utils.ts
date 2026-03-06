/**
 * Plugin Tab Utilities
 *
 * Shared helpers for plugin-tab components: formatting, color, labels.
 */

import { PLUGIN_PERMISSION_META, type PluginPermission } from '../../../../types/plugin'

/** Risk-based color for a plugin permission badge */
export function permissionColor(perm: PluginPermission): string {
  const risk = PLUGIN_PERMISSION_META[perm]?.risk ?? 'low'
  if (risk === 'high') return 'var(--error)'
  if (risk === 'medium') return 'var(--warning)'
  return 'var(--text-muted)'
}

/** Humanize download counts (e.g. 1200 → "1.2K") */
export function formatDownloads(n?: number): string {
  if (!n) return '0'
  if (n >= 1000) return `${(n / 1000).toFixed(1)}K`
  return String(n)
}

/** Capitalize first letter of a category slug */
export function categoryLabel(category: string): string {
  return category.charAt(0).toUpperCase() + category.slice(1)
}

/** Format a timestamp into HH:MM */
export function formatSyncTime(timestamp: number | null): string {
  if (!timestamp) return 'never'
  return new Date(timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
}

/** Derive a human-readable label for a plugin source type */
export function sourceLabel(sourceType: string | undefined): string {
  if (sourceType === 'git') return 'Git'
  if (sourceType === 'local-link') return 'Local'
  return 'Catalog'
}
