export type PluginCategory =
  | 'productivity'
  | 'quality'
  | 'git'
  | 'testing'
  | 'automation'
  | 'integrations'

export interface PluginManifest {
  id: string
  name: string
  description: string
  category: PluginCategory
  version: string
  author: string
  featured: boolean
  hasSettings: boolean
  tags: string[]
}

export interface PluginInstallState {
  pluginId: string
  installedAt: number
}
