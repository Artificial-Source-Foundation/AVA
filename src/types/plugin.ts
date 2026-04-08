export type PluginCategory = 'workflow' | 'quality' | 'integration'

/** Granular permission categories for plugin sandboxing */
export type PluginPermission = 'fs' | 'network' | 'shell' | 'clipboard'

/** Human-readable labels and risk levels for each permission */
export const PLUGIN_PERMISSION_META: Record<
  PluginPermission,
  { label: string; description: string; risk: 'low' | 'medium' | 'high' }
> = {
  fs: { label: 'File System', description: 'Read and write files on disk', risk: 'medium' },
  network: { label: 'Network', description: 'Make HTTP requests and fetch URLs', risk: 'medium' },
  shell: { label: 'Shell', description: 'Execute shell commands', risk: 'high' },
  clipboard: { label: 'Clipboard', description: 'Access system clipboard', risk: 'low' },
}

/** Permissions considered sensitive — require user confirmation before install */
export const SENSITIVE_PERMISSIONS: PluginPermission[] = ['shell', 'network']

export interface PluginCatalogItem {
  id: string
  name: string
  description: string
  category: PluginCategory
  version: string
  source: 'official' | 'community'
  trust: 'verified' | 'reviewed'
  changelogSummary: string
  repo?: string
  downloadUrl?: string
  readme?: string
  minVersion?: string
  screenshots?: string[]
  lastUpdated?: string
  permissions?: PluginPermission[]
  downloads?: number
  rating?: number
  ratingCount?: number
  author?: string
  homepage?: string
  publishedAt?: string
}

export interface PluginManifest {
  name: string
  version: string
  main: string
  description?: string
  author?: string
  permissions?: PluginPermission[]
}

export type PluginSourceType = 'catalog' | 'git' | 'local-link'
export type PluginScope = 'global' | 'project'

export interface PluginState {
  installed: boolean
  enabled: boolean
  version?: string
  installedAt?: number
  installPath?: string
  sourceType?: PluginSourceType
  sourceUrl?: string
  scope?: PluginScope
}

export interface PluginAppEvent {
  event: string
  payload: unknown
}

export interface PluginMountSpec {
  id: string
  location: string
  label: string
  description?: string
}

export interface PluginMountRegistration {
  plugin: string
  mount: PluginMountSpec
}

export interface PluginHostInvokeResult {
  result: unknown
  emittedEvents: PluginAppEvent[]
}
