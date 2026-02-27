/**
 * Integrations extension — external API integrations.
 *
 * Checks for API keys in credentials and registers search providers.
 * Currently supports Exa and Tavily search APIs.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { SearchResult, WebSearchProvider } from './types.js'

function createExaProvider(apiKey: string): WebSearchProvider {
  return {
    name: 'exa',
    async search(query: string, maxResults = 5): Promise<SearchResult[]> {
      const response = await fetch('https://api.exa.ai/search', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'x-api-key': apiKey },
        body: JSON.stringify({ query, numResults: maxResults, type: 'neural' }),
      })
      if (!response.ok) return []
      const data = (await response.json()) as {
        results?: Array<{ title: string; url: string; text?: string; score?: number }>
      }
      return (data.results ?? []).map((r) => ({
        title: r.title,
        url: r.url,
        snippet: r.text ?? '',
        score: r.score,
      }))
    },
  }
}

function createTavilyProvider(apiKey: string): WebSearchProvider {
  return {
    name: 'tavily',
    async search(query: string, maxResults = 5): Promise<SearchResult[]> {
      const response = await fetch('https://api.tavily.com/search', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ api_key: apiKey, query, max_results: maxResults }),
      })
      if (!response.ok) return []
      const data = (await response.json()) as {
        results?: Array<{ title: string; url: string; content?: string; score?: number }>
      }
      return (data.results ?? []).map((r) => ({
        title: r.title,
        url: r.url,
        snippet: r.content ?? '',
        score: r.score,
      }))
    },
  }
}

export function activate(api: ExtensionAPI): Disposable {
  const providers: WebSearchProvider[] = []
  const disposables: Disposable[] = []

  void (async () => {
    // Check for Exa API key
    const exaKey = await api.platform.credentials.get('exa-api-key')
    if (exaKey) {
      providers.push(createExaProvider(exaKey))
      api.log.debug('Exa search provider registered')
    }

    // Check for Tavily API key
    const tavilyKey = await api.platform.credentials.get('tavily-api-key')
    if (tavilyKey) {
      providers.push(createTavilyProvider(tavilyKey))
      api.log.debug('Tavily search provider registered')
    }

    api.emit('integrations:ready', {
      providers: providers.map((p) => p.name),
    })
  })()

  // Handle search requests
  disposables.push(
    api.on('integrations:search', (data) => {
      const {
        query,
        maxResults,
        provider: preferredProvider,
      } = data as {
        query: string
        maxResults?: number
        provider?: string
      }

      const provider = preferredProvider
        ? providers.find((p) => p.name === preferredProvider)
        : providers[0]

      if (!provider) {
        api.emit('integrations:search-result', {
          query,
          results: [],
          error: 'No provider available',
        })
        return
      }

      void provider.search(query, maxResults).then((results) => {
        api.emit('integrations:search-result', { query, results })
      })
    })
  )

  api.log.debug('Integrations extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      providers.length = 0
    },
  }
}
