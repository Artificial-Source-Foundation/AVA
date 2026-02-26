import { describe, expect, it } from 'vitest'
import { generateCodeChallenge, generateCodeVerifier, generatePKCE, generateState } from './pkce.js'

// ============================================================================
// generateCodeVerifier
// ============================================================================

describe('generateCodeVerifier', () => {
  it('should generate a string of length 43', () => {
    const verifier = generateCodeVerifier()
    expect(verifier).toHaveLength(43)
  })

  it('should use only RFC 7636 allowed characters', () => {
    const allowed = /^[A-Za-z0-9\-._~]+$/
    for (let i = 0; i < 10; i++) {
      const verifier = generateCodeVerifier()
      expect(verifier).toMatch(allowed)
    }
  })

  it('should generate different verifiers each time', () => {
    const verifiers = new Set<string>()
    for (let i = 0; i < 20; i++) {
      verifiers.add(generateCodeVerifier())
    }
    // With 66 chars and length 43, collision is astronomically unlikely
    expect(verifiers.size).toBe(20)
  })
})

// ============================================================================
// generateCodeChallenge
// ============================================================================

describe('generateCodeChallenge', () => {
  it('should produce a base64url-encoded string', async () => {
    const challenge = await generateCodeChallenge('test-verifier')
    // base64url uses only these characters (no padding)
    expect(challenge).toMatch(/^[A-Za-z0-9\-_]+$/)
  })

  it('should be deterministic for the same input', async () => {
    const challenge1 = await generateCodeChallenge('same-verifier')
    const challenge2 = await generateCodeChallenge('same-verifier')
    expect(challenge1).toBe(challenge2)
  })

  it('should produce different challenges for different verifiers', async () => {
    const challenge1 = await generateCodeChallenge('verifier-one')
    const challenge2 = await generateCodeChallenge('verifier-two')
    expect(challenge1).not.toBe(challenge2)
  })

  it('should produce a 43-character challenge (SHA-256 → base64url)', async () => {
    // SHA-256 = 32 bytes → base64url = ceil(32*4/3) = 43 chars (no padding)
    const challenge = await generateCodeChallenge('any-verifier')
    expect(challenge).toHaveLength(43)
  })
})

// ============================================================================
// generatePKCE
// ============================================================================

describe('generatePKCE', () => {
  it('should return both verifier and challenge', async () => {
    const pkce = await generatePKCE()
    expect(pkce.verifier).toBeTruthy()
    expect(pkce.challenge).toBeTruthy()
  })

  it('should have a challenge that matches the verifier', async () => {
    const pkce = await generatePKCE()
    const expectedChallenge = await generateCodeChallenge(pkce.verifier)
    expect(pkce.challenge).toBe(expectedChallenge)
  })
})

// ============================================================================
// generateState
// ============================================================================

describe('generateState', () => {
  it('should generate a non-empty string', () => {
    const state = generateState()
    expect(state.length).toBeGreaterThan(0)
  })

  it('should use only base64url characters', () => {
    const state = generateState()
    expect(state).toMatch(/^[A-Za-z0-9\-_]+$/)
  })

  it('should generate different states each time', () => {
    const states = new Set<string>()
    for (let i = 0; i < 20; i++) {
      states.add(generateState())
    }
    expect(states.size).toBe(20)
  })

  it('should produce a 43-character state (32 bytes → base64url)', () => {
    // 32 bytes → base64url = ceil(32*4/3) = 43 chars (no padding)
    const state = generateState()
    expect(state).toHaveLength(43)
  })
})
