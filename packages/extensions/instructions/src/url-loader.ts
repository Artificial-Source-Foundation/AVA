/**
 * URL instruction loader — fetches instruction content from remote URLs.
 */

import type { SimpleLogger } from '@ava/core-v2/logger'
import type { InstructionFile } from './types.js'

/**
 * Fetch instruction content from a URL.
 *
 * Uses a 5-second timeout. Returns null if the fetch fails for any reason.
 */
export async function fetchUrlInstruction(
  url: string,
  log?: SimpleLogger,
  signal?: AbortSignal
): Promise<InstructionFile | null> {
  try {
    const timeoutSignal = AbortSignal.timeout(5000)
    const combinedSignal = signal ? AbortSignal.any([timeoutSignal, signal]) : timeoutSignal

    const response = await fetch(url, { signal: combinedSignal })

    if (!response.ok) {
      log?.warn(`Failed to fetch instruction URL ${url}: HTTP ${response.status}`)
      return null
    }

    const content = await response.text()
    return {
      path: url,
      content,
      scope: 'remote',
      priority: 0,
    }
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    log?.warn(`Failed to fetch instruction URL ${url}: ${message}`)
    return null
  }
}
