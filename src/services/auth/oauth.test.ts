import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { LLMProvider } from '../../types/llm'

// Mock Tauri modules before importing oauth
vi.mock('@tauri-apps/plugin-opener', () => ({
  openUrl: vi.fn(),
}))
vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(async (cmd: string) => {
    if (cmd === 'oauth_listen') {
      // Read the state that storePKCE just wrote to localStorage
      const state = localStorage.getItem('ava_oauth_state_anthropic') || 'mock-state'
      return { code: 'test-auth-code', state }
    }
    if (cmd === 'oauth_copilot_device_start') {
      return {
        device_code: 'DC-123',
        user_code: 'ABCD-1234',
        verification_uri: 'https://github.com/login/device',
        expires_in: 900,
        interval: 5,
      }
    }
    if (cmd === 'oauth_copilot_device_poll') {
      return { error: 'authorization_pending' }
    }
    return null
  }),
}))
vi.mock('@ava/core', () => ({
  setStoredAuth: vi.fn().mockResolvedValue(undefined),
}))
vi.mock('../../stores/settings', () => ({
  syncProviderCredentials: vi.fn(),
}))
vi.mock('../logger', () => ({
  logDebug: vi.fn(),
  logInfo: vi.fn(),
  logWarn: vi.fn(),
  logError: vi.fn(),
}))

import { invoke } from '@tauri-apps/api/core'
import { openUrl } from '@tauri-apps/plugin-opener'
import {
  decodeJwtPayload,
  extractAccountId,
  getOAuthConfig,
  isOAuthSupported,
  startOAuthFlow,
  storeOAuthCredentials,
} from './oauth'

// ============================================================================
// isOAuthSupported
// ============================================================================

describe('isOAuthSupported', () => {
  it('returns true for supported providers', () => {
    for (const p of ['anthropic', 'openai', 'copilot'] as LLMProvider[]) {
      expect(isOAuthSupported(p)).toBe(true)
    }
  })

  it('returns false for unsupported providers', () => {
    for (const p of [
      'google',
      'xai',
      'mistral',
      'groq',
      'deepseek',
      'cohere',
      'together',
      'kimi',
      'glm',
      'ollama',
    ] as LLMProvider[]) {
      expect(isOAuthSupported(p)).toBe(false)
    }
  })
})

// ============================================================================
// getOAuthConfig
// ============================================================================

describe('getOAuthConfig', () => {
  it('returns config for supported providers', () => {
    const anthropic = getOAuthConfig('anthropic')
    expect(anthropic).not.toBeNull()
    expect(anthropic!.clientId).toBe('9d1c250a-e61b-44d9-88ed-5944d1962f5e')
    expect(anthropic!.flow).toBe('pkce')

    const copilot = getOAuthConfig('copilot')
    expect(copilot).not.toBeNull()
    expect(copilot!.flow).toBe('device-code')
  })

  it('returns null for unsupported providers', () => {
    expect(getOAuthConfig('ollama')).toBeNull()
    expect(getOAuthConfig('groq')).toBeNull()
  })
})

// ============================================================================
// storeOAuthCredentials
// ============================================================================

describe('storeOAuthCredentials', () => {
  beforeEach(() => localStorage.clear())
  afterEach(() => localStorage.clear())

  it('stores credentials in localStorage', () => {
    storeOAuthCredentials('anthropic', { accessToken: 'tok-abc' })
    const raw = localStorage.getItem('ava_credentials')
    expect(raw).not.toBeNull()
    const parsed = JSON.parse(raw!)
    expect(parsed.anthropic.provider).toBe('anthropic')
    expect(parsed.anthropic.type).toBe('oauth-token')
    expect(parsed.anthropic.value).toBe('tok-abc')
    expect(localStorage.getItem('estela_credentials')).toBe(raw)
  })

  it('stores multiple providers without overwriting', () => {
    storeOAuthCredentials('anthropic', { accessToken: 'tok-a' })
    storeOAuthCredentials('openai', { accessToken: 'tok-o' })
    const parsed = JSON.parse(localStorage.getItem('ava_credentials')!)
    expect(parsed.anthropic.value).toBe('tok-a')
    expect(parsed.openai.value).toBe('tok-o')
  })
})

// ============================================================================
// decodeJwtPayload / extractAccountId
// ============================================================================

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

describe('decodeJwtPayload', () => {
  it('decodes valid JWT payload', () => {
    const jwt = makeJwt({ sub: '123', chatgpt_account_id: 'acct-1' })
    const decoded = decodeJwtPayload(jwt)
    expect(decoded.sub).toBe('123')
    expect(decoded.chatgpt_account_id).toBe('acct-1')
  })

  it('returns empty object for invalid JWT', () => {
    expect(decodeJwtPayload('bad.token')).toEqual({})
    expect(decodeJwtPayload('not-a-jwt')).toEqual({})
  })
})

describe('extractAccountId', () => {
  it('extracts accountId from root claim', () => {
    const jwt = makeJwt({ chatgpt_account_id: 'acct-root' })
    expect(extractAccountId(jwt)).toBe('acct-root')
  })

  it('extracts accountId from organizations array', () => {
    const jwt = makeJwt({ organizations: [{ id: 'org-123' }] })
    expect(extractAccountId(jwt)).toBe('org-123')
  })

  it('returns undefined when claim missing', () => {
    const jwt = makeJwt({ sub: '123' })
    expect(extractAccountId(jwt)).toBeUndefined()
  })
})

// ============================================================================
// startOAuthFlow
// ============================================================================

describe('startOAuthFlow', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.restoreAllMocks()
  })
  afterEach(() => localStorage.clear())

  it('throws for unsupported providers', async () => {
    await expect(startOAuthFlow('ollama')).rejects.toThrow('OAuth not supported')
  })

  it('opens browser and completes PKCE flow for anthropic', async () => {
    // Mock fetch: first call = token exchange, second call = API key minting
    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce({
        ok: true,
        json: () =>
          Promise.resolve({
            access_token: 'oauth-access-tok',
            refresh_token: 'oauth-refresh-tok',
            expires_in: 3600,
          }),
      })
      .mockResolvedValueOnce({
        ok: true,
        json: () => Promise.resolve({ api_key: 'sk-ant-minted-key' }),
      })
    vi.stubGlobal('fetch', fetchMock)

    const mockOpenUrl = vi.mocked(openUrl)
    const result = await startOAuthFlow('anthropic')

    // Browser was opened with correct auth URL
    expect(mockOpenUrl).toHaveBeenCalledOnce()
    const url = mockOpenUrl.mock.calls[0][0]
    expect(url).toContain('claude.ai/oauth/authorize')
    expect(url).toContain('code_challenge_method=S256')

    // Tokens returned with minted API key (not raw OAuth token)
    expect('accessToken' in result).toBe(true)
    const tokens = result as import('./oauth').OAuthTokens
    expect(tokens.accessToken).toBe('sk-ant-minted-key')

    vi.unstubAllGlobals()
  })

  it('returns DeviceCodeResponse for copilot', async () => {
    const mockInvoke = vi.mocked(invoke)

    const result = await startOAuthFlow('copilot')
    expect(result).toBeDefined()
    expect('deviceCode' in result).toBe(true)
    const deviceResult = result as import('./oauth').DeviceCodeResponse
    expect(deviceResult.deviceCode).toBe('DC-123')
    expect(deviceResult.userCode).toBe('ABCD-1234')
    expect(deviceResult.verificationUri).toBe('https://github.com/login/device')
    expect(mockInvoke).toHaveBeenCalledWith('oauth_copilot_device_start', {
      clientId: 'Iv1.b507a08c87ecfe98',
      scope: 'read:user',
    })
  })
})
