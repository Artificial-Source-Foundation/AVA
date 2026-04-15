import * as tauriCore from '@tauri-apps/api/core'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  rustAgent,
  rustBrowser,
  rustCompute,
  rustExtensions,
  rustGit,
  rustMemory,
  rustOAuth,
  rustPermissions,
  rustPlugins,
  rustPty,
  rustReflection,
  rustSystem,
  rustTools,
  rustValidation,
} from '../../src/services/rust-bridge'
import { MockIpc } from './helpers/mock-ipc'

describe('rust-bridge wrappers', () => {
  const ipc = new MockIpc()

  beforeEach(() => {
    ipc.reset()
    ipc.install()
    vi.spyOn(tauriCore, 'isTauri').mockReturnValue(true)
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('serializes arguments and commands for all wrapper groups', async () => {
    ipc.setResponse('memory_remember', { id: 1, key: 'k', value: 'v', createdAt: 'now' })
    ipc.setResponse('memory_recall', null)
    ipc.setResponse('memory_search', [])
    ipc.setResponse('memory_recent', [])
    ipc.setResponse('evaluate_permission', { action: 'allow' })
    ipc.setResponse('validation_validate_edit', { valid: true, details: [], error: null })
    ipc.setResponse('validation_validate_with_retry', {
      result: { valid: true, details: [], error: null },
      finalContent: 'ok',
      attempts: 1,
    })
    ipc.setResponse('execute_git_tool', {
      program: 'git',
      args: [],
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    ipc.setResponse('execute_browser_tool', { output: 'ok' })
    ipc.setResponse('list_tools', [{ name: 'read_file', description: 'Read file' }])
    ipc.setResponse('execute_tool', { content: 'ok', is_error: false })
    ipc.setResponse('submit_goal', { id: 's1', completed: true, messages: [] })
    ipc.setResponse('compute_grep', { matches: [], truncated: false })
    ipc.setResponse('compute_fuzzy_replace', { content: 'new', strategy: 'simple' })
    ipc.setResponse('reflection_reflect_and_fix', { output: 'ok', attemptedFix: false })
    ipc.setResponse('pty_spawn', undefined)
    ipc.setResponse('pty_write', undefined)
    ipc.setResponse('pty_resize', undefined)
    ipc.setResponse('pty_kill', undefined)
    ipc.setResponse('oauth_listen', { code: 'c', state: 's' })
    ipc.setResponse('oauth_copilot_device_start', {
      device_code: 'd',
      user_code: 'u',
      verification_uri: 'https://example.com',
      expires_in: 600,
    })
    ipc.setResponse('oauth_copilot_device_poll', { access_token: 't' })
    ipc.setResponse('get_env_var', '1')
    ipc.setResponse('get_cwd', '/tmp')
    ipc.setResponse('append_log', undefined)
    ipc.setResponse('cleanup_old_logs', 2)
    ipc.setResponse('allow_project_path', undefined)
    ipc.setResponse('get_plugins_state', {})
    ipc.setResponse('set_plugins_state', undefined)
    ipc.setResponse('install_plugin', { installed: true, enabled: true })
    ipc.setResponse('uninstall_plugin', { installed: false, enabled: false })
    ipc.setResponse('set_plugin_enabled', { installed: true, enabled: false })
    ipc.setResponse('extensions_register_native', {
      kind: 'native',
      name: 'n',
      version: '1',
      path: '.',
      tools: [],
      hooks: [],
      validators: [],
    })
    ipc.setResponse('extensions_register_wasm', {
      kind: 'wasm',
      name: 'w',
      version: '1',
      path: '.',
      tools: [],
      hooks: [],
      validators: [],
      metadata: {},
    })

    await rustMemory.remember('k', 'v')
    await rustMemory.recall('k')
    await rustMemory.search('k')
    await rustMemory.recent(5)
    await rustPermissions.evaluate('/workspace', [], 'read_file', ['x'])
    await rustValidation.validateEdit('content')
    await rustValidation.validateWithRetry('content', ['fix'])
    await rustGit.execute('{"type":"status"}')
    await rustBrowser.execute('{"type":"goto"}')
    await rustTools.list()
    await rustTools.execute('read_file', { path: 'x' })
    await rustAgent.run('goal')
    await rustCompute.grep('.', 'needle', { include: '*.ts', maxResults: 3 })
    await rustCompute.fuzzyReplace('a', 'a', 'b', true)
    await rustReflection.reflectAndFix({ output: 'x' })
    await rustPty.spawn({ id: '1', cols: 80, rows: 24 })
    await rustPty.write('1', 'ls')
    await rustPty.resize('1', 100, 30)
    await rustPty.kill('1')
    await rustOAuth.listen(3000)
    await rustOAuth.copilotDeviceStart('id', 'scope')
    await rustOAuth.copilotDevicePoll('id', 'device')
    await rustSystem.getEnvVar('AVA_TEST')
    await rustSystem.getCwd()
    await rustSystem.appendLog('/tmp/a.log', 'entry')
    await rustSystem.cleanupOldLogs('/tmp', 7)
    await rustSystem.allowProjectPath('/tmp')
    await rustPlugins.getState()
    await rustPlugins.setState({})
    await rustPlugins.install('plugin.id')
    await rustPlugins.uninstall('plugin.id')
    await rustPlugins.setEnabled('plugin.id', false)
    await rustExtensions.registerNative({
      name: 'n',
      version: '1',
      path: '.',
      tools: [],
      hooks: [],
      validators: [],
    })
    await rustExtensions.registerWasm({
      name: 'w',
      version: '1',
      path: '.',
      tools: [],
      hooks: [],
      validators: [],
      metadata: {},
    })

    expect(ipc.getCalls('memory_remember')[0]?.args).toMatchObject({ key: 'k', value: 'v' })
    expect(ipc.getCalls('execute_tool')[0]?.args).toEqual({
      tool: 'read_file',
      args: { path: 'x' },
    })
    expect(ipc.getCalls('oauth_copilot_device_start')[0]?.args).toEqual({
      clientId: 'id',
      scope: 'scope',
    })
  })

  it('wraps invoke errors with command context', async () => {
    ipc.setHandler('list_tools', async () => {
      throw new Error('boom')
    })

    await expect(rustTools.list()).rejects.toThrow('[rust-bridge:list_tools] boom')
  })
})
