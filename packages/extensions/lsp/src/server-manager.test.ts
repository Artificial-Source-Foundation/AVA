import { describe, expect, it, vi } from 'vitest'
import { pathToUri, uriToPath } from './server-manager.js'

// Test the URI conversion helpers directly since server lifecycle
// requires real process spawning

describe('URI conversion', () => {
  it('converts file:// URI to path', () => {
    expect(uriToPath('file:///home/user/test.ts')).toBe('/home/user/test.ts')
  })

  it('handles encoded characters', () => {
    expect(uriToPath('file:///home/user/my%20project/test.ts')).toBe(
      '/home/user/my project/test.ts'
    )
  })

  it('returns non-file URIs unchanged', () => {
    expect(uriToPath('/absolute/path.ts')).toBe('/absolute/path.ts')
  })

  it('converts path to file:// URI', () => {
    expect(pathToUri('/home/user/test.ts')).toBe('file:///home/user/test.ts')
  })
})

describe('LSPServerManager', () => {
  it('can be constructed', async () => {
    const { LSPServerManager } = await import('./server-manager.js')
    const shell = { exec: vi.fn(), spawn: vi.fn() }
    const manager = new LSPServerManager(shell)
    expect(manager.getActiveLanguages()).toEqual([])
  })

  it('returns null client for unknown language', async () => {
    const { LSPServerManager } = await import('./server-manager.js')
    const shell = { exec: vi.fn(), spawn: vi.fn() }
    const manager = new LSPServerManager(shell)
    expect(manager.getClient('typescript')).toBeNull()
  })

  it('stopAll is safe when no servers running', async () => {
    const { LSPServerManager } = await import('./server-manager.js')
    const shell = { exec: vi.fn(), spawn: vi.fn() }
    const manager = new LSPServerManager(shell)
    await manager.stopAll() // should not throw
  })
})
