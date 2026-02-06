/**
 * Task Manager Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  agentMessage,
  createArtifactEvent,
  createStatusEvent,
  dataPart,
  type TaskEventListener,
  type TaskExecutor,
  TaskManager,
  textPart,
  userMessage,
} from './task.js'
import type { A2AEvent, A2AMessage, Artifact } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function makeUserMessage(text: string): A2AMessage {
  return { role: 'user', parts: [{ type: 'text', text }] }
}

function makeAgentMessage(text: string): A2AMessage {
  return { role: 'agent', parts: [{ type: 'text', text }] }
}

/** Mock executor that completes immediately */
function createMockExecutor(events: A2AEvent[] = []): TaskExecutor {
  return {
    execute: vi.fn(
      async (_goal: string, _cwd: string, _signal: AbortSignal, onEvent: TaskEventListener) => {
        for (const event of events) {
          onEvent(event)
        }
      }
    ),
  }
}

/** Mock executor that waits for abort */
function createBlockingExecutor(): TaskExecutor {
  return {
    execute: vi.fn(
      async (_goal: string, _cwd: string, signal: AbortSignal, _onEvent: TaskEventListener) => {
        return new Promise<void>((_resolve, reject) => {
          if (signal.aborted) {
            reject(new DOMException('Aborted', 'AbortError'))
            return
          }
          const handler = () => {
            signal.removeEventListener('abort', handler)
            reject(new DOMException('Aborted', 'AbortError'))
          }
          signal.addEventListener('abort', handler)
        })
      }
    ),
  }
}

/** Mock executor that throws */
function createFailingExecutor(errorMsg: string): TaskExecutor {
  return {
    execute: vi.fn(async () => {
      throw new Error(errorMsg)
    }),
  }
}

// ============================================================================
// Tests
// ============================================================================

describe('task', () => {
  let manager: TaskManager

  beforeEach(() => {
    manager = new TaskManager()
  })

  describe('TaskManager', () => {
    describe('createTask', () => {
      it('should create a task with submitted state', () => {
        const msg = makeUserMessage('Hello')
        const task = manager.createTask(msg)

        expect(task.id).toBeTruthy()
        expect(task.contextId).toBeTruthy()
        expect(task.status.state).toBe('submitted')
        expect(task.messages).toHaveLength(1)
        expect(task.messages[0]).toBe(msg)
        expect(task.artifacts).toHaveLength(0)
        expect(task.history).toHaveLength(1)
      })

      it('should use provided contextId', () => {
        const task = manager.createTask(makeUserMessage('Test'), 'ctx-123')
        expect(task.contextId).toBe('ctx-123')
      })

      it('should generate unique IDs', () => {
        const task1 = manager.createTask(makeUserMessage('A'))
        const task2 = manager.createTask(makeUserMessage('B'))
        expect(task1.id).not.toBe(task2.id)
      })
    })

    describe('getTask', () => {
      it('should retrieve an existing task', () => {
        const task = manager.createTask(makeUserMessage('Test'))
        const found = manager.getTask(task.id)
        expect(found).toBe(task)
      })

      it('should return undefined for unknown ID', () => {
        expect(manager.getTask('unknown')).toBeUndefined()
      })
    })

    describe('getAllTasks', () => {
      it('should return all tasks', () => {
        manager.createTask(makeUserMessage('A'))
        manager.createTask(makeUserMessage('B'))
        manager.createTask(makeUserMessage('C'))

        expect(manager.getAllTasks()).toHaveLength(3)
      })

      it('should return empty array when no tasks', () => {
        expect(manager.getAllTasks()).toHaveLength(0)
      })
    })

    describe('getOrCreateTask', () => {
      it('should create new task when no taskId', () => {
        const msg = makeUserMessage('New task')
        const task = manager.getOrCreateTask(msg)

        expect(task.id).toBeTruthy()
        expect(task.messages).toHaveLength(1)
      })

      it('should append to existing task when taskId matches', () => {
        const task = manager.createTask(makeUserMessage('First'))
        const msg2 = makeUserMessage('Second')

        const result = manager.getOrCreateTask(msg2, task.id)
        expect(result.id).toBe(task.id)
        expect(result.messages).toHaveLength(2)
      })

      it('should create new task when taskId not found', () => {
        const msg = makeUserMessage('New')
        const result = manager.getOrCreateTask(msg, 'nonexistent')

        expect(result.id).not.toBe('nonexistent')
        expect(result.messages).toHaveLength(1)
      })
    })

    describe('setState', () => {
      it('should update task state', () => {
        const task = manager.createTask(makeUserMessage('Test'))
        manager.setState(task, 'working')

        expect(task.status.state).toBe('working')
        expect(task.history).toHaveLength(2) // submitted + working
      })

      it('should include message in state update', () => {
        const task = manager.createTask(makeUserMessage('Test'))
        const msg = makeAgentMessage('Processing...')

        manager.setState(task, 'working', msg)

        expect(task.status.message).toBe(msg)
        expect(task.messages).toHaveLength(2)
      })

      it('should record state history', () => {
        const task = manager.createTask(makeUserMessage('Test'))
        manager.setState(task, 'working')
        manager.setState(task, 'completed')

        expect(task.history).toHaveLength(3)
        expect(task.history[0]!.state).toBe('submitted')
        expect(task.history[1]!.state).toBe('working')
        expect(task.history[2]!.state).toBe('completed')
      })
    })

    describe('addArtifact', () => {
      it('should add artifact to task', () => {
        const task = manager.createTask(makeUserMessage('Test'))
        const artifact: Artifact = {
          artifactId: 'art-1',
          name: 'result.txt',
          parts: [{ type: 'text', text: 'output' }],
        }

        const result = manager.addArtifact(task.id, artifact)
        expect(result).toBeDefined()
        expect(result!.artifacts).toHaveLength(1)
        expect(result!.artifacts[0]!.artifactId).toBe('art-1')
      })

      it('should return undefined for unknown task', () => {
        const artifact: Artifact = {
          artifactId: 'art-1',
          parts: [{ type: 'text', text: 'data' }],
        }
        expect(manager.addArtifact('unknown', artifact)).toBeUndefined()
      })
    })

    describe('cancelTask', () => {
      it('should cancel a submitted task', () => {
        const task = manager.createTask(makeUserMessage('Test'))
        const result = manager.cancelTask(task.id)

        expect(result).toBeDefined()
        expect(result!.status.state).toBe('canceled')
      })

      it('should return undefined for unknown task', () => {
        expect(manager.cancelTask('unknown')).toBeUndefined()
      })
    })

    describe('removeTask', () => {
      it('should remove a task', () => {
        const task = manager.createTask(makeUserMessage('Test'))
        expect(manager.removeTask(task.id)).toBe(true)
        expect(manager.getTask(task.id)).toBeUndefined()
      })

      it('should return false for unknown task', () => {
        expect(manager.removeTask('unknown')).toBe(false)
      })
    })

    describe('reset', () => {
      it('should clear all tasks', () => {
        manager.createTask(makeUserMessage('A'))
        manager.createTask(makeUserMessage('B'))
        manager.reset()

        expect(manager.getAllTasks()).toHaveLength(0)
      })
    })
  })

  describe('executeTask', () => {
    it('should transition to working and completed', async () => {
      const executor = createMockExecutor()
      manager.setExecutor(executor)

      const task = manager.createTask(makeUserMessage('Hello'))
      const events: A2AEvent[] = []

      for await (const event of manager.executeTask(task.id)) {
        events.push(event)
      }

      // Should have working + completed events
      const statusEvents = events.filter((e) => e.kind === 'status-update')
      expect(statusEvents.length).toBeGreaterThanOrEqual(2)

      // Final state should be completed
      const finalTask = manager.getTask(task.id)!
      expect(finalTask.status.state).toBe('completed')
    })

    it('should yield events from executor', async () => {
      const customEvent: A2AEvent = {
        kind: 'status-update',
        taskId: 'test',
        contextId: 'ctx',
        final: false,
        status: { state: 'working', timestamp: new Date().toISOString() },
      }

      const executor = createMockExecutor([customEvent])
      manager.setExecutor(executor)

      const task = manager.createTask(makeUserMessage('Test'))
      const events: A2AEvent[] = []

      for await (const event of manager.executeTask(task.id)) {
        events.push(event)
      }

      // Should include the custom event plus working/completed
      expect(events.length).toBeGreaterThanOrEqual(3)
    })

    it('should throw for unknown task', async () => {
      manager.setExecutor(createMockExecutor())

      const gen = manager.executeTask('nonexistent')
      await expect(gen.next()).rejects.toThrow('Task not found')
    })

    it('should throw if no executor set', async () => {
      const task = manager.createTask(makeUserMessage('Test'))

      const gen = manager.executeTask(task.id)
      await expect(gen.next()).rejects.toThrow('No task executor')
    })

    it('should handle executor failure', async () => {
      const executor = createFailingExecutor('Something broke')
      manager.setExecutor(executor)

      const task = manager.createTask(makeUserMessage('Test'))
      const events: A2AEvent[] = []

      for await (const event of manager.executeTask(task.id)) {
        events.push(event)
      }

      const finalTask = manager.getTask(task.id)!
      expect(finalTask.status.state).toBe('failed')
    })

    it('should pass goal from user messages to executor', async () => {
      const executor = createMockExecutor()
      manager.setExecutor(executor)

      const task = manager.createTask(makeUserMessage('Build a REST API'))
      // eslint-disable-next-line @typescript-eslint/no-unused-vars
      for await (const _ of manager.executeTask(task.id)) {
        // Drain
      }

      expect(executor.execute).toHaveBeenCalledWith(
        'Build a REST API',
        expect.any(String),
        expect.any(AbortSignal),
        expect.any(Function)
      )
    })

    it('should track active executions', async () => {
      const executor = createBlockingExecutor()
      manager.setExecutor(executor)

      const task = manager.createTask(makeUserMessage('Test'))
      expect(manager.isExecuting(task.id)).toBe(false)

      // Start execution without consuming (will block)
      const gen = manager.executeTask(task.id)
      const first = await gen.next()
      expect(first.done).toBe(false)
      expect(manager.isExecuting(task.id)).toBe(true)
      expect(manager.getActiveCount()).toBe(1)

      // Cancel to unblock
      manager.cancelTask(task.id)

      // Drain remaining events
      // eslint-disable-next-line @typescript-eslint/no-unused-vars
      for await (const _ of { [Symbol.asyncIterator]: () => gen }) {
        // Drain
      }
    })
  })

  describe('Part utilities', () => {
    it('textPart creates text', () => {
      const part = textPart('Hello')
      expect(part.type).toBe('text')
      expect(part.text).toBe('Hello')
    })

    it('dataPart creates data', () => {
      const part = dataPart({ key: 'value' })
      expect(part.type).toBe('data')
      expect(part.data).toEqual({ key: 'value' })
    })

    it('userMessage creates user message', () => {
      const msg = userMessage('Hello')
      expect(msg.role).toBe('user')
      expect(msg.parts).toHaveLength(1)
      expect(msg.parts[0]!.type).toBe('text')
    })

    it('agentMessage creates agent message', () => {
      const msg = agentMessage('Response')
      expect(msg.role).toBe('agent')
      expect(msg.parts).toHaveLength(1)
    })
  })

  describe('Event constructors', () => {
    it('createStatusEvent', () => {
      const task = manager.createTask(makeUserMessage('Test'))
      const event = createStatusEvent(task, false)

      expect(event.kind).toBe('status-update')
      expect(event.taskId).toBe(task.id)
      expect(event.contextId).toBe(task.contextId)
      expect(event.final).toBe(false)
      expect(event.status.state).toBe('submitted')
    })

    it('createArtifactEvent', () => {
      const task = manager.createTask(makeUserMessage('Test'))
      const artifact: Artifact = {
        artifactId: 'art-1',
        parts: [{ type: 'text', text: 'data' }],
      }

      const event = createArtifactEvent(task, artifact, false, true)

      expect(event.kind).toBe('artifact-update')
      expect(event.taskId).toBe(task.id)
      expect(event.artifact.artifactId).toBe('art-1')
      expect(event.append).toBe(false)
      expect(event.lastChunk).toBe(true)
    })
  })
})
