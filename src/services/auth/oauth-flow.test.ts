import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const mockSetStoredAuth = vi.fn().mockResolvedValue(undefined)

vi.mock('@tauri-apps/plugin-opener', () => ({
  openUrl: vi.fn(),
}))

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(),
}))

vi.mock('../../lib/auth-helpers', () => ({
  setStoredAuth: (...args: unknown[]) => mockSetStoredAuth(...args),
  removeStoredAuth: vi.fn().mockResolvedValue(undefined),
}))

vi.mock('../logger', () => ({
  logDebug: vi.fn(),
  logInfo: vi.fn(),
  logWarn: vi.fn(),
  logError: vi.fn(),
}))

import {
  checkStoredOAuth,
  clearProviderCredentials,
} from '../../components/settings/tabs/providers-tab-helpers'
import { extractAccountId, storeOAuthCredentials } from './oauth'

function base64UrlEncode(value: string): string {
  return Buffer.from(value)
    .toString('base64')
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '')
}

function makeJwt(payload: Record<string, unknown>): string {
  const header = base64UrlEncode(JSON.stringify({ alg: 'none', typ: 'JWT' }))
  const body = base64UrlEncode(JSON.stringify(payload))
  return `${header}.${body}.`
}

describe('oauth flow reconnect/storage', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.clearAllMocks()
  })

  afterEach(() => {
    localStorage.clear()
  })

  it('stores openai oauth with accountId and reports oauth as connected', async () => {
    const idToken = makeJwt({ chatgpt_account_id: 'acct-reconnect-1' })

    await storeOAuthCredentials('openai', {
      accessToken: 'openai-token-1',
      refreshToken: 'openai-refresh-1',
      expiresAt: Date.now() + 3600_000,
      idToken,
    })

    expect(checkStoredOAuth('openai')).toBe(true)
    expect(mockSetStoredAuth).toHaveBeenCalledWith(
      'openai',
      expect.objectContaining({
        type: 'oauth',
        accessToken: 'openai-token-1',
        refreshToken: 'openai-refresh-1',
        accountId: 'acct-reconnect-1',
      })
    )
  })

  it('supports clear and reconnect flow for openai', async () => {
    await storeOAuthCredentials('openai', {
      accessToken: 'openai-token-1',
      refreshToken: 'openai-refresh-1',
      expiresAt: Date.now() + 3600_000,
      idToken: makeJwt({ organizations: [{ id: 'org-reconnect-1' }] }),
    })
    expect(checkStoredOAuth('openai')).toBe(true)

    clearProviderCredentials('openai')
    expect(checkStoredOAuth('openai')).toBe(false)

    await storeOAuthCredentials('openai', {
      accessToken: 'openai-token-2',
      refreshToken: 'openai-refresh-2',
      expiresAt: Date.now() + 3600_000,
      idToken: makeJwt({ chatgpt_account_id: 'acct-reconnect-2' }),
    })

    expect(checkStoredOAuth('openai')).toBe(true)
    expect(mockSetStoredAuth).toHaveBeenCalledTimes(2)
  })

  it('surfaces native auth-store failures instead of claiming OAuth connected', async () => {
    mockSetStoredAuth.mockRejectedValueOnce(new Error('native bridge write failed'))

    await expect(
      storeOAuthCredentials('openai', {
        accessToken: 'openai-token-3',
        refreshToken: 'openai-refresh-3',
        expiresAt: Date.now() + 3600_000,
      })
    ).rejects.toThrow('native bridge write failed')
    expect(checkStoredOAuth('openai')).toBe(false)
  })

  it('extractAccountId returns undefined for malformed organizations claim', () => {
    const idToken = makeJwt({ organizations: [{ name: 'no-id' }] })
    expect(extractAccountId(idToken)).toBeUndefined()
  })
})
