import { describe, expect, it } from 'vitest'
import { getDefaultModelConfig } from './model-config.js'
import { FALLBACK_CHAINS, resolveModel } from './model-fallback.js'

describe('model fallback', () => {
  it('uses configured model when available', () => {
    const config = getDefaultModelConfig()
    const resolved = resolveModel('engineer', config)
    expect(resolved.provider).toBe(config.engineer.provider)
  })

  it('defines fallback chains for every role', () => {
    expect(FALLBACK_CHAINS.director.length).toBeGreaterThan(0)
    expect(FALLBACK_CHAINS['tech-lead'].length).toBeGreaterThan(0)
    expect(FALLBACK_CHAINS.engineer.length).toBeGreaterThan(0)
    expect(FALLBACK_CHAINS.reviewer.length).toBeGreaterThan(0)
  })
})
