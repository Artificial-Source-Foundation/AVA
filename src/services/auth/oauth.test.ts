import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { LLMProvider } from '../../types/llm'

// Mock @tauri-apps/plugin-shell before importing oauth
vi.mock('@tauri-apps/plugin-shell', () => ({
  open: vi.fn(),
}))

import { open } from '@tauri-apps/plugin-shell'
import { getOAuthConfig, isOAuthSupported, startOAuthFlow, storeOAuthCredentials } from './oauth'

// ============================================================================
// isOAuthSupported
// ============================================================================

describe('isOAuthSupported', () => {
  it('returns true for supported providers', () => {
    for (const p of ['anthropic', 'openai', 'google', 'copilot'] as LLMProvider[]) {
      expect(isOAuthSupported(p)).toBe(true)
    }
  })

  it('returns false for unsupported providers', () => {
    for (const p of [
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
    expect(anthropic!.clientId).toBe('claude-code')
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
    const raw = localStorage.getItem('estela_credentials')
    expect(raw).not.toBeNull()
    const parsed = JSON.parse(raw!)
    expect(parsed.anthropic.provider).toBe('anthropic')
    expect(parsed.anthropic.type).toBe('oauth-token')
    expect(parsed.anthropic.value).toBe('tok-abc')
  })

  it('stores multiple providers without overwriting', () => {
    storeOAuthCredentials('anthropic', { accessToken: 'tok-a' })
    storeOAuthCredentials('openai', { accessToken: 'tok-o' })
    const parsed = JSON.parse(localStorage.getItem('estela_credentials')!)
    expect(parsed.anthropic.value).toBe('tok-a')
    expect(parsed.openai.value).toBe('tok-o')
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

  it('opens browser for PKCE providers', async () => {
    const mockOpen = vi.mocked(open)
    await startOAuthFlow('anthropic')
    expect(mockOpen).toHaveBeenCalledOnce()
    const url = mockOpen.mock.calls[0][0]
    expect(url).toContain('claude.ai/oauth/authorize')
    expect(url).toContain('code_challenge_method=S256')
  })

  it('returns DeviceCodeResponse for copilot', async () => {
    const mockResponse = {
      ok: true,
      json: () =>
        Promise.resolve({
          device_code: 'DC-123',
          user_code: 'ABCD-1234',
          verification_uri: 'https://github.com/login/device',
          expires_in: 900,
          interval: 5,
        }),
    }
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(mockResponse))

    const result = await startOAuthFlow('copilot')
    expect(result).toBeDefined()
    expect('deviceCode' in result).toBe(true)
    const deviceResult = result as import('./oauth').DeviceCodeResponse
    expect(deviceResult.deviceCode).toBe('DC-123')
    expect(deviceResult.userCode).toBe('ABCD-1234')
    expect(deviceResult.verificationUri).toBe('https://github.com/login/device')

    vi.unstubAllGlobals()
  })
})
