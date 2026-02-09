/**
 * File Locking System Tests
 * Tests for promise-chain based file locking
 */

import { afterEach, describe, expect, it } from 'vitest'
import {
  clearAllLocks,
  getActiveLocks,
  getLockInfo,
  isFileLocked,
  tryFileLock,
  withFileLock,
} from './locks.js'

// Clean up locks between every test to prevent cross-contamination
afterEach(() => {
  clearAllLocks()
})

// ============================================================================
// Helpers
// ============================================================================

/** Create a deferred promise that can be resolved externally */
function createDeferred(): { promise: Promise<void>; resolve: () => void } {
  let resolve!: () => void
  const promise = new Promise<void>((r) => {
    resolve = r
  })
  return { promise, resolve }
}

// ============================================================================
// withFileLock
// ============================================================================

describe('withFileLock', () => {
  it('should execute function and return its result', async () => {
    const result = await withFileLock('/test/file.ts', async () => {
      return 42
    })
    expect(result).toBe(42)
  })

  it('should return string results', async () => {
    const result = await withFileLock('/test/file.ts', async () => {
      return 'hello'
    })
    expect(result).toBe('hello')
  })

  it('should return object results', async () => {
    const result = await withFileLock('/test/file.ts', async () => {
      return { key: 'value' }
    })
    expect(result).toEqual({ key: 'value' })
  })

  it('should serialize concurrent operations on the same path', async () => {
    const order: number[] = []
    const deferred1 = createDeferred()
    const deferred2 = createDeferred()

    // Start first lock - will hold until deferred1 resolves
    const p1 = withFileLock('/test/file.ts', async () => {
      order.push(1)
      await deferred1.promise
      order.push(2)
      return 'first'
    })

    // Start second lock - must wait for first to complete
    const p2 = withFileLock('/test/file.ts', async () => {
      order.push(3)
      await deferred2.promise
      order.push(4)
      return 'second'
    })

    // Allow a microtask tick so locks are queued
    await new Promise((r) => setTimeout(r, 10))

    // First lock should have started
    expect(order).toEqual([1])

    // Release first lock
    deferred1.resolve()
    await p1

    // Allow second lock to start
    await new Promise((r) => setTimeout(r, 10))
    expect(order).toContain(2)
    expect(order).toContain(3)

    // Release second lock
    deferred2.resolve()
    await p2

    // Verify strict ordering: 1, 2 (first completes), then 3, 4
    expect(order).toEqual([1, 2, 3, 4])
  })

  it('should allow concurrent operations on different paths', async () => {
    const order: string[] = []
    const deferred1 = createDeferred()
    const deferred2 = createDeferred()

    const p1 = withFileLock('/path/a.ts', async () => {
      order.push('a-start')
      await deferred1.promise
      order.push('a-end')
      return 'a'
    })

    const p2 = withFileLock('/path/b.ts', async () => {
      order.push('b-start')
      await deferred2.promise
      order.push('b-end')
      return 'b'
    })

    await new Promise((r) => setTimeout(r, 10))

    // Both should have started concurrently
    expect(order).toContain('a-start')
    expect(order).toContain('b-start')

    deferred1.resolve()
    deferred2.resolve()

    const [r1, r2] = await Promise.all([p1, p2])
    expect(r1).toBe('a')
    expect(r2).toBe('b')
  })

  it('should release lock after function throws an error', async () => {
    const error = new Error('test error')

    await expect(
      withFileLock('/test/file.ts', async () => {
        throw error
      })
    ).rejects.toThrow('test error')

    // Lock should be released
    expect(isFileLocked('/test/file.ts')).toBe(false)
  })

  it('should propagate the error from fn', async () => {
    await expect(
      withFileLock('/test/file.ts', async () => {
        throw new TypeError('type issue')
      })
    ).rejects.toThrow(TypeError)
  })

  it('should release lock after successful execution', async () => {
    await withFileLock('/test/file.ts', async () => {
      return 'done'
    })

    expect(isFileLocked('/test/file.ts')).toBe(false)
  })

  it('should normalize paths with backslashes', async () => {
    const deferred = createDeferred()

    const p = withFileLock('C:\\Users\\test\\file.ts', async () => {
      await deferred.promise
      return 'ok'
    })

    await new Promise((r) => setTimeout(r, 10))

    // Should be locked under normalized path
    expect(isFileLocked('C:/Users/test/file.ts')).toBe(true)
    expect(isFileLocked('C:\\Users\\test\\file.ts')).toBe(true)

    deferred.resolve()
    await p
  })

  it('should normalize paths with trailing slashes', async () => {
    const deferred = createDeferred()

    const p = withFileLock('/test/dir/', async () => {
      await deferred.promise
      return 'ok'
    })

    await new Promise((r) => setTimeout(r, 10))

    // Both forms should see the lock
    expect(isFileLocked('/test/dir/')).toBe(true)
    expect(isFileLocked('/test/dir')).toBe(true)

    deferred.resolve()
    await p
  })

  it('should treat paths with and without trailing slash as same lock', async () => {
    const order: number[] = []
    const deferred = createDeferred()

    const p1 = withFileLock('/test/dir/', async () => {
      order.push(1)
      await deferred.promise
      order.push(2)
      return 'first'
    })

    const p2 = withFileLock('/test/dir', async () => {
      order.push(3)
      return 'second'
    })

    await new Promise((r) => setTimeout(r, 10))
    // Only first should have started
    expect(order).toEqual([1])

    deferred.resolve()
    await Promise.all([p1, p2])
    expect(order).toEqual([1, 2, 3])
  })

  it('should pass through operation description to lock info', async () => {
    const deferred = createDeferred()

    const p = withFileLock(
      '/test/file.ts',
      async () => {
        await deferred.promise
        return 'ok'
      },
      'write operation'
    )

    await new Promise((r) => setTimeout(r, 10))

    const info = getLockInfo('/test/file.ts')
    expect(info).not.toBeNull()
    expect(info!.operation).toBe('write operation')

    deferred.resolve()
    await p
  })

  it('should work without operation description', async () => {
    const deferred = createDeferred()

    const p = withFileLock('/test/file.ts', async () => {
      await deferred.promise
      return 'ok'
    })

    await new Promise((r) => setTimeout(r, 10))

    const info = getLockInfo('/test/file.ts')
    expect(info).not.toBeNull()
    expect(info!.operation).toBeUndefined()

    deferred.resolve()
    await p
  })

  it('should handle many sequential locks on same path', async () => {
    const results: number[] = []

    for (let i = 0; i < 10; i++) {
      const val = await withFileLock('/test/file.ts', async () => {
        return i
      })
      results.push(val)
    }

    expect(results).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
  })

  it('should handle concurrent locks queued in order', async () => {
    const results: number[] = []
    const deferreds = Array.from({ length: 5 }, () => createDeferred())

    const promises = deferreds.map((deferred, i) =>
      withFileLock('/test/file.ts', async () => {
        await deferred.promise
        results.push(i)
        return i
      })
    )

    // Release in order
    for (const deferred of deferreds) {
      await new Promise((r) => setTimeout(r, 5))
      deferred.resolve()
    }

    const values = await Promise.all(promises)
    expect(values).toEqual([0, 1, 2, 3, 4])
    expect(results).toEqual([0, 1, 2, 3, 4])
  })
})

// ============================================================================
// tryFileLock
// ============================================================================

describe('tryFileLock', () => {
  it('should return result when path is not locked', async () => {
    const result = await tryFileLock('/test/file.ts', async () => {
      return 'success'
    })
    expect(result).toBe('success')
  })

  it('should return null when path is already locked', async () => {
    const deferred = createDeferred()

    // Start a lock that holds
    const p = withFileLock('/test/file.ts', async () => {
      await deferred.promise
      return 'held'
    })

    await new Promise((r) => setTimeout(r, 10))

    // tryFileLock should return null immediately
    const result = await tryFileLock('/test/file.ts', async () => {
      return 'should not run'
    })
    expect(result).toBeNull()

    deferred.resolve()
    await p
  })

  it('should pass through operation description', async () => {
    const deferred = createDeferred()

    const p = tryFileLock(
      '/test/file.ts',
      async () => {
        await deferred.promise
        return 'ok'
      },
      'try operation'
    )

    await new Promise((r) => setTimeout(r, 10))

    const info = getLockInfo('/test/file.ts')
    expect(info).not.toBeNull()
    expect(info!.operation).toBe('try operation')

    deferred.resolve()
    await p
  })

  it('should normalize paths before checking', async () => {
    const deferred = createDeferred()

    const p = withFileLock('/test/file.ts', async () => {
      await deferred.promise
      return 'held'
    })

    await new Promise((r) => setTimeout(r, 10))

    // Try with backslashes - should still see the lock
    const result = await tryFileLock('\\test\\file.ts', async () => {
      return 'should not run'
    })
    expect(result).toBeNull()

    deferred.resolve()
    await p
  })

  it('should release lock after successful execution', async () => {
    await tryFileLock('/test/file.ts', async () => {
      return 'done'
    })

    expect(isFileLocked('/test/file.ts')).toBe(false)
  })
})

// ============================================================================
// isFileLocked
// ============================================================================

describe('isFileLocked', () => {
  it('should return false when no lock is held', () => {
    expect(isFileLocked('/test/file.ts')).toBe(false)
  })

  it('should return true during active lock', async () => {
    const deferred = createDeferred()

    const p = withFileLock('/test/file.ts', async () => {
      await deferred.promise
      return 'ok'
    })

    await new Promise((r) => setTimeout(r, 10))
    expect(isFileLocked('/test/file.ts')).toBe(true)

    deferred.resolve()
    await p
    expect(isFileLocked('/test/file.ts')).toBe(false)
  })

  it('should normalize paths for checking', () => {
    // No lock held - just verify normalization does not crash
    expect(isFileLocked('C:\\test\\file.ts')).toBe(false)
    expect(isFileLocked('/test/dir/')).toBe(false)
  })
})

// ============================================================================
// getActiveLocks
// ============================================================================

describe('getActiveLocks', () => {
  it('should return empty array when no locks', () => {
    expect(getActiveLocks()).toEqual([])
  })

  it('should return info for all active locks', async () => {
    const deferred1 = createDeferred()
    const deferred2 = createDeferred()

    const p1 = withFileLock(
      '/test/a.ts',
      async () => {
        await deferred1.promise
        return 'a'
      },
      'op-a'
    )

    const p2 = withFileLock(
      '/test/b.ts',
      async () => {
        await deferred2.promise
        return 'b'
      },
      'op-b'
    )

    await new Promise((r) => setTimeout(r, 10))

    const locks = getActiveLocks()
    expect(locks).toHaveLength(2)

    const paths = locks.map((l) => l.path)
    expect(paths).toContain('/test/a.ts')
    expect(paths).toContain('/test/b.ts')

    const opA = locks.find((l) => l.path === '/test/a.ts')
    expect(opA!.operation).toBe('op-a')
    expect(opA!.acquiredAt).toBeGreaterThan(0)

    deferred1.resolve()
    deferred2.resolve()
    await Promise.all([p1, p2])
  })

  it('should return empty array after locks are released', async () => {
    await withFileLock('/test/file.ts', async () => 'done')
    expect(getActiveLocks()).toEqual([])
  })
})

// ============================================================================
// getLockInfo
// ============================================================================

describe('getLockInfo', () => {
  it('should return null for unlocked path', () => {
    expect(getLockInfo('/test/file.ts')).toBeNull()
  })

  it('should return info for locked path', async () => {
    const deferred = createDeferred()

    const p = withFileLock(
      '/test/file.ts',
      async () => {
        await deferred.promise
        return 'ok'
      },
      'read operation'
    )

    await new Promise((r) => setTimeout(r, 10))

    const info = getLockInfo('/test/file.ts')
    expect(info).not.toBeNull()
    expect(info!.path).toBe('/test/file.ts')
    expect(info!.operation).toBe('read operation')
    expect(info!.acquiredAt).toBeGreaterThan(0)
    expect(typeof info!.waitingCount).toBe('number')

    deferred.resolve()
    await p
  })

  it('should normalize path before looking up', async () => {
    const deferred = createDeferred()

    const p = withFileLock('/test/file.ts', async () => {
      await deferred.promise
      return 'ok'
    })

    await new Promise((r) => setTimeout(r, 10))

    // Query with backslash path
    const info = getLockInfo('\\test\\file.ts')
    expect(info).not.toBeNull()
    expect(info!.path).toBe('/test/file.ts')

    deferred.resolve()
    await p
  })

  it('should return null after lock is released', async () => {
    await withFileLock('/test/file.ts', async () => 'done')
    expect(getLockInfo('/test/file.ts')).toBeNull()
  })
})

// ============================================================================
// clearAllLocks
// ============================================================================

describe('clearAllLocks', () => {
  it('should clear all active locks', async () => {
    const deferred1 = createDeferred()
    const deferred2 = createDeferred()

    const p1 = withFileLock('/test/a.ts', async () => {
      await deferred1.promise
      return 'a'
    })

    const p2 = withFileLock('/test/b.ts', async () => {
      await deferred2.promise
      return 'b'
    })

    await new Promise((r) => setTimeout(r, 10))
    expect(getActiveLocks()).toHaveLength(2)

    clearAllLocks()

    expect(getActiveLocks()).toEqual([])
    expect(isFileLocked('/test/a.ts')).toBe(false)
    expect(isFileLocked('/test/b.ts')).toBe(false)

    // Clean up - the promises should resolve since locks were released
    deferred1.resolve()
    deferred2.resolve()
    await Promise.allSettled([p1, p2])
  })

  it('should be safe to call when no locks exist', () => {
    expect(() => clearAllLocks()).not.toThrow()
  })

  it('should allow new locks after clearing', async () => {
    clearAllLocks()

    const result = await withFileLock('/test/new.ts', async () => 'fresh')
    expect(result).toBe('fresh')
  })
})
