import type { LLMClientFactory, ProviderManifest } from './dynamic-loader.js'
import { loadProvider } from './dynamic-loader.js'

export * from './dynamic-loader.js'
export * from './normalize.js'

export async function resolveProviderFactory(
  name: string,
  bundledProviders: Record<string, LLMClientFactory>,
  registry: ProviderManifest[] = []
): Promise<LLMClientFactory> {
  const bundled = bundledProviders[name]
  if (bundled) {
    return bundled
  }

  return loadProvider(name, registry)
}
