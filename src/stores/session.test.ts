import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { STORAGE_KEYS } from '../config/constants'
import { getLastSessionForProject, setLastSessionForProject } from './session-persistence'

describe('session store project persistence', () => {
  beforeEach(() => {
    localStorage.clear()
  })

  afterEach(() => {
    localStorage.clear()
  })

  it('stores and restores last session by project id', () => {
    setLastSessionForProject('project-a', 'session-1')
    setLastSessionForProject('project-b', 'session-2')

    expect(getLastSessionForProject('project-a')).toBe('session-1')
    expect(getLastSessionForProject('project-b')).toBe('session-2')
  })

  it('returns null when project mapping is missing', () => {
    expect(getLastSessionForProject('missing-project')).toBeNull()
  })

  it('returns null on corrupted mapping json', () => {
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION_BY_PROJECT, '{bad-json')

    expect(getLastSessionForProject('project-a')).toBeNull()
  })
})
