/**
 * Question Tool
 * Allows LLM to ask clarifying questions to the user
 *
 * Based on OpenCode's question.ts pattern
 */

import { getQuestionManager } from '../question/manager.js'
import type { Question } from '../question/types.js'
import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'

// ============================================================================
// Types
// ============================================================================

interface QuestionItem {
  /** The question text */
  text: string
  /** Optional header/category */
  header?: string
  /** Optional predefined answer choices */
  options?: string[]
  /** Allow multiple selections */
  multiSelect?: boolean
}

interface QuestionParams {
  /** Questions to ask (1-4 questions) */
  questions: QuestionItem[]
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Generate a unique question ID
 */
function generateQuestionId(): string {
  return `q-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
}

/**
 * Format a question result for LLM output
 */
function formatAnswer(question: Question, answer: string, answers?: string[]): string {
  const header = question.header ? `[${question.header}] ` : ''
  const answerText = answers && answers.length > 1 ? answers.join(', ') : answer

  return `${header}Q: ${question.text}\nA: ${answerText}`
}

// ============================================================================
// Tool Implementation
// ============================================================================

export const questionTool: Tool<QuestionParams> = {
  definition: {
    name: 'question',
    description: `Ask the user clarifying questions during execution.

Use this tool when you need to:
- Gather user preferences or requirements
- Clarify ambiguous instructions
- Get decisions on implementation choices
- Offer choices about direction to take

Guidelines:
- Keep questions clear and concise
- Use options when there are predefined choices
- Use multiSelect when multiple answers make sense
- Ask 1-4 questions at a time
- Don't ask obvious questions you can figure out`,
    input_schema: {
      type: 'object',
      properties: {
        questions: {
          type: 'array',
          description: 'Questions to ask (1-4 questions)',
          minItems: 1,
          maxItems: 4,
          items: {
            type: 'object',
            properties: {
              text: {
                type: 'string',
                description: 'The question to ask',
              },
              header: {
                type: 'string',
                description: 'Short category label (e.g., "Auth method", "Library")',
              },
              options: {
                type: 'array',
                items: { type: 'string' },
                description: 'Predefined answer choices (optional)',
              },
              multiSelect: {
                type: 'boolean',
                description: 'Allow multiple selections (default: false)',
              },
            },
            required: ['text'],
          },
        },
      },
      required: ['questions'],
    },
  },

  validate(params: unknown): QuestionParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'question'
      )
    }

    const { questions } = params as Record<string, unknown>

    if (!Array.isArray(questions)) {
      throw new ToolError(
        'Invalid questions: must be array',
        ToolErrorType.INVALID_PARAMS,
        'question'
      )
    }

    if (questions.length === 0) {
      throw new ToolError(
        'Invalid questions: must have at least one question',
        ToolErrorType.INVALID_PARAMS,
        'question'
      )
    }

    if (questions.length > 4) {
      throw new ToolError(
        'Invalid questions: maximum 4 questions at a time',
        ToolErrorType.INVALID_PARAMS,
        'question'
      )
    }

    // Validate each question
    const validatedQuestions: QuestionItem[] = []
    for (let i = 0; i < questions.length; i++) {
      const q = questions[i]
      if (typeof q !== 'object' || q === null) {
        throw new ToolError(
          `Invalid question at index ${i}: must be object`,
          ToolErrorType.INVALID_PARAMS,
          'question'
        )
      }

      const { text, header, options, multiSelect } = q as Record<string, unknown>

      if (typeof text !== 'string' || !text.trim()) {
        throw new ToolError(
          `Invalid question at index ${i}: text is required`,
          ToolErrorType.INVALID_PARAMS,
          'question'
        )
      }

      if (header !== undefined && typeof header !== 'string') {
        throw new ToolError(
          `Invalid question at index ${i}: header must be string`,
          ToolErrorType.INVALID_PARAMS,
          'question'
        )
      }

      if (options !== undefined) {
        if (!Array.isArray(options)) {
          throw new ToolError(
            `Invalid question at index ${i}: options must be array`,
            ToolErrorType.INVALID_PARAMS,
            'question'
          )
        }
        if (!options.every((o) => typeof o === 'string')) {
          throw new ToolError(
            `Invalid question at index ${i}: all options must be strings`,
            ToolErrorType.INVALID_PARAMS,
            'question'
          )
        }
      }

      if (multiSelect !== undefined && typeof multiSelect !== 'boolean') {
        throw new ToolError(
          `Invalid question at index ${i}: multiSelect must be boolean`,
          ToolErrorType.INVALID_PARAMS,
          'question'
        )
      }

      validatedQuestions.push({
        text: text.trim(),
        header: typeof header === 'string' ? header.trim() : undefined,
        options: options as string[] | undefined,
        multiSelect: multiSelect as boolean | undefined,
      })
    }

    return { questions: validatedQuestions }
  },

  async execute(params: QuestionParams, ctx: ToolContext): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    const manager = getQuestionManager()
    const results: Array<{ question: Question; answer: string; answers?: string[] }> = []

    try {
      // Ask each question sequentially
      for (const q of params.questions) {
        if (ctx.signal.aborted) {
          return {
            success: false,
            output: 'Operation was cancelled',
            error: ToolErrorType.EXECUTION_ABORTED,
          }
        }

        const question: Question = {
          id: generateQuestionId(),
          text: q.text,
          header: q.header,
          options: q.options,
          multiSelect: q.multiSelect,
          required: true,
        }

        // Stream metadata to UI
        if (ctx.metadata) {
          ctx.metadata({
            title: `Asking: ${q.header ?? q.text.slice(0, 30)}...`,
            metadata: {
              question,
              pending: true,
            },
          })
        }

        // Wait for answer (blocks until user responds)
        const result = await manager.ask(question)
        results.push({
          question,
          answer: result.answer,
          answers: result.answers,
        })

        // Stream answer back
        if (ctx.metadata) {
          ctx.metadata({
            title: `Answered: ${q.header ?? q.text.slice(0, 30)}...`,
            metadata: {
              question,
              answer: result.answer,
              answers: result.answers,
              pending: false,
            },
          })
        }
      }

      // Format output for LLM
      const outputLines = results.map((r) => formatAnswer(r.question, r.answer, r.answers))
      const output = `User responses:\n\n${outputLines.join('\n\n')}`

      return {
        success: true,
        output,
        metadata: {
          questionCount: params.questions.length,
          answers: results.map((r) => ({
            questionId: r.question.id,
            header: r.question.header,
            answer: r.answer,
            answers: r.answers,
          })),
        },
      }
    } catch (err) {
      // Handle timeout or cancellation
      const message = err instanceof Error ? err.message : String(err)

      if (message.includes('timed out')) {
        return {
          success: false,
          output: `Question timed out waiting for user response`,
          error: ToolErrorType.EXECUTION_TIMEOUT,
        }
      }

      if (message.includes('cancelled')) {
        return {
          success: false,
          output: `User cancelled the question`,
          error: ToolErrorType.EXECUTION_ABORTED,
        }
      }

      return {
        success: false,
        output: `Error asking question: ${message}`,
        error: ToolErrorType.UNKNOWN,
      }
    }
  },
}
