import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const mockSetStoredAuth = vi.fn().mockResolvedValue(undefined)
const mockSyncProviderCredentials = vi.fn()

vi.mock('@tauri-apps/plugin-opener', () => ({
  openUrl: vi.fn(),
}))

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(),
}))

vi.mock('@estela/core', () => ({
  setStoredAuth: (...args: unknown[]) => mockSetStoredAuth(...args),
}))

vi.mock('../../stores/settings', () => ({
  syncProviderCredentials: (...args: unknown[]) => mockSyncProviderCredentials(...args),
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

    storeOAuthCredentials('openai', {
      accessToken: 'openai-token-1',
      refreshToken: 'openai-refresh-1',
      expiresAt: Date.now() + 3600_000,
      idToken,
    })

    await Promise.resolve()

    expect(checkStoredOAuth('openai')).toBe(true)
    expect(mockSetStoredAuth).toHaveBeenCalledWith(
      'openai',
      expect.objectContaining({
        type: 'oauth',
        accessToken: 'openai-token-1',
        accountId: 'acct-reconnect-1',
      })
    )
  })

  it('supports clear and reconnect flow for openai', async () => {
    storeOAuthCredentials('openai', {
      accessToken: 'openai-token-1',
      refreshToken: 'openai-refresh-1',
      expiresAt: Date.now() + 3600_000,
      idToken: makeJwt({ organizations: [{ id: 'org-reconnect-1' }] }),
    })
    await Promise.resolve()
    expect(checkStoredOAuth('openai')).toBe(true)

    clearProviderCredentials('openai')
    expect(checkStoredOAuth('openai')).toBe(false)

    storeOAuthCredentials('openai', {
      accessToken: 'openai-token-2',
      refreshToken: 'openai-refresh-2',
      expiresAt: Date.now() + 3600_000,
      idToken: makeJwt({ chatgpt_account_id: 'acct-reconnect-2' }),
    })
    await Promise.resolve()

    expect(checkStoredOAuth('openai')).toBe(true)
    expect(mockSetStoredAuth).toHaveBeenCalledTimes(2)
  })

  it('stores anthropic minted key without writing oauth core auth object', () => {
    storeOAuthCredentials('anthropic', {
      accessToken: 'sk-ant-minted-1',
    })

    expect(mockSyncProviderCredentials).toHaveBeenCalledWith('anthropic', 'sk-ant-minted-1')
    expect(mockSetStoredAuth).not.toHaveBeenCalled()
  })

  it('extractAccountId returns undefined for malformed organizations claim', () => {
    const idToken = makeJwt({ organizations: [{ name: 'no-id' }] })
    expect(extractAccountId(idToken)).toBeUndefined()
  })
})
