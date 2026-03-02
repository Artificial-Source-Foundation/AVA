/**
 * Well-Known Organization Config
 *
 * Fetches configuration from `/.well-known/ava` endpoints,
 * allowing organizations to publish default AVA settings.
 */

export interface WellKnownConfig {
  name?: string
  provider?: string
  model?: string
  instructions?: string
  mcpServers?: Array<{ name: string; url: string }>
}

const TIMEOUT_MS = 5000

/**
 * Fetch organization configuration from `https://<domain>/.well-known/ava`.
 * Returns parsed config or null on any failure (timeout, network, invalid JSON).
 */
export async function fetchWellKnownConfig(
  domain: string,
  signal?: AbortSignal
): Promise<WellKnownConfig | null> {
  const url = `https://${domain}/.well-known/ava`

  const controller = new AbortController()
  const timeout = setTimeout(() => controller.abort(), TIMEOUT_MS)

  // If caller provides a signal, abort when it fires
  const onCallerAbort = (): void => controller.abort()
  signal?.addEventListener('abort', onCallerAbort)

  try {
    const response = await fetch(url, {
      method: 'GET',
      headers: { Accept: 'application/json' },
      signal: controller.signal,
    })

    if (!response.ok) return null

    const contentType = response.headers.get('content-type') ?? ''
    if (!contentType.includes('json')) return null

    const data: unknown = await response.json()
    if (!data || typeof data !== 'object') return null

    return data as WellKnownConfig
  } catch {
    return null
  } finally {
    clearTimeout(timeout)
    signal?.removeEventListener('abort', onCallerAbort)
  }
}
