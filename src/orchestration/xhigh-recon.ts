/**
 * Delta9 XHIGH Mode Reconnaissance
 *
 * Before XHIGH council deliberation, oracles get access to:
 * 1. RECON reconnaissance - codebase search results
 * 2. SIGINT research - documentation and library references
 *
 * This provides oracles with evidence-based context.
 */

import { getNamedLogger } from '../lib/logger.js'
import type { OpenCodeClient } from '../lib/background-manager.js'
import { createReconAgent, createSigintAgent } from '../agents/support/index.js'
import { parseModelId } from '../lib/models.js'

const logger = getNamedLogger('xhigh-recon')

// =============================================================================
// Types
// =============================================================================

export interface CodeSnippet {
  file: string
  line: number
  content: string
  context?: string
}

export interface ReconResult {
  /** Whether relevant files were found */
  found: boolean
  /** List of relevant files */
  files: string[]
  /** Code snippets */
  snippets: CodeSnippet[]
  /** Summary of findings */
  summary: string
  /** Suggestions for further investigation */
  suggestions: string[]
}

export interface SigintResult {
  /** Classification of the research request */
  classification: 'CONCEPTUAL' | 'IMPLEMENTATION' | 'CONTEXT' | 'COMPREHENSIVE'
  /** Synthesized answer */
  answer: string
  /** Evidence with citations */
  evidence: Array<{
    source: string
    url?: string
    quote: string
    relevance: string
  }>
  /** Confidence level */
  confidence: 'HIGH' | 'MEDIUM' | 'LOW'
  /** Why this confidence */
  confidenceReason: string
}

export interface XhighReconResult {
  /** RECON reconnaissance results */
  recon: ReconResult | null
  /** SIGINT research results */
  sigint: SigintResult | null
  /** Total time for recon */
  durationMs: number
  /** Whether recon was successful */
  success: boolean
  /** Formatted context for oracle prompts */
  formattedContext: string
}

export interface XhighReconOptions {
  /** OpenCode client for agent invocation */
  client?: OpenCodeClient
  /** Working directory */
  cwd?: string
  /** Timeout for each reconnaissance agent (ms) */
  agentTimeoutMs?: number
  /** Whether to run RECON */
  runRecon?: boolean
  /** Whether to run SIGINT */
  runSigint?: boolean
}

// Default timeout for each recon agent
const DEFAULT_AGENT_TIMEOUT_MS = 30_000

// =============================================================================
// Reconnaissance Execution
// =============================================================================

/**
 * Perform XHIGH mode reconnaissance
 *
 * Runs RECON and SIGINT agents in parallel to gather context
 * before oracle deliberation.
 */
export async function performXhighRecon(
  question: string,
  missionDescription: string,
  options: XhighReconOptions = {}
): Promise<XhighReconResult> {
  const {
    client,
    cwd = process.cwd(),
    agentTimeoutMs = DEFAULT_AGENT_TIMEOUT_MS,
    runRecon = true,
    runSigint = true,
  } = options

  const startTime = Date.now()

  logger.info('Starting XHIGH reconnaissance', {
    question: question.substring(0, 100),
    runRecon,
    runSigint,
    hasClient: !!client,
  })

  // Run RECON and SIGINT in parallel
  const [reconResult, sigintResult] = await Promise.all([
    runRecon
      ? runReconAgent(question, missionDescription, client, cwd, agentTimeoutMs)
      : Promise.resolve(null),
    runSigint
      ? runSigintAgent(question, missionDescription, client, cwd, agentTimeoutMs)
      : Promise.resolve(null),
  ])

  const durationMs = Date.now() - startTime
  const success =
    (runRecon ? reconResult !== null : true) && (runSigint ? sigintResult !== null : true)

  // Format context for oracle prompts
  const formattedContext = formatReconContext(reconResult, sigintResult)

  logger.info('XHIGH reconnaissance complete', {
    durationMs,
    reconFound: reconResult?.found ?? false,
    reconFiles: reconResult?.files?.length ?? 0,
    sigintConfidence: sigintResult?.confidence ?? 'N/A',
  })

  return {
    recon: reconResult,
    sigint: sigintResult,
    durationMs,
    success,
    formattedContext,
  }
}

/**
 * Run RECON reconnaissance
 */
async function runReconAgent(
  question: string,
  missionDescription: string,
  client: OpenCodeClient | undefined,
  cwd: string,
  timeoutMs: number
): Promise<ReconResult | null> {
  try {
    // If no client, return simulated result
    if (!client) {
      return simulateReconResult(question)
    }

    // Create session for RECON
    const createResult = await client.session.create({
      body: { title: 'Delta9 RECON' },
      query: { directory: cwd },
    })

    if (createResult.error || !createResult.data?.id) {
      logger.warn('Failed to create RECON session', { error: createResult.error })
      return simulateReconResult(question)
    }

    const sessionId = createResult.data.id

    // Build RECON prompt
    const prompt = `Search the codebase to find relevant files and code for:

**Question:** ${question}

**Mission Context:** ${missionDescription}

Focus on:
- Files that are likely to be modified or referenced
- Existing patterns that should be followed
- Related implementations

Return JSON response as specified in your system prompt.`

    // Create RECON agent config with proper model
    const reconAgent = createReconAgent(cwd)

    // Parse model from agent config (config-driven)
    const reconModelId = reconAgent.model ?? 'anthropic/claude-haiku-4'
    const reconModel = parseModelId(reconModelId)

    // Send prompt
    await client.session.prompt({
      path: { id: sessionId },
      body: {
        model: {
          providerID: reconModel.provider,
          modelID: reconModel.model,
        },
        system: reconAgent.prompt,
        parts: [{ type: 'text', text: prompt }],
      },
    })

    // Wait for response (with timeout)
    const response = await Promise.race([
      waitForAgentResponse(client, sessionId, 'RECON'),
      timeoutPromise(timeoutMs, 'RECON'),
    ])

    // Parse RECON response
    return parseReconResponse(response)
  } catch (error) {
    logger.error('RECON reconnaissance failed', {
      error: error instanceof Error ? error.message : String(error),
    })
    return simulateReconResult(question)
  }
}

/**
 * Run SIGINT research
 */
async function runSigintAgent(
  question: string,
  missionDescription: string,
  client: OpenCodeClient | undefined,
  cwd: string,
  timeoutMs: number
): Promise<SigintResult | null> {
  try {
    // If no client, return simulated result
    if (!client) {
      return simulateSigintResult(question)
    }

    // Create session for SIGINT
    const createResult = await client.session.create({
      body: { title: 'Delta9 SIGINT' },
      query: { directory: cwd },
    })

    if (createResult.error || !createResult.data?.id) {
      logger.warn('Failed to create SIGINT session', { error: createResult.error })
      return simulateSigintResult(question)
    }

    const sessionId = createResult.data.id

    // Build SIGINT prompt
    const prompt = `Research documentation and best practices for:

**Question:** ${question}

**Mission Context:** ${missionDescription}

Find:
- Official documentation
- Best practices and patterns
- Examples from authoritative sources

Return JSON response as specified in your system prompt.`

    // Create SIGINT agent config with proper model
    const sigintAgent = createSigintAgent(cwd)

    // Parse model from agent config (config-driven)
    const sigintModelId = sigintAgent.model ?? 'anthropic/claude-sonnet-4-5'
    const sigintModel = parseModelId(sigintModelId)

    // Send prompt
    await client.session.prompt({
      path: { id: sessionId },
      body: {
        model: {
          providerID: sigintModel.provider,
          modelID: sigintModel.model,
        },
        system: sigintAgent.prompt,
        parts: [{ type: 'text', text: prompt }],
      },
    })

    // Wait for response (with timeout)
    const response = await Promise.race([
      waitForAgentResponse(client, sessionId, 'SIGINT'),
      timeoutPromise(timeoutMs, 'SIGINT'),
    ])

    // Parse SIGINT response
    return parseSigintResponse(response)
  } catch (error) {
    logger.error('SIGINT reconnaissance failed', {
      error: error instanceof Error ? error.message : String(error),
    })
    return simulateSigintResult(question)
  }
}

// =============================================================================
// Response Parsing
// =============================================================================

/**
 * Parse RECON response JSON
 */
function parseReconResponse(response: string): ReconResult {
  try {
    const jsonMatch = response.match(/\{[\s\S]*\}/)
    if (!jsonMatch) {
      return createEmptyReconResult('Could not parse RECON response')
    }

    const parsed = JSON.parse(jsonMatch[0]) as Partial<ReconResult>
    return {
      found: parsed.found ?? false,
      files: parsed.files ?? [],
      snippets: parsed.snippets ?? [],
      summary: parsed.summary ?? 'No summary available',
      suggestions: parsed.suggestions ?? [],
    }
  } catch {
    return createEmptyReconResult('RECON response parsing failed')
  }
}

/**
 * Parse SIGINT response JSON
 */
function parseSigintResponse(response: string): SigintResult {
  try {
    const jsonMatch = response.match(/\{[\s\S]*\}/)
    if (!jsonMatch) {
      return createEmptySigintResult('Could not parse SIGINT response')
    }

    const parsed = JSON.parse(jsonMatch[0]) as Partial<SigintResult>
    return {
      classification: parsed.classification ?? 'CONCEPTUAL',
      answer: parsed.answer ?? 'No answer available',
      evidence: parsed.evidence ?? [],
      confidence: parsed.confidence ?? 'LOW',
      confidenceReason: parsed.confidenceReason ?? 'Response parsing issues',
    }
  } catch {
    return createEmptySigintResult('SIGINT response parsing failed')
  }
}

// =============================================================================
// Simulation Mode
// =============================================================================

function simulateReconResult(question: string): ReconResult {
  return {
    found: true,
    files: [],
    snippets: [],
    summary: `[Simulation] RECON would search codebase for: "${question.substring(0, 50)}..."`,
    suggestions: [
      'Real RECON invocation requires OpenCode SDK',
      'Search manually using Glob and Grep tools',
    ],
  }
}

function simulateSigintResult(question: string): SigintResult {
  return {
    classification: 'CONCEPTUAL',
    answer: `[Simulation] SIGINT would research: "${question.substring(0, 50)}..."`,
    evidence: [],
    confidence: 'LOW',
    confidenceReason: 'Simulation mode - real SIGINT invocation requires OpenCode SDK',
  }
}

function createEmptyReconResult(summary: string): ReconResult {
  return {
    found: false,
    files: [],
    snippets: [],
    summary,
    suggestions: [],
  }
}

function createEmptySigintResult(answer: string): SigintResult {
  return {
    classification: 'CONCEPTUAL',
    answer,
    evidence: [],
    confidence: 'LOW',
    confidenceReason: 'Response unavailable',
  }
}

// =============================================================================
// Context Formatting
// =============================================================================

/**
 * Format reconnaissance results for oracle prompts
 */
export function formatReconContext(recon: ReconResult | null, sigint: SigintResult | null): string {
  const sections: string[] = []

  // RECON section
  if (recon) {
    sections.push(`## Codebase Reconnaissance (RECON)

${recon.summary}

${
  recon.files.length > 0
    ? `**Relevant Files:**
${recon.files.map((f) => `- \`${f}\``).join('\n')}`
    : ''
}

${
  recon.snippets.length > 0
    ? `**Code Snippets:**
${recon.snippets
  .map(
    (s) => `\`${s.file}:${s.line}\`
\`\`\`
${s.content}
\`\`\``
  )
  .join('\n\n')}`
    : ''
}`)
  }

  // SIGINT section
  if (sigint) {
    sections.push(`## Documentation Research (SIGINT)

**Confidence:** ${sigint.confidence} - ${sigint.confidenceReason}

${sigint.answer}

${
  sigint.evidence.length > 0
    ? `**Evidence:**
${sigint.evidence
  .map(
    (e) => `- **${e.source}**${e.url ? ` ([link](${e.url}))` : ''}
  > ${e.quote}
  _Relevance: ${e.relevance}_`
  )
  .join('\n\n')}`
    : ''
}`)
  }

  if (sections.length === 0) {
    return ''
  }

  return `# XHIGH Reconnaissance Results

${sections.join('\n\n---\n\n')}

---
_Use this evidence to inform your analysis._`
}

// =============================================================================
// Helpers
// =============================================================================

/**
 * Wait for agent response by polling session
 */
async function waitForAgentResponse(
  client: OpenCodeClient,
  sessionId: string,
  agentName: string
): Promise<string> {
  const pollIntervalMs = 500
  const maxPolls = 60

  for (let i = 0; i < maxPolls; i++) {
    await sleep(pollIntervalMs)

    try {
      const messagesResult = await client.session.messages({
        path: { id: sessionId },
      })

      const messages = messagesResult.data || []
      const assistantMessages = messages.filter((m) => m.info?.role === 'assistant')

      if (assistantMessages.length > 0) {
        const lastMessage = assistantMessages[assistantMessages.length - 1]
        const textParts = lastMessage.parts?.filter((p) => p.type === 'text')
        if (textParts && textParts.length > 0) {
          const text = textParts.map((p) => p.text || '').join('\n')
          if (text.includes('{') && text.includes('}')) {
            return text
          }
        }
      }
    } catch (error) {
      logger.warn(`Error polling ${agentName}`, {
        error: error instanceof Error ? error.message : String(error),
      })
    }
  }

  throw new Error(`${agentName} response timeout`)
}

function timeoutPromise(ms: number, agentName: string): Promise<never> {
  return new Promise((_, reject) => {
    setTimeout(() => {
      reject(new Error(`${agentName} timed out after ${ms}ms`))
    }, ms)
  })
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}
