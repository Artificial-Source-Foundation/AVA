import { describe, expect, it } from 'vitest'
import { createPolicyCache, enforcePolicy, type SecurityPolicy } from './conseca.js'

function makePolicy(overrides: Partial<SecurityPolicy> = {}): SecurityPolicy {
  return {
    allowedTools: ['read', 'write_file', 'bash'],
    allowedPaths: ['/workspace/**'],
    deniedCommands: ['rm -rf *', 'chmod 777*'],
    networkAccess: false,
    reasoning: 'test policy',
    ...overrides,
  }
}

describe('conseca policy enforcement', () => {
  it('restricts a tool not listed in allowedTools', () => {
    const policy = makePolicy({ allowedTools: ['read'] })

    const result = enforcePolicy(policy, 'write_file', {
      path: '/workspace/file.txt',
      content: 'x',
    })

    expect(result.allowed).toBe(false)
    expect(result.reason).toContain('not allowed')
  })

  it('allows a tool included in allowedTools', () => {
    const policy = makePolicy({ allowedTools: ['write_file'] })

    const result = enforcePolicy(policy, 'write_file', {
      path: '/workspace/file.txt',
      content: 'x',
    })

    expect(result.allowed).toBe(true)
  })

  it('enforces allowedPaths using glob matching', () => {
    const policy = makePolicy({
      allowedTools: ['write_file'],
      allowedPaths: ['/workspace/src/**'],
    })

    const denied = enforcePolicy(policy, 'write_file', {
      path: '/workspace/docs/readme.md',
      content: 'x',
    })
    const allowed = enforcePolicy(policy, 'write_file', {
      path: '/workspace/src/app.ts',
      content: 'x',
    })

    expect(denied.allowed).toBe(false)
    expect(denied.reason).toContain('Path')
    expect(allowed.allowed).toBe(true)
  })

  it('blocks bash commands that match denied command patterns', () => {
    const policy = makePolicy({
      allowedTools: ['bash'],
      deniedCommands: ['rm -rf *'],
    })

    const blocked = enforcePolicy(policy, 'bash', { command: 'rm -rf /tmp/cache' })
    const allowed = enforcePolicy(policy, 'bash', { command: 'ls -la' })

    expect(blocked.allowed).toBe(false)
    expect(blocked.reason).toContain('Command blocked')
    expect(allowed.allowed).toBe(true)
  })
})

describe('conseca policy cache', () => {
  it('caches policies per session', async () => {
    const cache = createPolicyCache()
    let calls = 0

    const first = await cache.getOrCreate('session-a', async () => {
      calls += 1
      return makePolicy({ reasoning: 'first' })
    })
    const second = await cache.getOrCreate('session-a', async () => {
      calls += 1
      return makePolicy({ reasoning: 'second' })
    })

    expect(calls).toBe(1)
    expect(second).toEqual(first)

    await cache.getOrCreate('session-b', async () => {
      calls += 1
      return makePolicy({ reasoning: 'third' })
    })

    expect(calls).toBe(2)
  })
})
