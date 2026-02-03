/**
 * Validator Module
 * QA verification gate for agent outputs
 *
 * Provides validation pipeline with built-in validators:
 * - Syntax (parse checking)
 * - TypeScript (type checking)
 * - Lint (style checking)
 * - Test (test running)
 * - Build (compilation)
 * - Self-Review (LLM review)
 */

export { buildValidator } from './build.js'
export { lintValidator } from './lint.js'
// Pipeline
export {
  createFailedResult,
  createPassedResult,
  mergeResults,
  SimpleValidatorRegistry,
  ValidationPipeline,
} from './pipeline.js'
export { selfReviewValidator } from './self-review.js'
// Built-in Validators
export { syntaxValidator } from './syntax.js'
export { testValidator } from './test.js'
// Types
export * from './types.js'
export { typescriptValidator } from './typescript.js'
