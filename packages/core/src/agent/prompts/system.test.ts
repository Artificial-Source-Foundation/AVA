/**
 * System Prompt Builder Tests
 */

import { describe, expect, it } from 'vitest'
import type { SystemPromptContext } from './system.js'
import {
  BEST_PRACTICES,
  buildScenarioPrompt,
  buildSystemPrompt,
  buildWorkerPrompt,
  CAPABILITIES,
  getModelAdjustments,
  RULES,
} from './system.js'

const baseContext: SystemPromptContext = {
  cwd: '/home/user/project',
  os: 'linux',
  shell: 'bash',
}

// ============================================================================
// Constants
// ============================================================================

describe('RULES', () => {
  it('contains CWD placeholder', () => {
    expect(RULES).toContain('{{CWD}}')
  })

  it('contains OS placeholder', () => {
    expect(RULES).toContain('{{OS}}')
  })

  it('contains environment section', () => {
    expect(RULES).toContain('### Environment')
  })

  it('contains tool usage section', () => {
    expect(RULES).toContain('### Tool Usage')
  })

  it('contains code changes section', () => {
    expect(RULES).toContain('### Code Changes')
  })

  it('contains safety section', () => {
    expect(RULES).toContain('### Safety')
  })
})

describe('CAPABILITIES', () => {
  it('contains file operations', () => {
    expect(CAPABILITIES).toContain('### File Operations')
  })

  it('mentions glob tool', () => {
    expect(CAPABILITIES).toContain('glob')
  })

  it('mentions bash tool', () => {
    expect(CAPABILITIES).toContain('bash')
  })

  it('mentions attempt_completion', () => {
    expect(CAPABILITIES).toContain('attempt_completion')
  })
})

describe('BEST_PRACTICES', () => {
  it('contains before/after making changes', () => {
    expect(BEST_PRACTICES).toContain('Before Making Changes')
    expect(BEST_PRACTICES).toContain('After Making Changes')
  })

  it('contains debugging section', () => {
    expect(BEST_PRACTICES).toContain('When Debugging')
  })
})

// ============================================================================
// buildSystemPrompt
// ============================================================================

describe('buildSystemPrompt', () => {
  it('includes working directory', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).toContain('/home/user/project')
  })

  it('replaces CWD placeholder in rules', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).not.toContain('{{CWD}}')
    // CWD appears in rules as replaced
    const cwdCount = (prompt.match(/\/home\/user\/project/g) || []).length
    expect(cwdCount).toBeGreaterThanOrEqual(2) // environment + rules
  })

  it('replaces OS placeholder in rules', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).not.toContain('{{OS}}')
  })

  it('includes role introduction', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).toContain('autonomous coding agent')
  })

  it('includes environment section', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).toContain('Working Directory')
    expect(prompt).toContain('Operating System')
  })

  it('uses default OS when not provided', () => {
    const prompt = buildSystemPrompt({ cwd: '/tmp' })
    expect(prompt).toContain('unknown')
  })

  it('uses default shell when not provided', () => {
    const prompt = buildSystemPrompt({ cwd: '/tmp' })
    expect(prompt).toContain('bash')
  })

  it('includes capabilities', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).toContain('CAPABILITIES')
  })

  it('includes best practices', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).toContain('BEST PRACTICES')
  })

  it('includes custom context when provided', () => {
    const prompt = buildSystemPrompt({
      ...baseContext,
      customContext: 'This project uses React with TypeScript',
    })
    expect(prompt).toContain('This project uses React with TypeScript')
    expect(prompt).toContain('## CONTEXT')
  })

  it('omits context section when no custom context', () => {
    const prompt = buildSystemPrompt(baseContext)
    expect(prompt).not.toContain('## CONTEXT')
  })

  it('handles darwin OS', () => {
    const prompt = buildSystemPrompt({ ...baseContext, os: 'darwin' })
    expect(prompt).toContain('darwin')
  })

  it('handles win32 OS', () => {
    const prompt = buildSystemPrompt({ ...baseContext, os: 'win32' })
    expect(prompt).toContain('win32')
  })
})

// ============================================================================
// buildWorkerPrompt
// ============================================================================

describe('buildWorkerPrompt', () => {
  it('includes working directory', () => {
    const prompt = buildWorkerPrompt(baseContext)
    expect(prompt).toContain('/home/user/project')
  })

  it('includes OS', () => {
    const prompt = buildWorkerPrompt(baseContext)
    expect(prompt).toContain('linux')
  })

  it('is shorter than system prompt', () => {
    const system = buildSystemPrompt(baseContext)
    const worker = buildWorkerPrompt(baseContext)
    expect(worker.length).toBeLessThan(system.length)
  })

  it('includes attempt_completion rule', () => {
    const prompt = buildWorkerPrompt(baseContext)
    expect(prompt).toContain('attempt_completion')
  })

  it('includes custom context when provided', () => {
    const prompt = buildWorkerPrompt({ ...baseContext, customContext: 'Focus on CSS only' })
    expect(prompt).toContain('Focus on CSS only')
  })

  it('handles missing custom context', () => {
    const prompt = buildWorkerPrompt(baseContext)
    expect(prompt).toBeTruthy()
  })

  it('defaults OS to unknown', () => {
    const prompt = buildWorkerPrompt({ cwd: '/tmp' })
    expect(prompt).toContain('unknown')
  })
})

// ============================================================================
// buildScenarioPrompt
// ============================================================================

describe('buildScenarioPrompt', () => {
  it('returns debugging guidelines', () => {
    const prompt = buildScenarioPrompt('debugging')
    expect(prompt).toContain('Debugging Guidelines')
    expect(prompt).toContain('reproducing the issue')
  })

  it('returns refactoring guidelines', () => {
    const prompt = buildScenarioPrompt('refactoring')
    expect(prompt).toContain('Refactoring Guidelines')
    expect(prompt).toContain('incremental changes')
  })

  it('returns testing guidelines', () => {
    const prompt = buildScenarioPrompt('testing')
    expect(prompt).toContain('Testing Guidelines')
    expect(prompt).toContain('success and failure')
  })

  it('returns documentation guidelines', () => {
    const prompt = buildScenarioPrompt('documentation')
    expect(prompt).toContain('Documentation Guidelines')
    expect(prompt).toContain('concise but complete')
  })
})

// ============================================================================
// getModelAdjustments
// ============================================================================

describe('getModelAdjustments', () => {
  it('returns Claude-specific notes', () => {
    const adj = getModelAdjustments('claude')
    expect(adj).toContain('Claude-Specific')
    expect(adj).toContain('XML tags')
  })

  it('returns GPT-specific notes', () => {
    const adj = getModelAdjustments('gpt')
    expect(adj).toContain('GPT-Specific')
    expect(adj).toContain('markdown')
  })

  it('returns Gemini-specific notes', () => {
    const adj = getModelAdjustments('gemini')
    expect(adj).toContain('Gemini-Specific')
  })

  it('returns empty string for unknown model', () => {
    const adj = getModelAdjustments('unknown')
    expect(adj).toBe('')
  })
})
