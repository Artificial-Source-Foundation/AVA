/**
 * Extension activation smoke test — verifies all extensions activate without errors.
 *
 * Imports each extension's activate() function with a mock ExtensionAPI
 * and verifies it returns a disposable without throwing.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate as activateAgentModes } from './agent-modes/src/index.js'
import { activate as activateCommander } from './commander/src/index.js'
import { activate as activateContext } from './context/src/index.js'
import { activate as activateDiff } from './diff/src/index.js'
import { activate as activateGit } from './git/src/index.js'
import { activate as activateHooks } from './hooks/src/index.js'
import { activate as activateInstructions } from './instructions/src/index.js'
import { activate as activateLsp } from './lsp/src/index.js'
import { activate as activateMcp } from './mcp/src/index.js'
import { activate as activateMemory } from './memory/src/index.js'
import { activate as activateModels } from './models/src/index.js'
import { activate as activatePermissions } from './permissions/src/index.js'
import { activate as activatePlugins } from './plugins/src/index.js'
import { activate as activatePrompts } from './prompts/src/index.js'
import { activate as activateRecall } from './recall/src/index.js'
import { activate as activateServer } from './server/src/index.js'
import { activate as activateSlashCommands } from './slash-commands/src/index.js'
// Extension imports
import { activate as activateToolsExtended } from './tools-extended/src/index.js'
import { activate as activateValidator } from './validator/src/index.js'

const EXTENSIONS: Array<{
  name: string
  activate: (api: ReturnType<typeof createMockExtensionAPI>['api']) => { dispose(): void }
}> = [
  { name: 'tools-extended', activate: activateToolsExtended },
  { name: 'permissions', activate: activatePermissions },
  { name: 'agent-modes', activate: activateAgentModes },
  { name: 'hooks', activate: activateHooks },
  { name: 'context', activate: activateContext },
  { name: 'prompts', activate: activatePrompts },
  { name: 'diff', activate: activateDiff },
  { name: 'git', activate: activateGit },
  { name: 'lsp', activate: activateLsp },
  { name: 'memory', activate: activateMemory },
  { name: 'models', activate: activateModels },
  { name: 'slash-commands', activate: activateSlashCommands },
  { name: 'instructions', activate: activateInstructions },
  { name: 'mcp', activate: activateMcp },
  { name: 'validator', activate: activateValidator },
  { name: 'commander', activate: activateCommander },
  { name: 'plugins', activate: activatePlugins },
  { name: 'server', activate: activateServer },
  { name: 'recall', activate: activateRecall },
]

describe('Extension activation smoke test', () => {
  it.each(
    EXTENSIONS.map((e) => [e.name, e.activate])
  )('%s activates without error', (_name, activate) => {
    const { api, dispose } = createMockExtensionAPI()
    let disposable: { dispose(): void } | undefined
    expect(() => {
      disposable = activate(api)
    }).not.toThrow()
    expect(disposable).toBeDefined()
    expect(typeof disposable!.dispose).toBe('function')
    disposable!.dispose()
    dispose()
  })

  it('all extensions dispose cleanly', () => {
    const disposables: Array<{ dispose(): void }> = []
    const mocks: Array<{ dispose(): void }> = []

    for (const ext of EXTENSIONS) {
      const mock = createMockExtensionAPI()
      mocks.push(mock)
      disposables.push(ext.activate(mock.api))
    }

    // Dispose in reverse order (like real extension manager)
    for (const d of disposables.reverse()) {
      expect(() => d.dispose()).not.toThrow()
    }
    for (const m of mocks) {
      m.dispose()
    }
  })

  it(`activates ${EXTENSIONS.length} extensions total`, () => {
    expect(EXTENSIONS.length).toBe(19)
  })
})
