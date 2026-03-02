import * as nodePath from 'node:path'
import type { MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { getInstalledPlugins, installPlugin, uninstallPlugin } from './installer.js'

const HOME = '/home/testuser'
const PLUGINS_DIR = nodePath.join(HOME, '.ava/plugins')

describe('PluginInstaller', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    vi.stubEnv('HOME', HOME)
  })

  afterEach(() => {
    vi.unstubAllEnvs()
  })

  describe('getInstalledPlugins', () => {
    it('returns empty array when plugins dir does not exist', async () => {
      const plugins = await getInstalledPlugins()
      expect(plugins).toEqual([])
    })

    it('returns plugins with valid manifests', async () => {
      const manifestPath = nodePath.join(PLUGINS_DIR, 'my-plugin', 'ava-extension.json')
      platform.fs.addFile(
        manifestPath,
        JSON.stringify({
          name: 'my-plugin',
          version: '1.2.3',
          description: 'A test plugin',
          enabledByDefault: true,
        })
      )

      const plugins = await getInstalledPlugins()
      expect(plugins).toHaveLength(1)
      expect(plugins[0]!.name).toBe('my-plugin')
      expect(plugins[0]!.version).toBe('1.2.3')
      expect(plugins[0]!.description).toBe('A test plugin')
      expect(plugins[0]!.enabled).toBe(true)
    })

    it('skips entries without valid manifests', async () => {
      // Directory exists but no manifest file
      platform.fs.addDir(nodePath.join(PLUGINS_DIR, 'broken-plugin'))

      // Valid plugin alongside broken one
      platform.fs.addFile(
        nodePath.join(PLUGINS_DIR, 'good-plugin', 'ava-extension.json'),
        JSON.stringify({ name: 'good-plugin', version: '1.0.0' })
      )

      const plugins = await getInstalledPlugins()
      expect(plugins).toHaveLength(1)
      expect(plugins[0]!.name).toBe('good-plugin')
    })

    it('defaults version to 0.0.0 when missing', async () => {
      platform.fs.addFile(
        nodePath.join(PLUGINS_DIR, 'no-ver', 'ava-extension.json'),
        JSON.stringify({ name: 'no-ver' })
      )

      const plugins = await getInstalledPlugins()
      expect(plugins).toHaveLength(1)
      expect(plugins[0]!.version).toBe('0.0.0')
    })

    it('defaults enabled to true when enabledByDefault missing', async () => {
      platform.fs.addFile(
        nodePath.join(PLUGINS_DIR, 'p', 'ava-extension.json'),
        JSON.stringify({ name: 'p', version: '1.0.0' })
      )

      const plugins = await getInstalledPlugins()
      expect(plugins[0]!.enabled).toBe(true)
    })
  })

  describe('installPlugin', () => {
    it('returns error for URL sources (not yet implemented)', async () => {
      const result = await installPlugin('https://example.com/plugin.tar.gz')
      expect(result.success).toBe(false)
      expect(result.error).toContain('not yet implemented')
    })

    it('installs from local path', async () => {
      // Set up source directory with manifest
      const sourcePath = '/tmp/my-local-plugin'
      platform.fs.addFile(
        nodePath.join(sourcePath, 'ava-extension.json'),
        JSON.stringify({
          name: 'local-plugin',
          version: '2.0.0',
          description: 'Installed locally',
        })
      )

      // Mock shell for cp command
      platform.shell.setResult(
        `cp -r "${sourcePath}"/* "${nodePath.join(PLUGINS_DIR, 'my-local-plugin')}/"`,
        { stdout: '', stderr: '', exitCode: 0 }
      )

      // The installFromLocal function copies and then reads the manifest
      // from the install path, so we need to also add it there
      const installPath = nodePath.join(PLUGINS_DIR, 'my-local-plugin')
      platform.fs.addFile(
        nodePath.join(installPath, 'ava-extension.json'),
        JSON.stringify({
          name: 'local-plugin',
          version: '2.0.0',
          description: 'Installed locally',
        })
      )

      const result = await installPlugin(sourcePath, {
        name: 'my-local-plugin',
      })
      expect(result.success).toBe(true)
      expect(result.plugin).toBeDefined()
      expect(result.plugin!.name).toBe('local-plugin')
      expect(result.plugin!.version).toBe('2.0.0')
    })

    it('installs from GitHub repo', async () => {
      const installPath = nodePath.join(PLUGINS_DIR, 'cool-plugin')

      // Mock git clone
      platform.shell.setResult(
        `git clone https://github.com/user/cool-plugin.git "${installPath}"`,
        { stdout: '', stderr: '', exitCode: 0 }
      )

      // Add manifest at install path (post-clone)
      platform.fs.addFile(
        nodePath.join(installPath, 'ava-extension.json'),
        JSON.stringify({
          name: 'cool-plugin',
          version: '3.0.0',
          description: 'From GitHub',
        })
      )

      const result = await installPlugin('github:user/cool-plugin')
      expect(result.success).toBe(true)
      expect(result.plugin!.name).toBe('cool-plugin')
      expect(result.plugin!.version).toBe('3.0.0')
    })

    it('handles GitHub clone failure', async () => {
      const installPath = nodePath.join(PLUGINS_DIR, 'bad-repo')

      platform.shell.setResult(`git clone https://github.com/user/bad-repo.git "${installPath}"`, {
        stdout: '',
        stderr: 'fatal: repository not found',
        exitCode: 128,
      })

      const result = await installPlugin('github:user/bad-repo')
      expect(result.success).toBe(false)
      expect(result.error).toContain('git clone failed')
    })

    it('handles local install with missing manifest after copy', async () => {
      const sourcePath = '/tmp/fail-plugin'

      // Copy "succeeds" (default shell result is exitCode 0)
      // but no manifest exists at install path, so it fails
      const result = await installPlugin(sourcePath)
      expect(result.success).toBe(false)
      expect(result.error).toBeDefined()
      expect(result.error).toContain('ENOENT')
    })

    it('installs deps if package.json exists in GitHub plugin', async () => {
      const installPath = nodePath.join(PLUGINS_DIR, 'deps-plugin')

      platform.shell.setResult(
        `git clone https://github.com/user/deps-plugin.git "${installPath}"`,
        { stdout: '', stderr: '', exitCode: 0 }
      )

      // Add manifest and package.json at install path
      platform.fs.addFile(
        nodePath.join(installPath, 'ava-extension.json'),
        JSON.stringify({ name: 'deps-plugin', version: '1.0.0' })
      )
      platform.fs.addFile(
        nodePath.join(installPath, 'package.json'),
        JSON.stringify({ name: 'deps-plugin', dependencies: {} })
      )

      platform.shell.setResult('npm install --production', {
        stdout: '',
        stderr: '',
        exitCode: 0,
      })

      const result = await installPlugin('github:user/deps-plugin')
      expect(result.success).toBe(true)
    })
  })

  describe('uninstallPlugin', () => {
    it('uninstalls an installed plugin', async () => {
      const manifestPath = nodePath.join(PLUGINS_DIR, 'removeme', 'ava-extension.json')
      platform.fs.addFile(manifestPath, JSON.stringify({ name: 'removeme', version: '1.0.0' }))

      const result = await uninstallPlugin('removeme')
      expect(result.success).toBe(true)
    })

    it('returns error when plugin not found', async () => {
      const result = await uninstallPlugin('nonexistent')
      expect(result.success).toBe(false)
      expect(result.error).toContain('Plugin not found')
    })
  })
})
