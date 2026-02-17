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
