import { describe, expect, it } from 'vitest'
import { REMOVED_DELEGATE_TOOLS } from './delegate.js'

describe('delegate migration', () => {
  it('removes legacy delegate tools in Praxis v2', () => {
    expect(REMOVED_DELEGATE_TOOLS).toEqual([
      'delegate_coder',
      'delegate_reviewer',
      'delegate_researcher',
      'delegate_explorer',
    ])
  })
})
