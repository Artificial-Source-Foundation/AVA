/**
 * Intent Classifier Tests
 *
 * Consolidated tests using representative sampling.
 */

import { describe, it, expect } from 'vitest'
import {
  classifyIntent,
  formatClassificationForPrompt,
  getIntentLabel,
} from '../../src/planning/intent-classifier.js'

describe('classifyIntent', () => {
  it('should classify intents correctly', () => {
    expect(classifyIntent('Refactor the authentication module').intent).toBe('refactoring')
    expect(classifyIntent('Create a new user dashboard').intent).toBe('build')
    expect(classifyIntent('Fix the login bug').intent).toBe('fix')
    expect(classifyIntent('How should we structure the API?').intent).toBe('architecture')
    expect(classifyIntent('Research best practices for caching').intent).toBe('research')
  })

  it('should suggest appropriate tools for each intent', () => {
    const refactor = classifyIntent('Refactor the auth module')
    expect(refactor.suggestedTools).toContain('validator')
    expect(refactor.planningFocus).toContain('SAFETY')

    const build = classifyIntent('Create a new feature')
    expect(build.suggestedTools).toContain('explorer')
    expect(build.planningFocus).toContain('DISCOVERY')

    const architecture = classifyIntent('How should we architect this?')
    expect(architecture.suggestedTools).toContain('consult_council')
    expect(architecture.planningFocus).toContain('STRATEGIC')
  })

  it('should default to build for ambiguous requests', () => {
    expect(classifyIntent('Do something with the code').intent).toBe('build')
  })

  it('should have higher confidence with more keyword matches', () => {
    const low = classifyIntent('Fix it')
    const high = classifyIntent('Fix the bug, resolve the error, debug the issue')
    expect(high.confidence).toBeGreaterThan(low.confidence)
  })
})

describe('formatClassificationForPrompt', () => {
  it('should format classification as markdown', () => {
    const formatted = formatClassificationForPrompt(classifyIntent('Refactor the auth module'))
    expect(formatted).toContain('## INTENT CLASSIFICATION')
    expect(formatted).toContain('REFACTORING')
    expect(formatted).toContain('Confidence')
  })
})

describe('getIntentLabel', () => {
  it('should return human-readable labels', () => {
    expect(getIntentLabel('refactoring')).toBe('Refactoring')
    expect(getIntentLabel('build')).toBe('Build/Create')
    expect(getIntentLabel('fix')).toBe('Bug Fix')
    expect(getIntentLabel('architecture')).toBe('Architecture')
    expect(getIntentLabel('research')).toBe('Research')
  })
})
