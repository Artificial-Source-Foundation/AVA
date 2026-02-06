/**
 * Command Discovery Tests
 */

import { mkdir, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { discoverCommands } from './discovery.js'
import type { CommandDiscoveryConfig } from './types.js'

// ============================================================================
// Test Setup
// ============================================================================

describe('discoverCommands', () => {
  let testDir: string
  let projectDir: string
  let userDir: string

  beforeEach(async () => {
    testDir = join(
      tmpdir(),
      `estela-cmd-test-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
    )
    projectDir = join(testDir, 'project', '.estela', 'commands')
    userDir = join(testDir, 'user', '.estela', 'commands')

    await mkdir(projectDir, { recursive: true })
    await mkdir(userDir, { recursive: true })
  })

  afterEach(async () => {
    try {
      await rm(testDir, { recursive: true })
    } catch {
      // Ignore cleanup errors
    }
  })

  // =========================================================================
  // Basic Discovery
  // =========================================================================

  describe('basic discovery', () => {
    it('should discover TOML files in a directory', async () => {
      await writeFile(join(userDir, 'test.toml'), 'prompt = "test"')
      await writeFile(join(userDir, 'review.toml'), 'prompt = "review"')

      const config: CommandDiscoveryConfig = { userDir }
      const commands = await discoverCommands(config)

      expect(commands).toHaveLength(2)
      expect(commands.map((c) => c.name)).toContain('test')
      expect(commands.map((c) => c.name)).toContain('review')
    })

    it('should return empty for non-existent directories', async () => {
      const config: CommandDiscoveryConfig = {
        projectDir: '/nonexistent/project',
        userDir: '/nonexistent/user',
      }

      const commands = await discoverCommands(config)
      expect(commands).toHaveLength(0)
    })

    it('should skip non-TOML files', async () => {
      await writeFile(join(userDir, 'test.toml'), 'prompt = "test"')
      await writeFile(join(userDir, 'readme.md'), '# Readme')
      await writeFile(join(userDir, 'config.json'), '{}')

      const config: CommandDiscoveryConfig = { userDir }
      const commands = await discoverCommands(config)

      expect(commands).toHaveLength(1)
      expect(commands[0]!.name).toBe('test')
    })

    it('should skip hidden files', async () => {
      await writeFile(join(userDir, '.hidden.toml'), 'prompt = "hidden"')
      await writeFile(join(userDir, 'visible.toml'), 'prompt = "visible"')

      const config: CommandDiscoveryConfig = { userDir }
      const commands = await discoverCommands(config)

      expect(commands).toHaveLength(1)
      expect(commands[0]!.name).toBe('visible')
    })
  })

  // =========================================================================
  // Namespaced Commands
  // =========================================================================

  describe('namespaced commands', () => {
    it('should create namespaced names from subdirectories', async () => {
      const gitDir = join(userDir, 'git')
      await mkdir(gitDir, { recursive: true })
      await writeFile(join(gitDir, 'commit.toml'), 'prompt = "commit"')
      await writeFile(join(gitDir, 'push.toml'), 'prompt = "push"')

      const config: CommandDiscoveryConfig = { userDir }
      const commands = await discoverCommands(config)

      expect(commands).toHaveLength(2)
      expect(commands.map((c) => c.name)).toContain('git:commit')
      expect(commands.map((c) => c.name)).toContain('git:push')
    })

    it('should handle deeply nested namespaces', async () => {
      const deepDir = join(userDir, 'code', 'review')
      await mkdir(deepDir, { recursive: true })
      await writeFile(join(deepDir, 'frontend.toml'), 'prompt = "review"')

      const config: CommandDiscoveryConfig = { userDir }
      const commands = await discoverCommands(config)

      expect(commands).toHaveLength(1)
      expect(commands[0]!.name).toBe('code:review:frontend')
    })

    it('should lowercase command names', async () => {
      await writeFile(join(userDir, 'MyCommand.toml'), 'prompt = "test"')

      const config: CommandDiscoveryConfig = { userDir }
      const commands = await discoverCommands(config)

      expect(commands[0]!.name).toBe('mycommand')
    })
  })

  // =========================================================================
  // Priority (Project > User)
  // =========================================================================

  describe('priority', () => {
    it('should prefer project commands over user commands', async () => {
      await writeFile(join(userDir, 'test.toml'), 'prompt = "user version"')
      await writeFile(join(projectDir, 'test.toml'), 'prompt = "project version"')

      const config: CommandDiscoveryConfig = { projectDir, userDir }
      const commands = await discoverCommands(config)

      expect(commands).toHaveLength(1)
      expect(commands[0]!.name).toBe('test')
      expect(commands[0]!.isProjectLevel).toBe(true)
      expect(commands[0]!.filePath).toContain('project')
    })

    it('should merge non-overlapping commands', async () => {
      await writeFile(join(userDir, 'user-cmd.toml'), 'prompt = "user"')
      await writeFile(join(projectDir, 'project-cmd.toml'), 'prompt = "project"')

      const config: CommandDiscoveryConfig = { projectDir, userDir }
      const commands = await discoverCommands(config)

      expect(commands).toHaveLength(2)
      expect(commands.map((c) => c.name)).toContain('user-cmd')
      expect(commands.map((c) => c.name)).toContain('project-cmd')
    })

    it('should handle extra directories', async () => {
      const extraDir = join(testDir, 'extra', 'commands')
      await mkdir(extraDir, { recursive: true })
      await writeFile(join(extraDir, 'extra-cmd.toml'), 'prompt = "extra"')

      const config: CommandDiscoveryConfig = {
        userDir,
        extraDirs: [extraDir],
      }
      const commands = await discoverCommands(config)

      expect(commands.map((c) => c.name)).toContain('extra-cmd')
    })
  })

  // =========================================================================
  // Return Order
  // =========================================================================

  describe('return order', () => {
    it('should return commands sorted by name', async () => {
      await writeFile(join(userDir, 'charlie.toml'), 'prompt = "c"')
      await writeFile(join(userDir, 'alpha.toml'), 'prompt = "a"')
      await writeFile(join(userDir, 'bravo.toml'), 'prompt = "b"')

      const config: CommandDiscoveryConfig = { userDir }
      const commands = await discoverCommands(config)

      expect(commands.map((c) => c.name)).toEqual(['alpha', 'bravo', 'charlie'])
    })
  })
})
