/**
 * Integrations types.
 */

export interface SearchResult {
  title: string
  url: string
  snippet: string
  score?: number
}

export interface WebSearchProvider {
  name: string
  search(query: string, maxResults?: number): Promise<SearchResult[]>
}

export interface IntegrationConfig {
  enabledProviders: string[]
}

export const DEFAULT_INTEGRATION_CONFIG: IntegrationConfig = {
  enabledProviders: [],
}
