import { beforeEach, describe, expect, it, vi } from 'vitest'
import { downloadPlugin, reloadPlugin } from './plugin-download'

// ============================================================================
// Mock logger to suppress output
// ============================================================================

vi.mock('./logger', () => ({
  logInfo: vi.fn(),
  logWarn: vi.fn(),
  logError: vi.fn(),
}))

// ============================================================================
// downloadPlugin
// ============================================================================

describe('downloadPlugin', () => {
  let mockFs: {
    mkdir: ReturnType<typeof vi.fn>
    writeTextFile: ReturnType<typeof vi.fn>
  }
  let getTauriFs: () => Promise<typeof import('@tauri-apps/plugin-fs')>

  beforeEach(() => {
    mockFs = {
      mkdir: vi.fn(async () => {}),
      writeTextFile: vi.fn(async () => {}),
    }
    getTauriFs = async () => mockFs as unknown as typeof import('@tauri-apps/plugin-fs')
    vi.restoreAllMocks()
  })

  it('downloads a JSON plugin bundle', async () => {
    const jsonBody = JSON.stringify({ name: 'test', version: '1.0.0', main: 'index.js' })
    globalThis.fetch = vi.fn(async () => ({
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      text: async () => jsonBody,
    })) as unknown as typeof fetch

    await downloadPlugin('https://example.com/plugin.json', '/plugins/test', 'test', getTauriFs)

    expect(mockFs.mkdir).toHaveBeenCalledWith('/plugins/test', { recursive: true })
    expect(mockFs.writeTextFile).toHaveBeenCalledWith('/plugins/test/plugin.json', jsonBody)
  })

  it('detects JSON by .json URL extension', async () => {
    const jsonBody = '{"name":"test"}'
    globalThis.fetch = vi.fn(async () => ({
      ok: true,
      headers: new Headers({ 'content-type': 'text/plain' }),
      text: async () => jsonBody,
    })) as unknown as typeof fetch

    await downloadPlugin('https://example.com/bundle.json', '/plugins/test', 'test', getTauriFs)

    expect(mockFs.writeTextFile).toHaveBeenCalledWith('/plugins/test/plugin.json', jsonBody)
  })

  it('downloads a JS plugin and creates a manifest', async () => {
    const jsCode = 'export function activate(api) { return { dispose() {} } }'
    globalThis.fetch = vi.fn(async () => ({
      ok: true,
      headers: new Headers({ 'content-type': 'application/javascript' }),
      text: async () => jsCode,
    })) as unknown as typeof fetch

    await downloadPlugin(
      'https://example.com/plugin.js',
      '/plugins/my-plugin',
      'my-plugin',
      getTauriFs
    )

    expect(mockFs.writeTextFile).toHaveBeenCalledWith('/plugins/my-plugin/index.js', jsCode)
    // Should also write a manifest
    const manifestCall = (mockFs.writeTextFile.mock.calls as string[][]).find(
      (c) => c[0] === '/plugins/my-plugin/manifest.json'
    )
    expect(manifestCall).toBeDefined()
    const manifest = JSON.parse(manifestCall![1])
    expect(manifest.name).toBe('my-plugin')
    expect(manifest.version).toBe('0.0.0')
    expect(manifest.main).toBe('index.js')
  })

  it('treats unknown content type as JS', async () => {
    globalThis.fetch = vi.fn(async () => ({
      ok: true,
      headers: new Headers({ 'content-type': 'application/octet-stream' }),
      text: async () => 'some code',
    })) as unknown as typeof fetch

    await downloadPlugin('https://example.com/plugin', '/dir', 'p', getTauriFs)

    expect(mockFs.writeTextFile).toHaveBeenCalledWith('/dir/index.js', 'some code')
  })

  it('throws on non-ok response', async () => {
    globalThis.fetch = vi.fn(async () => ({
      ok: false,
      status: 404,
      statusText: 'Not Found',
      headers: new Headers(),
    })) as unknown as typeof fetch

    await expect(
      downloadPlugin('https://example.com/missing', '/dir', 'p', getTauriFs)
    ).rejects.toThrow('Failed to download plugin: 404 Not Found')
  })

  it('silently ignores mkdir errors (dir already exists)', async () => {
    mockFs.mkdir.mockRejectedValueOnce(new Error('EEXIST'))
    globalThis.fetch = vi.fn(async () => ({
      ok: true,
      headers: new Headers({ 'content-type': 'application/json' }),
      text: async () => '{}',
    })) as unknown as typeof fetch

    // Should not throw
    await downloadPlugin('https://example.com/p.json', '/plugins/x', 'x', getTauriFs)
    expect(mockFs.writeTextFile).toHaveBeenCalled()
  })
})

// ============================================================================
// reloadPlugin
// ============================================================================

describe('reloadPlugin', () => {
  it('disposes current plugin before reloading', async () => {
    const disposeFn = vi.fn()
    const readSource = vi.fn(async () => null)
    const createApi = vi.fn()

    await reloadPlugin('my-plugin', { dispose: disposeFn }, createApi, readSource)

    expect(disposeFn).toHaveBeenCalledTimes(1)
  })

  it('returns null when no source is found', async () => {
    const readSource = vi.fn(async () => null)
    const createApi = vi.fn()

    const result = await reloadPlugin('missing', undefined, createApi, readSource)

    expect(result).toBeNull()
    expect(readSource).toHaveBeenCalledWith('missing')
  })

  it('catches errors during dispose without throwing', async () => {
    const disposeFn = vi.fn(() => {
      throw new Error('dispose failed')
    })
    const readSource = vi.fn(async () => null)
    const createApi = vi.fn()

    // Should not throw
    const result = await reloadPlugin('my-plugin', { dispose: disposeFn }, createApi, readSource)
    expect(result).toBeNull()
  })

  it('propagates import errors when blob URL import fails (e.g. test env)', async () => {
    // In Node/jsdom, dynamic import of blob URLs fails with ERR_MODULE_NOT_FOUND.
    // The try/finally in reloadPlugin revokes the URL but does NOT catch import errors,
    // so the error propagates to the caller. This is correct behavior — the caller
    // (plugin store) is responsible for error handling.
    const moduleCode = 'export const version = "1.0.0"'
    const readSource = vi.fn(async () => moduleCode)
    const createApi = vi.fn()

    await expect(reloadPlugin('no-activate', undefined, createApi, readSource)).rejects.toThrow()

    expect(readSource).toHaveBeenCalledWith('no-activate')
  })
})
