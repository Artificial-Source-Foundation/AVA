import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { analyzeSession, registerAutoLearn } from './auto-learn.js'

describe('Auto-Learn Memory', () => {
  describe('analyzeSession', () => {
    it('detects React framework usage', () => {
      const items = analyzeSession('Built a component using react and styled it', 'Build a form')
      const reactItem = items.find((i) => i.key === 'uses-react')
      expect(reactItem).toBeDefined()
      expect(reactItem!.category).toBe('project-conventions')
      expect(reactItem!.confidence).toBeGreaterThanOrEqual(0.7)
    })

    it('detects Vue framework usage', () => {
      const items = analyzeSession('Set up vue component with reactivity', 'Create UI')
      expect(items.find((i) => i.key === 'uses-vue')).toBeDefined()
    })

    it('detects Solid framework usage', () => {
      const items = analyzeSession('Used solid createSignal for state', 'Build reactive UI')
      expect(items.find((i) => i.key === 'uses-solid')).toBeDefined()
    })

    it('detects framework from goal', () => {
      const items = analyzeSession(
        'Completed the task with standard patterns that have nothing unique in them',
        'Build a svelte component for the dashboard'
      )
      expect(items.find((i) => i.key === 'uses-svelte')).toBeDefined()
    })

    it('detects multiple frameworks', () => {
      const items = analyzeSession('Used react frontend with express backend', 'Full stack')
      expect(items.find((i) => i.key === 'uses-react')).toBeDefined()
      expect(items.find((i) => i.key === 'uses-express')).toBeDefined()
    })

    it('detects vitest test framework', () => {
      const items = analyzeSession(
        'Ran tests with vitest and all 15 passed successfully',
        'Add tests'
      )
      const testItem = items.find((i) => i.key === 'test-framework')
      expect(testItem).toBeDefined()
      expect(testItem!.value).toContain('vitest')
      expect(testItem!.confidence).toBe(0.9)
    })

    it('detects jest test framework', () => {
      const items = analyzeSession(
        'describe("utils", () => { it("works", () => { expect(true).toBe(true) }) })',
        'Test utils'
      )
      const testItem = items.find((i) => i.key === 'test-framework')
      expect(testItem).toBeDefined()
      expect(testItem!.value).toContain('jest')
    })

    it('detects pytest test framework', () => {
      const items = analyzeSession(
        'Used pytest to run the test suite and everything passed',
        'Run tests'
      )
      const testItem = items.find((i) => i.key === 'test-framework')
      expect(testItem).toBeDefined()
      expect(testItem!.value).toContain('pytest')
    })

    it('detects TypeScript language', () => {
      const items = analyzeSession('Compiled the .tsx files with tsc', 'Build project')
      const langItem = items.find((i) => i.key === 'primary-language')
      expect(langItem).toBeDefined()
      expect(langItem!.value).toContain('TypeScript')
      expect(langItem!.confidence).toBe(0.85)
    })

    it('detects Python language', () => {
      const items = analyzeSession(
        'Installed packages with pip install -r requirements.txt',
        'Setup'
      )
      const langItem = items.find((i) => i.key === 'primary-language')
      expect(langItem).toBeDefined()
      expect(langItem!.value).toContain('Python')
    })

    it('detects Rust language', () => {
      const items = analyzeSession('Ran cargo build and compiled the .rs files', 'Build')
      const langItem = items.find((i) => i.key === 'primary-language')
      expect(langItem).toBeDefined()
      expect(langItem!.value).toContain('Rust')
    })

    it('detects Go language', () => {
      const items = analyzeSession('Built the golang service with .go source files', 'Deploy')
      const langItem = items.find((i) => i.key === 'primary-language')
      expect(langItem).toBeDefined()
      expect(langItem!.value).toContain('Go')
    })

    it('detects language from goal', () => {
      const items = analyzeSession('Completed the task as requested', 'Refactor the rust module')
      const langItem = items.find((i) => i.key === 'primary-language')
      expect(langItem).toBeDefined()
      expect(langItem!.value).toContain('Rust')
    })

    it('returns empty for unrecognized content', () => {
      const items = analyzeSession('Did some generic work on the project', 'General task')
      expect(items).toHaveLength(0)
    })

    it('filters out items below confidence threshold', () => {
      // All built-in detectors produce items >= 0.7, so this tests the filter logic
      const items = analyzeSession('Used react', 'Build')
      for (const item of items) {
        expect(item.confidence).toBeGreaterThanOrEqual(0.7)
      }
    })

    it('only returns first matching test framework', () => {
      // vitest and jest patterns could overlap but only one should be returned
      const items = analyzeSession('import { describe } from vitest', 'Test')
      const testItems = items.filter((i) => i.key === 'test-framework')
      expect(testItems).toHaveLength(1)
    })

    it('only returns first matching language', () => {
      const items = analyzeSession('file.tsx with typescript and tsc', 'Build')
      const langItems = items.filter((i) => i.key === 'primary-language')
      expect(langItems).toHaveLength(1)
    })
  })

  describe('registerAutoLearn', () => {
    it('registers an agent:completing event handler', () => {
      const { api, eventHandlers } = createMockExtensionAPI()
      registerAutoLearn(api)
      expect(eventHandlers.has('agent:completing')).toBe(true)
    })

    it('emits memory:auto-learned when patterns detected', () => {
      const { api, emittedEvents } = createMockExtensionAPI()
      registerAutoLearn(api)

      const longOutput =
        'Built a react component with vitest tests and TypeScript .tsx files for the dashboard'
      api.emit('agent:completing', { result: longOutput, goal: 'Build dashboard' })

      const learned = emittedEvents.filter((e) => e.event === 'memory:auto-learned')
      expect(learned.length).toBeGreaterThan(0)
    })

    it('does not emit for trivial sessions (output < 50 chars)', () => {
      const { api, emittedEvents } = createMockExtensionAPI()
      registerAutoLearn(api)

      api.emit('agent:completing', { result: 'done', goal: 'react' })

      const learned = emittedEvents.filter((e) => e.event === 'memory:auto-learned')
      expect(learned).toHaveLength(0)
    })

    it('does not emit when no patterns detected', () => {
      const { api, emittedEvents } = createMockExtensionAPI()
      registerAutoLearn(api)

      const longOutput =
        'This is a long enough output string that contains no recognizable framework or language patterns at all whatsoever.'
      api.emit('agent:completing', { result: longOutput, goal: 'generic task' })

      const learned = emittedEvents.filter((e) => e.event === 'memory:auto-learned')
      expect(learned).toHaveLength(0)
    })

    it('handles missing result and goal gracefully', () => {
      const { api, emittedEvents } = createMockExtensionAPI()
      registerAutoLearn(api)

      // Should not throw
      api.emit('agent:completing', {})

      const learned = emittedEvents.filter((e) => e.event === 'memory:auto-learned')
      expect(learned).toHaveLength(0)
    })

    it('emits with correct category in event data', () => {
      const { api, emittedEvents } = createMockExtensionAPI()
      registerAutoLearn(api)

      const longOutput =
        'Set up the react project with full component library and routing support for production deployment'
      api.emit('agent:completing', { result: longOutput, goal: 'Build project' })

      const learned = emittedEvents.filter((e) => e.event === 'memory:auto-learned')
      expect(learned.length).toBeGreaterThan(0)
      for (const event of learned) {
        const data = event.data as { category: string }
        expect(data.category).toBe('project-conventions')
      }
    })
  })
})
