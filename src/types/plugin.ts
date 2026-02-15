export type PluginCategory = 'workflow' | 'quality' | 'integration'

export interface PluginCatalogItem {
  id: string
  name: string
  description: string
  category: PluginCategory
  version: string
  source: 'official' | 'community'
  trust: 'verified' | 'reviewed'
  changelogSummary: string
}

export interface PluginState {
  installed: boolean
  enabled: boolean
}
