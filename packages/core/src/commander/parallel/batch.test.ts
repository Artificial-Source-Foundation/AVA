/**
 * Semaphore Tests
 *
 * Tests for the concurrency-limiting semaphore used in batch execution.
 * (executeBatch itself requires AgentExecutor/LLM and is not tested here)
 */

import { describe, expect, it } from 'vitest'
import { Semaphore } from './batch.js'

// ============================================================================
// Semaphore
// ============================================================================

describe('Semaphore', () => {
  it('starts with specified concurrency', () => {
    const sem = new Semaphore(4)
    expect(sem.getAvailable()).toBe(4)
    expect(sem.getQueueLength()).toBe(0)
  })

  it('clamps to minimum of 1', () => {
    const sem = new Semaphore(0)
    expect(sem.getAvailable()).toBe(1)
  })

  it('clamps to maximum of 8', () => {
    const sem = new Semaphore(100)
    expect(sem.getAvailable()).toBe(8)
  })

  it('acquire decrements available', async () => {
    const sem = new Semaphore(3)

    await sem.acquire()
    expect(sem.getAvailable()).toBe(2)

    await sem.acquire()
    expect(sem.getAvailable()).toBe(1)
  })

  it('release increments available', async () => {
    const sem = new Semaphore(2)

    await sem.acquire()
    expect(sem.getAvailable()).toBe(1)

    sem.release()
    expect(sem.getAvailable()).toBe(2)
  })

  it('queues when no permits available', async () => {
    const sem = new Semaphore(1)

    // Acquire the only permit
    await sem.acquire()
    expect(sem.getAvailable()).toBe(0)

    // Next acquire should queue
    let acquired = false
    const acquirePromise = sem.acquire().then(() => {
      acquired = true
    })

    // Should be queued
    expect(sem.getQueueLength()).toBe(1)
    expect(acquired).toBe(false)

    // Release should unblock the queued acquire
    sem.release()

    await acquirePromise
    expect(acquired).toBe(true)
    expect(sem.getQueueLength()).toBe(0)
  })

  it('maintains FIFO order for queued acquires', async () => {
    const sem = new Semaphore(1)
    const order: number[] = []

    await sem.acquire() // Take the only permit

    // Queue 3 waiters
    const p1 = sem.acquire().then(() => order.push(1))
    const p2 = sem.acquire().then(() => order.push(2))
    const p3 = sem.acquire().then(() => order.push(3))

    expect(sem.getQueueLength()).toBe(3)

    // Release in sequence
    sem.release() // Unblocks first waiter
    await p1
    sem.release() // Unblocks second waiter
    await p2
    sem.release() // Unblocks third waiter
    await p3

    expect(order).toEqual([1, 2, 3])
  })

  it('handles concurrent acquire/release cycles', async () => {
    const sem = new Semaphore(2)
    let maxConcurrent = 0
    let currentConcurrent = 0

    const task = async () => {
      await sem.acquire()
      currentConcurrent++
      if (currentConcurrent > maxConcurrent) {
        maxConcurrent = currentConcurrent
      }
      // Simulate work
      await new Promise((r) => setTimeout(r, 10))
      currentConcurrent--
      sem.release()
    }

    // Run 5 tasks with concurrency limit of 2
    await Promise.all([task(), task(), task(), task(), task()])

    expect(maxConcurrent).toBeLessThanOrEqual(2)
  })
})
