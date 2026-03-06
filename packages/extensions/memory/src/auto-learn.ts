/**
 * Auto-learning memory — extract patterns from agent sessions.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createLogger } from '@ava/core-v2/logger'

const log = createLogger('AutoLearn')

export interface LearnedItem {
  category: 'learned-patterns' | 'user-preferences' | 'project-conventions'
  key: string
  value: string
  confidence: number // 0-1, only persist if > 0.7
  source: string // session ID
}

interface PatternDetector {
  name: string
  detect(sessionOutput: string, goal: string): LearnedItem[]
}

const detectors: PatternDetector[] = [
  {
    name: 'tech-stack',
    detect(output, goal) {
      const items: LearnedItem[] = []
      const frameworks = [
        'react',
        'vue',
        'angular',
        'express',
        'fastify',
        'next.js',
        'solid',
        'svelte',
      ]
      for (const fw of frameworks) {
        if (output.toLowerCase().includes(fw) || goal.toLowerCase().includes(fw)) {
          items.push({
            category: 'project-conventions',
            key: `uses-${fw}`,
            value: `This project uses ${fw}`,
            confidence: 0.8,
            source: 'auto-detected',
          })
        }
      }
      return items
    },
  },
  {
    name: 'test-framework',
    detect(output, _goal) {
      const items: LearnedItem[] = []
      const testFw = [
        { pattern: /vitest|import.*from.*vitest/i, name: 'vitest' },
        { pattern: /jest|describe.*it.*expect/i, name: 'jest' },
        { pattern: /mocha|chai/i, name: 'mocha' },
        { pattern: /pytest/i, name: 'pytest' },
      ]
      for (const { pattern, name } of testFw) {
        if (pattern.test(output)) {
          items.push({
            category: 'project-conventions',
            key: 'test-framework',
            value: `Project uses ${name} for testing`,
            confidence: 0.9,
            source: 'auto-detected',
          })
          break
        }
      }
      return items
    },
  },
  {
    name: 'language',
    detect(output, goal) {
      const items: LearnedItem[] = []
      const langs = [
        { pattern: /\.tsx?|typescript|tsc/i, name: 'TypeScript' },
        { pattern: /\.py|python|pip/i, name: 'Python' },
        { pattern: /\.rs|rust|cargo/i, name: 'Rust' },
        { pattern: /\.go|golang/i, name: 'Go' },
      ]
      for (const { pattern, name } of langs) {
        if (pattern.test(output) || pattern.test(goal)) {
          items.push({
            category: 'project-conventions',
            key: 'primary-language',
            value: `Primary language: ${name}`,
            confidence: 0.85,
            source: 'auto-detected',
          })
          break
        }
      }
      return items
    },
  },
]

export function analyzeSession(output: string, goal: string): LearnedItem[] {
  const allItems: LearnedItem[] = []
  for (const detector of detectors) {
    const items = detector.detect(output, goal)
    allItems.push(...items.filter((i) => i.confidence >= 0.7))
  }
  return allItems
}

export function registerAutoLearn(api: ExtensionAPI): Disposable {
  return api.on('agent:completing', (data: unknown) => {
    const event = data as Record<string, unknown>
    const output = (event.result as string) ?? ''
    const goal = (event.goal as string) ?? ''
    if (output.length < 50) return // skip trivial sessions

    const items = analyzeSession(output, goal)
    for (const item of items) {
      log.debug(`Auto-learned: ${item.key} = ${item.value}`)
      api.emit('memory:auto-learned', { key: item.key, value: item.value, category: item.category })
    }
  })
}
