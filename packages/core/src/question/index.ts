/**
 * @estela/core Question Module
 * LLM-to-user question system
 */

// Manager
export {
  clearQuestionManager,
  createQuestionManager,
  getQuestionManager,
  QuestionManager,
  setQuestionManager,
} from './manager.js'
// Types
export type {
  PendingQuestion,
  Question,
  QuestionEvent,
  QuestionEventListener,
  QuestionManagerConfig,
  QuestionResult,
} from './types.js'
