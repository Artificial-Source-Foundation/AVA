import * as fs from 'node:fs/promises'
import * as os from 'node:os'
import * as path from 'node:path'
import { afterEach, describe, expect, it } from 'vitest'
import { runPluginCommand } from './plugin'

const tempRoots: string[] = []

async function createTempRoot(): Promise<string> {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'ava-plugin-test-'))
  tempRoots.push(root)
  return root
}

afterEach(async () => {
  await Promise.all(
    tempRoots.splice(0).map((root) => fs.rm(root, { recursive: true, force: true }))
  )
})

describe('plugin command', () => {
  it('creates scaffold files with init command', async () => {
    const root = await createTempRoot()

    await runPluginCommand(['init', 'My Plugin', '--dir', root])

    const pluginDir = path.join(root, 'my-plugin')
    await expect(fs.stat(path.join(pluginDir, 'package.json'))).resolves.toBeDefined()
    await expect(fs.stat(path.join(pluginDir, 'README.md'))).resolves.toBeDefined()
    await expect(fs.stat(path.join(pluginDir, 'src', 'index.ts'))).resolves.toBeDefined()
  })

  it('generates ava-extension.json manifest', async () => {
    const root = await createTempRoot()
    await runPluginCommand(['init', 'My Plugin', '--dir', root])
    const pluginDir = path.join(root, 'my-plugin')
    const manifest = JSON.parse(
      await fs.readFile(path.join(pluginDir, 'ava-extension.json'), 'utf-8')
    )
    expect(manifest.name).toBe('my-plugin')
    expect(manifest.version).toBe('0.1.0')
    expect(manifest.main).toBe('dist/index.js')
  })

  it('generates test file using ExtensionAPI pattern', async () => {
    const root = await createTempRoot()
    await runPluginCommand(['init', 'My Plugin', '--dir', root])
    const pluginDir = path.join(root, 'my-plugin')
    const testSource = await fs.readFile(path.join(pluginDir, 'src', 'index.test.ts'), 'utf-8')
    expect(testSource).toContain('createMockExtensionAPI')
    expect(testSource).toContain('activate')
  })

  it('generates source using ExtensionAPI pattern', async () => {
    const root = await createTempRoot()
    await runPluginCommand(['init', 'My Plugin', '--dir', root])
    const pluginDir = path.join(root, 'my-plugin')
    const source = await fs.readFile(path.join(pluginDir, 'src', 'index.ts'), 'utf-8')
    expect(source).toContain('ExtensionAPI')
    expect(source).toContain('Disposable')
    expect(source).toContain('export function activate')
    expect(source).not.toContain('PluginContext')
  })

  it('fails when target directory is non-empty without force', async () => {
    const root = await createTempRoot()
    const pluginDir = path.join(root, 'demo-plugin')
    await fs.mkdir(pluginDir, { recursive: true })
    await fs.writeFile(path.join(pluginDir, 'keep.txt'), 'content', 'utf-8')

    await expect(runPluginCommand(['init', 'demo plugin', '--dir', root])).rejects.toThrow(
      /Target directory is not empty/
    )
  })

  it('fails dev command when plugin scaffold is missing', async () => {
    const root = await createTempRoot()

    await expect(runPluginCommand(['dev', 'missing-plugin', '--dir', root])).rejects.toThrow(
      /Plugin not found/
    )
  })

  it('fails test command when plugin scaffold is missing', async () => {
    const root = await createTempRoot()

    await expect(runPluginCommand(['test', 'missing-plugin', '--dir', root])).rejects.toThrow(
      /Plugin not found/
    )
  })

  it('fails init when plugin name has no alphanumeric characters', async () => {
    const root = await createTempRoot()

    await expect(runPluginCommand(['init', '!!!', '--dir', root])).rejects.toThrow(
      /at least one alphanumeric/
    )
  })
})
