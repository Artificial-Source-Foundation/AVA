import { afterEach, describe, expect, it } from 'vitest'
import {
  getRegisteredHooks,
  hasHooks,
  mergeHookResults,
  registerHook,
  resetHooks,
  runHooks,
} from './runner.js'
import type { PreToolUseContext } from './types.js'

describe('Hook Runner', () => {
  afterEach(() => resetHooks())

  describe('registerHook', () => {
    it('registers a hook', () => {
      registerHook({
        type: 'PreToolUse',
        name: 'test-hook',
        handler: async () => ({}),
      })
      expect(hasHooks('PreToolUse')).toBe(true)
      expect(hasHooks('PostToolUse')).toBe(false)
    })

    it('returns unsubscribe function', () => {
      const unsub = registerHook({
        type: 'PreToolUse',
        name: 'test-hook',
        handler: async () => ({}),
      })
      expect(hasHooks('PreToolUse')).toBe(true)
      unsub()
      expect(hasHooks('PreToolUse')).toBe(false)
    })
  })

  describe('runHooks', () => {
    it('returns empty result when no hooks registered', async () => {
      const result = await runHooks('PreToolUse', {} as PreToolUseContext)
      expect(result).toEqual({})
    })

    it('runs registered hooks', async () => {
      registerHook({
        type: 'PreToolUse',
        name: 'logger',
        handler: async () => ({ contextModification: 'logged' }),
      })

      const result = await runHooks('PreToolUse', {
        toolName: 'read_file',
        parameters: {},
        workingDirectory: '/tmp',
        sessionId: 's1',
      })

      expect(result.contextModification).toBe('logged')
    })

    it('stops on cancel for PreToolUse', async () => {
      const calls: string[] = []

      registerHook({
        type: 'PreToolUse',
        name: 'blocker',
        handler: async () => {
          calls.push('blocker')
          return { cancel: true, errorMessage: 'Blocked' }
        },
      })
      registerHook({
        type: 'PreToolUse',
        name: 'logger',
        handler: async () => {
          calls.push('logger')
          return {}
        },
      })

      const result = await runHooks('PreToolUse', {} as PreToolUseContext)
      expect(result.cancel).toBe(true)
      expect(result.errorMessage).toBe('Blocked')
      expect(calls).toEqual(['blocker']) // logger not called
    })

    it('runs all PostToolUse hooks', async () => {
      const calls: string[] = []

      registerHook({
        type: 'PostToolUse',
        name: 'a',
        handler: async () => {
          calls.push('a')
          return {}
        },
      })
      registerHook({
        type: 'PostToolUse',
        name: 'b',
        handler: async () => {
          calls.push('b')
          return {}
        },
      })

      await runHooks('PostToolUse', {} as PreToolUseContext)
      expect(calls).toEqual(['a', 'b'])
    })

    it('handles errors with continueOnError', async () => {
      registerHook({
        type: 'PreToolUse',
        name: 'failing',
        handler: async () => {
          throw new Error('oops')
        },
      })
      registerHook({
        type: 'PreToolUse',
        name: 'ok',
        handler: async () => ({ contextModification: 'ok' }),
      })

      const result = await runHooks('PreToolUse', {} as PreToolUseContext)
      expect(result.contextModification).toBe('ok')
    })
  })

  describe('mergeHookResults', () => {
    it('merges empty results', () => {
      expect(mergeHookResults([])).toEqual({})
    })

    it('ORs cancel flags', () => {
      const result = mergeHookResults([{ cancel: false }, { cancel: true }])
      expect(result.cancel).toBe(true)
    })

    it('concatenates context modifications', () => {
      const result = mergeHookResults([
        { contextModification: 'first' },
        { contextModification: 'second' },
      ])
      expect(result.contextModification).toBe('first\nsecond')
    })

    it('takes last error message', () => {
      const result = mergeHookResults([{ errorMessage: 'first' }, { errorMessage: 'second' }])
      expect(result.errorMessage).toBe('second')
    })
  })

  describe('getRegisteredHooks', () => {
    it('returns registered hooks map', () => {
      registerHook({ type: 'PreToolUse', name: 'a', handler: async () => ({}) })
      registerHook({ type: 'PostToolUse', name: 'b', handler: async () => ({}) })

      const map = getRegisteredHooks()
      expect(map.get('PreToolUse')).toHaveLength(1)
      expect(map.get('PostToolUse')).toHaveLength(1)
    })
  })
})
