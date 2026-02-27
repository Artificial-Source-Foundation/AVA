import { beforeEach, describe, expect, it } from 'vitest'
import {
  addFocusItem,
  completeFocusItem,
  createFocusChain,
  getActiveItems,
  getPendingItems,
  resetIdCounter,
  startFocusItem,
} from './tracker.js'

describe('focus chain tracker', () => {
  beforeEach(() => resetIdCounter())

  it('creates an empty focus chain', () => {
    const chain = createFocusChain('session-1')
    expect(chain.sessionId).toBe('session-1')
    expect(chain.items).toHaveLength(0)
    expect(chain.currentFocus).toBeUndefined()
  })

  it('adds focus items', () => {
    const chain = createFocusChain('session-1')
    const item = addFocusItem(chain, 'Implement feature')
    expect(item.id).toBe('focus-1')
    expect(item.status).toBe('pending')
    expect(chain.items).toHaveLength(1)
  })

  it('starts a focus item', () => {
    const chain = createFocusChain('session-1')
    const item = addFocusItem(chain, 'Task 1')
    expect(startFocusItem(chain, item.id)).toBe(true)
    expect(item.status).toBe('in_progress')
    expect(chain.currentFocus).toBe(item.id)
  })

  it('completes a focus item', () => {
    const chain = createFocusChain('session-1')
    const item = addFocusItem(chain, 'Task 1')
    startFocusItem(chain, item.id)
    expect(completeFocusItem(chain, item.id)).toBe(true)
    expect(item.status).toBe('completed')
    expect(item.completedAt).toBeDefined()
    expect(chain.currentFocus).toBeUndefined()
  })

  it('cannot start an already in-progress item', () => {
    const chain = createFocusChain('session-1')
    const item = addFocusItem(chain, 'Task 1')
    startFocusItem(chain, item.id)
    expect(startFocusItem(chain, item.id)).toBe(false)
  })

  it('cannot complete an already completed item', () => {
    const chain = createFocusChain('session-1')
    const item = addFocusItem(chain, 'Task 1')
    startFocusItem(chain, item.id)
    completeFocusItem(chain, item.id)
    expect(completeFocusItem(chain, item.id)).toBe(false)
  })

  it('getActiveItems returns in-progress items', () => {
    const chain = createFocusChain('session-1')
    addFocusItem(chain, 'Pending')
    const active = addFocusItem(chain, 'Active')
    startFocusItem(chain, active.id)
    expect(getActiveItems(chain)).toHaveLength(1)
    expect(getPendingItems(chain)).toHaveLength(1)
  })

  it('supports parent items', () => {
    const chain = createFocusChain('session-1')
    const parent = addFocusItem(chain, 'Parent task')
    const child = addFocusItem(chain, 'Child task', parent.id)
    expect(child.parentId).toBe(parent.id)
  })
})
