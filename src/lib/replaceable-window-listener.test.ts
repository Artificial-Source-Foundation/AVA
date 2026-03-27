import { describe, expect, it, vi } from 'vitest'
import { installReplaceableWindowListener } from './replaceable-window-listener'

describe('installReplaceableWindowListener', () => {
  it('cleans up the previous listener before installing a replacement', () => {
    const firstCleanup = vi.fn()
    const secondCleanup = vi.fn()

    installReplaceableWindowListener('unit-test', () => firstCleanup)
    installReplaceableWindowListener('unit-test', () => secondCleanup)

    expect(firstCleanup).toHaveBeenCalledOnce()
    expect(secondCleanup).not.toHaveBeenCalled()
  })
})
