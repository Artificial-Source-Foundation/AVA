import { beforeEach, describe, expect, it, vi } from 'vitest'
import type { ProviderManifest } from './dynamic-loader.js'
import { fetchRegistry, loadProvider, resolveProviderFactory } from './index.js'

const { execFileMock } = vi.hoisted(() => ({
  execFileMock: vi.fn(
    (command: string, args: string[], options: object, callback: (err: Error | null) => void) => {
      callback(null)
    }
  ),
}))

vi.mock('node:child_process', () => ({
  execFile: execFileMock,
  default: {
    execFile: execFileMock,
  },
}))

vi.mock('node:fs/promises', () => ({
  access: vi.fn(async () => {
    throw new Error('not found')
  }),
  mkdir: vi.fn(async () => undefined),
  readFile: vi.fn(async () => {
    throw new Error('no cache')
  }),
  writeFile: vi.fn(async () => undefined),
  default: {
    access: vi.fn(async () => {
      throw new Error('not found')
    }),
    mkdir: vi.fn(async () => undefined),
    readFile: vi.fn(async () => {
      throw new Error('no cache')
    }),
    writeFile: vi.fn(async () => undefined),
  },
}))

describe('dynamic provider loader', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    delete process.env.AVA_PROVIDERS_AUTO_INSTALL
  })

  it('loads bundled provider without runtime install', async () => {
    const factory = await loadProvider('openai')
    const client = factory()

    expect(client).toBeDefined()
    expect(execFileMock).not.toHaveBeenCalled()
  })

  it('unknown provider triggers install flow when auto-install enabled', async () => {
    process.env.AVA_PROVIDERS_AUTO_INSTALL = 'true'
    const registry: ProviderManifest[] = [
      {
        name: 'runtime-provider',
        package: '@ava/non-existent-provider',
        factory: 'createClient',
        models: ['runtime/model'],
      },
    ]

    await expect(loadProvider('runtime-provider', registry)).rejects.toThrow()
    expect(execFileMock).toHaveBeenCalled()
  })

  it('fetches registry manifests from remote URL', async () => {
    const mockFetch = vi.fn(async () => ({
      ok: true,
      json: async () => [
        {
          name: 'registry-provider',
          package: '@ava/registry-provider',
          factory: 'createClient',
          models: ['registry/model'],
        },
      ],
    }))

    vi.stubGlobal('fetch', mockFetch)
    const registry = await fetchRegistry('https://example.com/providers.json')

    expect(registry).toHaveLength(1)
    expect(registry[0]?.name).toBe('registry-provider')
  })

  it('throws when auto-install is disabled for uninstalled provider', async () => {
    const registry: ProviderManifest[] = [
      {
        name: 'blocked-provider',
        package: '@ava/blocked-provider',
        factory: 'createClient',
        models: ['blocked/model'],
      },
    ]

    await expect(loadProvider('blocked-provider', registry)).rejects.toThrow('auto-install')
    expect(execFileMock).not.toHaveBeenCalled()
  })

  it('falls back to dynamic loader when bundled provider is missing', async () => {
    const bundledProviders = {
      openai: await loadProvider('openai'),
    }

    const factory = await resolveProviderFactory('openai', bundledProviders)

    expect(factory).toBe(bundledProviders.openai)
  })
})
