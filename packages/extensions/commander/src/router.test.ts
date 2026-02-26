import { describe, expect, it } from 'vitest'
import { analyzeTask, getFilteredTools, selectWorker } from './router.js'
import { BUILTIN_WORKERS } from './workers.js'

describe('Task Router', () => {
  describe('analyzeTask', () => {
    it('identifies test tasks', () => {
      const result = analyzeTask('Write unit tests for the auth module')
      expect(result.taskType).toBe('test')
      expect(result.confidence).toBeGreaterThan(0.7)
    })

    it('identifies debug tasks', () => {
      const result = analyzeTask('Fix the bug in the login flow')
      expect(result.taskType).toBe('debug')
      expect(result.confidence).toBeGreaterThan(0.7)
    })

    it('identifies review tasks', () => {
      const result = analyzeTask('Review the code for security issues')
      expect(result.taskType).toBe('review')
      expect(result.confidence).toBeGreaterThan(0.7)
    })

    it('identifies research tasks', () => {
      const result = analyzeTask('Research how the auth system works')
      expect(result.taskType).toBe('research')
      expect(result.confidence).toBeGreaterThan(0.7)
    })

    it('identifies write tasks', () => {
      const result = analyzeTask('Implement a new user registration feature')
      expect(result.taskType).toBe('write')
      expect(result.confidence).toBeGreaterThan(0.7)
    })

    it('returns general for ambiguous tasks', () => {
      const result = analyzeTask('Hello, how are you?')
      expect(result.taskType).toBe('general')
      expect(result.confidence).toBe(0)
    })

    it('increases confidence for multiple keyword matches', () => {
      const single = analyzeTask('Fix the error')
      const multi = analyzeTask('Fix the bug error, debug the crash')
      expect(multi.confidence).toBeGreaterThan(single.confidence)
    })
  })

  describe('selectWorker', () => {
    it('selects tester for test tasks', () => {
      const analysis = analyzeTask('Write unit tests')
      const worker = selectWorker(analysis, BUILTIN_WORKERS)
      expect(worker?.name).toBe('tester')
    })

    it('selects debugger for debug tasks', () => {
      const analysis = analyzeTask('Fix the bug')
      const worker = selectWorker(analysis, BUILTIN_WORKERS)
      expect(worker?.name).toBe('debugger')
    })

    it('selects coder for write tasks', () => {
      const analysis = analyzeTask('Implement the feature')
      const worker = selectWorker(analysis, BUILTIN_WORKERS)
      expect(worker?.name).toBe('coder')
    })

    it('returns null for low confidence', () => {
      const analysis = analyzeTask('Hello world')
      const worker = selectWorker(analysis, BUILTIN_WORKERS)
      expect(worker).toBeNull()
    })

    it('respects custom minConfidence', () => {
      const analysis = analyzeTask('maybe add something')
      const worker = selectWorker(analysis, BUILTIN_WORKERS, 0.9)
      expect(worker).toBeNull()
    })
  })

  describe('getFilteredTools', () => {
    it('removes delegate_ prefixed tools', () => {
      const tools = ['read_file', 'delegate_coder', 'write_file', 'delegate_tester']
      expect(getFilteredTools(tools)).toEqual(['read_file', 'write_file'])
    })

    it('keeps all tools when no delegates', () => {
      const tools = ['read_file', 'write_file', 'bash']
      expect(getFilteredTools(tools)).toEqual(tools)
    })
  })
})
