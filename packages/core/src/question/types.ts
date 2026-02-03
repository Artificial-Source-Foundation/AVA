/**
 * Question Types
 * Types for LLM-to-user question system
 */

// ============================================================================
// Question Types
// ============================================================================

/**
 * A question the LLM wants to ask the user
 */
export interface Question {
  /** Unique question identifier */
  id: string
  /** The question text */
  text: string
  /** Optional predefined answer choices */
  options?: string[]
  /** Whether an answer is required (default: true) */
  required?: boolean
  /** Optional header/category for the question */
  header?: string
  /** Allow multiple selections (for options) */
  multiSelect?: boolean
}

/**
 * Result of a question being answered
 */
export interface QuestionResult {
  /** The question ID */
  questionId: string
  /** The user's answer */
  answer: string
  /** Multiple answers if multiSelect */
  answers?: string[]
  /** When the question was answered */
  answeredAt: number
}

/**
 * A pending question waiting for user response
 */
export interface PendingQuestion {
  /** The question being asked */
  question: Question
  /** When the question was asked */
  askedAt: number
  /** Promise resolver for when answered */
  resolve: (result: QuestionResult) => void
  /** Promise rejecter for timeout/cancel */
  reject: (error: Error) => void
}

// ============================================================================
// Events
// ============================================================================

/**
 * Events emitted by the question system
 */
export type QuestionEvent =
  | { type: 'question_asked'; question: Question }
  | { type: 'question_answered'; questionId: string; answer: string }
  | { type: 'question_cancelled'; questionId: string }
  | { type: 'question_timeout'; questionId: string }

/**
 * Listener for question events
 */
export type QuestionEventListener = (event: QuestionEvent) => void

// ============================================================================
// Manager Config
// ============================================================================

/**
 * Configuration for QuestionManager
 */
export interface QuestionManagerConfig {
  /** Timeout for questions in milliseconds (default: 5 minutes) */
  timeout?: number
  /** Event listener for question events */
  onEvent?: QuestionEventListener
}
