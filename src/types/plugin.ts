export type PluginCategory = 'workflow' | 'quality' | 'integration'

export interface PluginCatalogItem {
  id: string
  name: string
  description: string
  category: PluginCategory
}

export interface PluginState {
  installed: boolean
  enabled: boolean
}
