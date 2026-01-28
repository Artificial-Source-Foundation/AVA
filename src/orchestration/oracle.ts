/**
 * Delta9 Oracle Invocation
 *
 * Invokes individual oracles for council deliberation.
 * Each oracle provides an opinion with confidence and recommendations.
 */

import type { OracleConfig } from '../types/config.js'
import type { OracleOpinion } from '../types/mission.js'
import type { OpenCodeClient } from '../lib/background-manager.js'
import { parseModelId } from '../lib/models.js'
import { getNamedLogger } from '../lib/logger.js'

const logger = getNamedLogger('oracle')

// =============================================================================
// Types
// =============================================================================

export interface OraclePromptContext {
  /** Question or topic for the oracle */
  question: string
  /** Mission description for context */
  missionDescription: string
  /** Current objectives summary */
  objectivesSummary: string
  /** Any previous oracle opinions (for building consensus) */
  previousOpinions?: OracleOpinion[]
  /** Additional context */
  additionalContext?: string
}

export interface OracleInvocationResult {
  /** Oracle configuration */
  oracle: OracleConfig
  /** Oracle's opinion */
  opinion: OracleOpinion
  /** Tokens used */
  tokensUsed: number
  /** Time taken in ms */
  durationMs: number
  /** Session ID if using SDK */
  sessionId?: string
  /** Whether the oracle invocation failed (A-7: Graceful Degradation) */
  failed?: boolean
  /** Failure reason if failed */
  failureReason?: 'timeout' | 'rate_limit' | 'auth' | 'simulation' | 'error'
  /** Whether this was a degraded/fallback response */
  degraded?: boolean
}

export interface OracleInvocationOptions {
  /** OpenCode client for real invocation (optional - falls back to simulation) */
  client?: OpenCodeClient
  /** Timeout in milliseconds (default: 60000) */
  timeoutMs?: number
  /** Working directory for session */
  cwd?: string
}

// Default timeout for oracle invocation
const DEFAULT_TIMEOUT_MS = 60_000

// =============================================================================
// Oracle Prompt Building
// =============================================================================

/**
 * Build system prompt for an oracle based on its specialty
 */
export function buildOracleSystemPrompt(oracle: OracleConfig): string {
  const specialtyPrompts: Record<string, string> = {
    architecture: `You are an expert software architect oracle named "${oracle.name}".
Your role is to analyze problems from an architectural perspective, considering:
- System design and scalability
- Component interactions and dependencies
- Long-term maintainability
- Design patterns and best practices

You MUST respond with valid JSON in this exact format:
{
  "recommendation": "Your detailed architectural recommendation",
  "confidence": 0.0 to 1.0,
  "caveats": ["Any concerns or limitations"],
  "suggestedTasks": ["Specific actionable tasks"]
}`,

    logic: `You are an expert logic and reasoning oracle named "${oracle.name}".
Your role is to analyze problems from a logical perspective, considering:
- Correctness and edge cases
- Algorithm efficiency
- Error handling
- Data flow and state management

You MUST respond with valid JSON in this exact format:
{
  "recommendation": "Your detailed recommendation on logic and patterns",
  "confidence": 0.0 to 1.0,
  "caveats": ["Any concerns or limitations"],
  "suggestedTasks": ["Specific actionable tasks"]
}`,

    ui: `You are an expert UI/UX oracle named "${oracle.name}".
Your role is to analyze problems from a user interface perspective, considering:
- User experience and usability
- Accessibility (WCAG compliance)
- Responsive design
- Component reusability
- Visual consistency

You MUST respond with valid JSON in this exact format:
{
  "recommendation": "Your detailed UX recommendation",
  "confidence": 0.0 to 1.0,
  "caveats": ["Any concerns or limitations"],
  "suggestedTasks": ["Specific actionable tasks"]
}`,

    performance: `You are an expert performance oracle named "${oracle.name}".
Your role is to analyze problems from a performance perspective, considering:
- Time and space complexity
- Memory usage and optimization
- Caching strategies
- Bottleneck identification
- Load and scalability

You MUST respond with valid JSON in this exact format:
{
  "recommendation": "Your detailed performance recommendation",
  "confidence": 0.0 to 1.0,
  "caveats": ["Any concerns or limitations"],
  "suggestedTasks": ["Specific actionable tasks"]
}`,

    general: `You are a general-purpose expert oracle named "${oracle.name}".
Your role is to analyze problems holistically, considering:
- Overall solution quality
- Trade-offs and alternatives
- Implementation complexity
- Testing and validation

You MUST respond with valid JSON in this exact format:
{
  "recommendation": "Your detailed recommendation",
  "confidence": 0.0 to 1.0,
  "caveats": ["Any concerns or limitations"],
  "suggestedTasks": ["Specific actionable tasks"]
}`,
  }

  return specialtyPrompts[oracle.specialty] || specialtyPrompts.general
}

/**
 * Build the user prompt for oracle consultation
 */
export function buildOracleUserPrompt(context: OraclePromptContext): string {
  let prompt = `## Mission Context
${context.missionDescription}

## Current Objectives
${context.objectivesSummary}

## Question for Consultation
${context.question}
`

  if (context.additionalContext) {
    prompt += `\n## Additional Context\n${context.additionalContext}\n`
  }

  if (context.previousOpinions && context.previousOpinions.length > 0) {
    prompt += `\n## Previous Oracle Opinions\n`
    for (const opinion of context.previousOpinions) {
      prompt += `### ${opinion.oracle} (Confidence: ${(opinion.confidence * 100).toFixed(0)}%)
${opinion.recommendation}
${opinion.caveats ? `Caveats: ${opinion.caveats.join(', ')}` : ''}

`
    }
    prompt += `Consider these perspectives in your analysis.\n`
  }

  prompt += `
Respond with JSON only. No additional text.`

  return prompt
}

// =============================================================================
// Oracle Response Parsing
// =============================================================================

/**
 * Parse oracle response into OracleOpinion
 */
export function parseOracleResponse(oracle: OracleConfig, response: string): OracleOpinion {
  try {
    // Try to extract JSON from response
    const jsonMatch = response.match(/\{[\s\S]*\}/)
    if (!jsonMatch) {
      return {
        oracle: oracle.name,
        recommendation: response,
        confidence: 0.5,
        caveats: ['Response was not in expected JSON format'],
      }
    }

    const parsed = JSON.parse(jsonMatch[0]) as {
      recommendation?: string
      confidence?: number
      caveats?: string[]
      suggestedTasks?: string[]
    }

    return {
      oracle: oracle.name,
      recommendation: parsed.recommendation || response,
      confidence: Math.min(1, Math.max(0, parsed.confidence || 0.5)),
      caveats: parsed.caveats,
      suggestedTasks: parsed.suggestedTasks,
    }
  } catch {
    // If JSON parsing fails, treat the whole response as the recommendation
    return {
      oracle: oracle.name,
      recommendation: response,
      confidence: 0.5,
      caveats: ['Response parsing had issues'],
    }
  }
}

// =============================================================================
// Oracle Invocation
// =============================================================================

/**
 * Invoke an oracle using OpenCode SDK
 *
 * If client is provided, creates a sub-session and invokes the model.
 * Otherwise, falls back to simulation mode.
 */
export async function invokeOracle(
  oracle: OracleConfig,
  context: OraclePromptContext,
  options: OracleInvocationOptions = {}
): Promise<OracleInvocationResult> {
  const { client, timeoutMs = DEFAULT_TIMEOUT_MS, cwd } = options
  const startTime = Date.now()

  // Build prompts
  const systemPrompt = buildOracleSystemPrompt(oracle)
  const userPrompt = buildOracleUserPrompt(context)

  // Parse model ID
  const { provider, model } = parseModelId(oracle.model)

  logger.debug(`Invoking oracle ${oracle.name}`, {
    model: oracle.model,
    specialty: oracle.specialty,
    hasClient: !!client,
  })

  // If no client, use simulation mode
  if (!client) {
    return invokeOracleSimulation(oracle, context, startTime)
  }

  // Real SDK invocation
  try {
    // Create a sub-session for this oracle
    const createResult = await client.session.create({
      body: {
        title: `Delta9 Oracle: ${oracle.name}`,
      },
      query: cwd ? { directory: cwd } : undefined,
    })

    if (createResult.error || !createResult.data?.id) {
      logger.warn(`Failed to create session for oracle ${oracle.name}`, {
        error: createResult.error,
      })
      return invokeOracleSimulation(oracle, context, startTime)
    }

    const sessionId = createResult.data.id
    logger.debug(`Created session for oracle ${oracle.name}`, { sessionId })

    // Send prompt to oracle
    const promptPromise = client.session.prompt({
      path: { id: sessionId },
      body: {
        model: { providerID: provider, modelID: model },
        system: systemPrompt,
        parts: [{ type: 'text', text: userPrompt }],
      },
    })

    // Wait for response with timeout
    const response = await Promise.race([
      waitForOracleResponse(client, sessionId, oracle.name),
      timeoutPromise(timeoutMs, oracle.name),
    ])

    // Fire-and-forget the prompt (we poll for completion)
    void promptPromise

    const opinion = parseOracleResponse(oracle, response)
    const durationMs = Date.now() - startTime

    logger.info(`Oracle ${oracle.name} responded`, {
      confidence: opinion.confidence,
      durationMs,
    })

    return {
      oracle,
      opinion,
      tokensUsed: 0, // Token tracking not available via session polling (SDK limitation)
      durationMs,
      sessionId,
    }
  } catch (error) {
    const errorMessage = error instanceof Error ? error.message : String(error)
    const isTimeout = errorMessage.includes('timeout')
    const isRateLimit = errorMessage.includes('rate') || errorMessage.includes('429')
    const isAuth =
      errorMessage.includes('auth') || errorMessage.includes('401') || errorMessage.includes('403')

    logger.error(`Oracle ${oracle.name} invocation failed`, {
      error: errorMessage,
      errorType: isTimeout ? 'timeout' : isRateLimit ? 'rate_limit' : isAuth ? 'auth' : 'unknown',
      model: oracle.model,
      specialty: oracle.specialty,
    })

    // Build actionable error context
    const troubleshooting: string[] = []
    if (isTimeout) {
      troubleshooting.push('Consider using quick mode for simpler queries')
      troubleshooting.push('Try reducing the complexity of the question')
      troubleshooting.push(
        `Current timeout: ${timeoutMs}ms - may need increase for complex queries`
      )
    } else if (isRateLimit) {
      troubleshooting.push('Rate limit hit - the model provider is throttling requests')
      troubleshooting.push('Consider using a fallback model from a different provider')
      troubleshooting.push('Wait and retry, or use sequential council mode to reduce concurrency')
    } else if (isAuth) {
      troubleshooting.push('Authentication failed - check API keys for the provider')
      troubleshooting.push(`Model: ${oracle.model} - verify provider credentials`)
    } else {
      troubleshooting.push('Check network connectivity and provider status')
      troubleshooting.push('Verify the model is available and correctly configured')
    }

    // A-7: Graceful degradation - fall back to error opinion with actionable information
    const failureReason = isTimeout
      ? 'timeout'
      : isRateLimit
        ? 'rate_limit'
        : isAuth
          ? 'auth'
          : 'error'

    return {
      oracle,
      opinion: {
        oracle: oracle.name,
        recommendation: `Oracle ${oracle.name} (${oracle.specialty}) invocation failed: ${errorMessage}

**What happened:** The oracle could not be consulted due to ${isTimeout ? 'a timeout' : isRateLimit ? 'rate limiting' : isAuth ? 'authentication failure' : 'an error'}.

**Troubleshooting:**
${troubleshooting.map((t) => `- ${t}`).join('\n')}

**Fallback:** Proceeding without this oracle's input. Consider re-running the council after addressing the issue.`,
        confidence: 0,
        caveats: [`Oracle invocation failed: ${failureReason}`, ...troubleshooting],
      },
      tokensUsed: 0,
      durationMs: Date.now() - startTime,
      failed: true,
      failureReason: failureReason as 'timeout' | 'rate_limit' | 'auth' | 'error',
      degraded: true,
    }
  }
}

/**
 * Wait for oracle response by polling session messages
 */
async function waitForOracleResponse(
  client: OpenCodeClient,
  sessionId: string,
  oracleName: string
): Promise<string> {
  const pollIntervalMs = 1000
  const maxPolls = 60 // Max 60 seconds

  for (let i = 0; i < maxPolls; i++) {
    await sleep(pollIntervalMs)

    try {
      // Check session status
      const statusResult = await client.session.status()
      const sessionStatus = statusResult.data?.[sessionId]

      // Get messages
      const messagesResult = await client.session.messages({
        path: { id: sessionId },
      })

      const messages = messagesResult.data || []

      // Find the last assistant message
      const assistantMessages = messages.filter((m) => m.info?.role === 'assistant')

      if (assistantMessages.length > 0) {
        const lastMessage = assistantMessages[assistantMessages.length - 1]
        const textParts = lastMessage.parts?.filter((p) => p.type === 'text')
        if (textParts && textParts.length > 0) {
          const text = textParts.map((p) => p.text || '').join('\n')
          if (text.includes('{') && text.includes('}')) {
            // Looks like we have a JSON response
            return text
          }
        }
      }

      // Check if session is no longer running
      if (sessionStatus && sessionStatus.type !== 'running') {
        // Session finished, return whatever we have
        const lastAssistant = assistantMessages[assistantMessages.length - 1]
        if (lastAssistant) {
          return lastAssistant.parts?.map((p) => p.text || '').join('\n') || ''
        }
        break
      }
    } catch (error) {
      logger.warn(`Error polling oracle ${oracleName}`, {
        error: error instanceof Error ? error.message : String(error),
      })
    }
  }

  throw new Error(`Oracle ${oracleName} response timeout`)
}

/**
 * Simulation mode for when SDK is not available
 *
 * Returns a placeholder response with clear indication that real
 * invocation was not possible and guidance on how to enable it.
 */
function invokeOracleSimulation(
  oracle: OracleConfig,
  context: OraclePromptContext,
  startTime: number
): OracleInvocationResult {
  const { provider, model } = parseModelId(oracle.model)

  logger.warn(`Oracle ${oracle.name} using simulation mode`, {
    reason: 'No OpenCode client provided',
    model: oracle.model,
    hint: 'Pass client to conveneCouncil for real oracle invocation',
  })

  const placeholderResponse = JSON.stringify({
    recommendation: `**⚠️ SIMULATION MODE - ${oracle.name} (${oracle.specialty})**

This is a simulated response. The oracle was not actually invoked because no OpenCode client was provided.

**Question received:** ${context.question.substring(0, 150)}${context.question.length > 150 ? '...' : ''}

**What would happen with real invocation:**
- Oracle ${oracle.name} would analyze this from a ${oracle.specialty} perspective
- Model: ${model} via ${provider}
- Response would include detailed recommendations and confidence score

**To enable real oracle invocation:**
1. Ensure the \`client\` parameter is passed through the tool chain
2. Verify: createCouncilTools(state, cwd, client) → conveneCouncil → invokeOracle
3. Check that OpenCode is running and accessible

**Generic ${oracle.specialty} guidance (placeholder):**
- Consider the ${oracle.specialty} implications of the proposed approach
- Evaluate trade-offs between complexity and maintainability
- Follow established patterns and best practices`,
    confidence: 0.3, // Lower confidence since this is simulated
    caveats: [
      '⚠️ SIMULATION MODE - real oracle not invoked',
      `Model ${model} via ${provider} was NOT called`,
      'Pass OpenCode client to enable real oracle consultation',
    ],
    suggestedTasks: [
      'Verify OpenCode client is passed to council tools',
      `Manually review ${oracle.specialty} considerations until real invocation is fixed`,
    ],
  })

  const opinion = parseOracleResponse(oracle, placeholderResponse)

  // A-7: Mark simulation mode as degraded
  return {
    oracle,
    opinion,
    tokensUsed: 0,
    durationMs: Date.now() - startTime,
    failed: false, // Not a failure, just degraded
    failureReason: 'simulation',
    degraded: true,
  }
}

/**
 * Create a timeout promise
 */
function timeoutPromise(ms: number, oracleName: string): Promise<never> {
  return new Promise((_, reject) => {
    setTimeout(() => {
      reject(new Error(`Oracle ${oracleName} timed out after ${ms}ms`))
    }, ms)
  })
}

/**
 * Sleep for a given number of milliseconds
 */
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

// =============================================================================
// Parallel/Sequential Invocation
// =============================================================================

/**
 * Invoke multiple oracles in parallel
 */
export async function invokeOraclesParallel(
  oracles: OracleConfig[],
  context: OraclePromptContext,
  options: OracleInvocationOptions = {}
): Promise<OracleInvocationResult[]> {
  const results = await Promise.all(oracles.map((oracle) => invokeOracle(oracle, context, options)))
  return results
}

/**
 * Invoke multiple oracles sequentially
 *
 * Builds on previous opinions for consensus building.
 */
export async function invokeOraclesSequential(
  oracles: OracleConfig[],
  context: OraclePromptContext,
  options: OracleInvocationOptions = {}
): Promise<OracleInvocationResult[]> {
  const results: OracleInvocationResult[] = []
  const opinions: OracleOpinion[] = []

  for (const oracle of oracles) {
    const result = await invokeOracle(
      oracle,
      {
        ...context,
        previousOpinions: opinions,
      },
      options
    )
    results.push(result)
    // Only include non-failed opinions for consensus building
    if (!result.failed) {
      opinions.push(result.opinion)
    }
  }

  return results
}

// =============================================================================
// Graceful Degradation Helpers (A-7)
// =============================================================================

/**
 * Filter oracle results to only include successful invocations
 */
export function filterSuccessfulResults(
  results: OracleInvocationResult[]
): OracleInvocationResult[] {
  return results.filter((r) => !r.failed)
}

/**
 * Filter oracle results to only include degraded/failed invocations
 */
export function filterFailedResults(results: OracleInvocationResult[]): OracleInvocationResult[] {
  return results.filter((r) => r.failed || r.degraded)
}

/**
 * Get degradation summary for council results
 */
export function getDegradationSummary(results: OracleInvocationResult[]): {
  total: number
  successful: number
  degraded: number
  failed: number
  failureReasons: Record<string, number>
} {
  const summary = {
    total: results.length,
    successful: 0,
    degraded: 0,
    failed: 0,
    failureReasons: {} as Record<string, number>,
  }

  for (const result of results) {
    if (result.failed) {
      summary.failed++
      if (result.failureReason) {
        summary.failureReasons[result.failureReason] =
          (summary.failureReasons[result.failureReason] || 0) + 1
      }
    } else if (result.degraded) {
      summary.degraded++
      if (result.failureReason) {
        summary.failureReasons[result.failureReason] =
          (summary.failureReasons[result.failureReason] || 0) + 1
      }
    } else {
      summary.successful++
    }
  }

  return summary
}

/**
 * Check if enough oracles responded successfully for meaningful consensus
 */
export function hasMinimumQuorum(
  results: OracleInvocationResult[],
  minQuorum: number = 1
): boolean {
  const successful = filterSuccessfulResults(results)
  return successful.length >= minQuorum
}
