/**
 * Tests for the auth manager — dual-write, cleanup, and migration.
 */

import type { MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  completeOAuthFlow,
  getStoredAuth,
  migrateOAuthCredentials,
  removeStoredAuth,
} from './manager.js'
import type { OAuthTokenResult } from './types.js'

let mockPlatform: MockPlatform

beforeEach(() => {
  mockPlatform = installMockPlatform()
})

afterEach(() => {
  // Platform is reset by next installMockPlatform()
})

describe('completeOAuthFlow', () => {
  it('writes both CLI and core-v2 keys on success', async () => {
    const result: OAuthTokenResult = {
      type: 'success',
      accessToken: 'ghp_abc123',
      refreshToken: 'ghp_abc123',
      expiresAt: 0,
    }

    const ok = await completeOAuthFlow('copilot', result)
    expect(ok).toBe(true)

    // CLI format: auth-copilot → JSON blob
    const cliData = await mockPlatform.credentials.get('auth-copilot')
    expect(cliData).toBeTruthy()
    const parsed = JSON.parse(cliData!)
    expect(parsed.accessToken).toBe('ghp_abc123')

    // Core-v2 format: ava:copilot:oauth_token → plain string
    const coreToken = await mockPlatform.credentials.get('ava:copilot:oauth_token')
    expect(coreToken).toBe('ghp_abc123')
  })

  it('writes account_id to core-v2 when present', async () => {
    const result: OAuthTokenResult = {
      type: 'success',
      accessToken: 'tok_openai',
      refreshToken: 'ref_openai',
      expiresAt: Date.now() + 3_600_000,
      accountId: 'acct_12345',
    }

    await completeOAuthFlow('openai', result)

    expect(await mockPlatform.credentials.get('ava:openai:oauth_token')).toBe('tok_openai')
    expect(await mockPlatform.credentials.get('ava:openai:account_id')).toBe('acct_12345')
  })

  it('returns false on failed result without writing anything', async () => {
    const result: OAuthTokenResult = {
      type: 'failed',
      error: 'user cancelled',
    }

    const ok = await completeOAuthFlow('copilot', result)
    expect(ok).toBe(false)

    expect(await mockPlatform.credentials.get('auth-copilot')).toBeNull()
    expect(await mockPlatform.credentials.get('ava:copilot:oauth_token')).toBeNull()
  })
})

describe('removeStoredAuth', () => {
  it('deletes both CLI and core-v2 keys', async () => {
    // Seed both formats
    await mockPlatform.credentials.set(
      'auth-copilot',
      JSON.stringify({ type: 'oauth', accessToken: 'tok', refreshToken: 'tok', expiresAt: 0 })
    )
    await mockPlatform.credentials.set('ava:copilot:oauth_token', 'tok')
    await mockPlatform.credentials.set('ava:copilot:account_id', 'acct')

    await removeStoredAuth('copilot')

    expect(await mockPlatform.credentials.get('auth-copilot')).toBeNull()
    expect(await mockPlatform.credentials.get('ava:copilot:oauth_token')).toBeNull()
    expect(await mockPlatform.credentials.get('ava:copilot:account_id')).toBeNull()
  })
})

describe('migrateOAuthCredentials', () => {
  it('copies CLI token to core-v2 format when core-v2 key is missing', async () => {
    // Simulate pre-fix state: CLI key exists, core-v2 key doesn't
    await mockPlatform.credentials.set(
      'auth-copilot',
      JSON.stringify({
        type: 'oauth',
        accessToken: 'ghp_legacy',
        refreshToken: 'ghp_legacy',
        expiresAt: 0,
      })
    )

    await migrateOAuthCredentials()

    expect(await mockPlatform.credentials.get('ava:copilot:oauth_token')).toBe('ghp_legacy')
  })

  it('skips when core-v2 key already exists', async () => {
    await mockPlatform.credentials.set(
      'auth-copilot',
      JSON.stringify({
        type: 'oauth',
        accessToken: 'ghp_old',
        refreshToken: 'ghp_old',
        expiresAt: 0,
      })
    )
    await mockPlatform.credentials.set('ava:copilot:oauth_token', 'ghp_current')

    await migrateOAuthCredentials()

    // Should NOT overwrite existing core-v2 key
    expect(await mockPlatform.credentials.get('ava:copilot:oauth_token')).toBe('ghp_current')
  })

  it('migrates account_id when present', async () => {
    await mockPlatform.credentials.set(
      'auth-openai',
      JSON.stringify({
        type: 'oauth',
        accessToken: 'tok_openai',
        refreshToken: 'ref_openai',
        expiresAt: Date.now() + 3_600_000,
        accountId: 'acct_999',
      })
    )

    await migrateOAuthCredentials()

    expect(await mockPlatform.credentials.get('ava:openai:oauth_token')).toBe('tok_openai')
    expect(await mockPlatform.credentials.get('ava:openai:account_id')).toBe('acct_999')
  })

  it('skips providers with no stored auth', async () => {
    // No keys set for any provider
    await migrateOAuthCredentials()

    expect(await mockPlatform.credentials.get('ava:copilot:oauth_token')).toBeNull()
    expect(await mockPlatform.credentials.get('ava:openai:oauth_token')).toBeNull()
    expect(await mockPlatform.credentials.get('ava:google:oauth_token')).toBeNull()
  })
})

describe('expiresAt=0 edge case', () => {
  it('Copilot tokens with expiresAt=0 do not trigger refresh', async () => {
    // Set up a copilot auth with expiresAt=0
    await mockPlatform.credentials.set(
      'auth-copilot',
      JSON.stringify({
        type: 'oauth',
        accessToken: 'ghp_valid',
        refreshToken: 'ghp_valid',
        expiresAt: 0,
      })
    )

    // getStoredAuth should return the auth without needsRefresh triggering
    const auth = await getStoredAuth('copilot')
    expect(auth).toBeTruthy()
    expect(auth!.type).toBe('oauth')
    if (auth!.type === 'oauth') {
      expect(auth!.accessToken).toBe('ghp_valid')
      expect(auth!.expiresAt).toBe(0)
    }
  })
})
