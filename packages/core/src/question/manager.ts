/**
 * Question Manager
 * Manages LLM-to-user questions with async/await pattern
 */

import type {
  PendingQuestion,
  Question,
  QuestionEventListener,
  QuestionManagerConfig,
  QuestionResult,
} from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default timeout for questions (5 minutes) */
const DEFAULT_TIMEOUT = 5 * 60 * 1000

// ============================================================================
// Question Manager
// ============================================================================

/**
 * Manages pending questions and their answers
 * Provides async/await interface for tool-based question flow
 */
export class QuestionManager {
  /** Pending questions waiting for answers */
  private pending = new Map<string, PendingQuestion>()

  /** Timeout timers for auto-cancellation */
  private timeouts = new Map<string, NodeJS.Timeout>()

  /** Event listener */
  private onEvent?: QuestionEventListener

  /** Default timeout */
  private timeout: number

  constructor(config: QuestionManagerConfig = {}) {
    this.timeout = config.timeout ?? DEFAULT_TIMEOUT
    this.onEvent = config.onEvent
  }

  // ==========================================================================
  // Public API
  // ==========================================================================

  /**
   * Ask a question and wait for the answer
   * This blocks until the user answers or the question times out
   */
  async ask(question: Question): Promise<QuestionResult> {
    // Create promise that will resolve when answered
    return new Promise<QuestionResult>((resolve, reject) => {
      const pending: PendingQuestion = {
        question,
        askedAt: Date.now(),
        resolve,
        reject,
      }

      // Store pending question
      this.pending.set(question.id, pending)

      // Emit question_asked event
      this.emit({ type: 'question_asked', question })

      // Set timeout for auto-cancellation
      const timer = setTimeout(() => {
        this.handleTimeout(question.id)
      }, this.timeout)

      this.timeouts.set(question.id, timer)
    })
  }

  /**
   * Answer a pending question
   * Called by UI when user provides an answer
   */
  answer(questionId: string, answer: string, answers?: string[]): boolean {
    const pending = this.pending.get(questionId)
    if (!pending) {
      return false
    }

    // Clear timeout
    this.clearTimeout(questionId)

    // Create result
    const result: QuestionResult = {
      questionId,
      answer,
      answers,
      answeredAt: Date.now(),
    }

    // Resolve the promise
    pending.resolve(result)

    // Clean up
    this.pending.delete(questionId)

    // Emit event
    this.emit({ type: 'question_answered', questionId, answer })

    return true
  }

  /**
   * Cancel a pending question
   * Called by UI when user dismisses without answering
   */
  cancel(questionId: string): boolean {
    const pending = this.pending.get(questionId)
    if (!pending) {
      return false
    }

    // Clear timeout
    this.clearTimeout(questionId)

    // Reject the promise
    pending.reject(new Error('Question was cancelled by user'))

    // Clean up
    this.pending.delete(questionId)

    // Emit event
    this.emit({ type: 'question_cancelled', questionId })

    return true
  }

  /**
   * Get all pending questions
   */
  getPending(): Question[] {
    return Array.from(this.pending.values()).map((p) => p.question)
  }

  /**
   * Check if a question is pending
   */
  isPending(questionId: string): boolean {
    return this.pending.has(questionId)
  }

  /**
   * Get count of pending questions
   */
  get pendingCount(): number {
    return this.pending.size
  }

  /**
   * Clear all pending questions
   */
  clear(): void {
    for (const [id, pending] of this.pending) {
      this.clearTimeout(id)
      pending.reject(new Error('Question manager was cleared'))
    }
    this.pending.clear()
  }

  /**
   * Destroy the manager (cleanup)
   */
  destroy(): void {
    this.clear()
  }

  // ==========================================================================
  // Private Helpers
  // ==========================================================================

  private handleTimeout(questionId: string): void {
    const pending = this.pending.get(questionId)
    if (!pending) {
      return
    }

    // Reject with timeout error
    pending.reject(new Error('Question timed out waiting for answer'))

    // Clean up
    this.pending.delete(questionId)
    this.timeouts.delete(questionId)

    // Emit event
    this.emit({ type: 'question_timeout', questionId })
  }

  private clearTimeout(questionId: string): void {
    const timer = this.timeouts.get(questionId)
    if (timer) {
      clearTimeout(timer)
      this.timeouts.delete(questionId)
    }
  }

  private emit(event: Parameters<QuestionEventListener>[0]): void {
    this.onEvent?.(event)
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a new QuestionManager instance
 */
export function createQuestionManager(config?: QuestionManagerConfig): QuestionManager {
  return new QuestionManager(config)
}

// ============================================================================
// Singleton (for tool access)
// ============================================================================

let globalManager: QuestionManager | undefined

/**
 * Get or create the global question manager
 * Used by the question tool for simple integration
 */
export function getQuestionManager(): QuestionManager {
  if (!globalManager) {
    globalManager = new QuestionManager()
  }
  return globalManager
}

/**
 * Set the global question manager
 * Allows custom configuration
 */
export function setQuestionManager(manager: QuestionManager): void {
  globalManager = manager
}

/**
 * Clear the global question manager
 */
export function clearQuestionManager(): void {
  globalManager?.destroy()
  globalManager = undefined
}
