import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { buildSystemPromptAfterInstructions, resetInstructionsLoaded } from './prompt-builder'

describe('buildSystemPromptAfterInstructions', () => {
  beforeEach(() => {
    resetInstructionsLoaded()
    vi.useFakeTimers()
  })

  afterEach(() => {
    vi.useRealTimers()
    vi.restoreAllMocks()
  })

  it('removes the temporary instructions listener after timeout', async () => {
    const removeSpy = vi.spyOn(window, 'removeEventListener')

    const promise = buildSystemPromptAfterInstructions('model', '/tmp/project')
    await vi.advanceTimersByTimeAsync(1500)
    await promise

    expect(removeSpy).toHaveBeenCalledWith('ava:instructions-loaded', expect.any(Function))
  })
})
