import type { PluginCatalogItem } from '../types/plugin'

export type PluginCatalogStatus = 'idle' | 'syncing' | 'ready' | 'error'

export const PLUGIN_CATALOG: PluginCatalogItem[] = [
  {
    id: 'task-planner',
    name: 'Task Planner',
    description: 'Breaks goals into actionable implementation steps.',
    category: 'workflow',
    version: '1.4.0',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Added milestone templates and dependency hints.',
  },
  {
    id: 'test-guard',
    name: 'Test Guard',
    description: 'Runs verification checks before completion.',
    category: 'quality',
    version: '0.9.3',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Expanded flaky-test diagnostics and retry classification.',
  },
  {
    id: 'git-helper',
    name: 'Git Helper',
    description: 'Guides commit hygiene and pull request workflows.',
    category: 'workflow',
    version: '1.1.2',
    source: 'community',
    trust: 'reviewed',
    changelogSummary: 'Improved commit message hints and branch naming checks.',
  },
  {
    id: 'mcp-inspector',
    name: 'MCP Inspector',
    description: 'Inspects and validates MCP server wiring.',
    category: 'integration',
    version: '0.7.0',
    source: 'official',
    trust: 'verified',
    changelogSummary: 'Added auth flow checks and endpoint handshake diagnostics.',
  },
]

export const FEATURED_PLUGIN_IDS = ['task-planner', 'test-guard']

export async function syncPluginCatalog(): Promise<PluginCatalogItem[]> {
  if (typeof navigator !== 'undefined' && navigator.onLine === false) {
    throw new Error('You appear to be offline. Reconnect to sync plugin metadata.')
  }

  await Promise.resolve()
  return PLUGIN_CATALOG
}
