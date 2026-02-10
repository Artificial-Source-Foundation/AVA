/**
 * Agent Planner
 * Breaks down goals into executable steps and generates recovery plans
 *
 * Based on Gemini CLI's codebase-investigator pattern for structured output
 */

import { createClient, getAuth, getWeakModelConfig } from '../llm/client.js'
import { getToolDefinitions } from '../tools/registry.js'
import type { ChatMessage, ToolDefinition } from '../types/llm.js'
import type { AgentStep, ToolCallInfo } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * A planned step before execution
 */
export interface PlannedStep {
  /** Step number (1-indexed) */
  stepNumber: number
  /** Description of what this step will do */
  description: string
  /** Tools that will likely be used */
  toolsRequired: string[]
  /** Expected output/result */
  expectedOutput: string
  /** Dependencies on other steps (by step number) */
  dependsOn: number[]
  /** Estimated complexity (1-5) */
  complexity: number
}

/**
 * A complete task plan
 */
export interface TaskPlan {
  /** Original goal/task */
  goal: string
  /** Summary of the approach */
  summary: string
  /** Ordered list of steps */
  steps: PlannedStep[]
  /** Estimated total complexity (sum) */
  totalComplexity: number
  /** Potential risks or blockers */
  risks: string[]
  /** Success criteria */
  successCriteria: string[]
}

/**
 * Recovery strategy types
 */
export type RecoveryStrategy =
  | 'retry' // Same approach, try again
  | 'alternate' // Different tool/method
  | 'decompose' // Break into smaller steps
  | 'rollback' // Undo and try different path
  | 'skip' // Skip this step if possible
  | 'abort' // Cannot recover

/**
 * A recovery plan for a failed step
 */
export interface RecoveryPlan {
  /** The failed step */
  failedStep: AgentStep
  /** Why it failed */
  failureAnalysis: string
  /** Recommended strategy */
  strategy: RecoveryStrategy
  /** Alternative steps if strategy is 'alternate' or 'decompose' */
  alternativeSteps?: PlannedStep[]
  /** Specific actions to take */
  actions: string[]
  /** Whether the overall goal can still be achieved */
  goalRecoverable: boolean
}

/**
 * Planner configuration
 */
export interface PlannerConfig {
  /** LLM provider to use */
  provider?: 'anthropic' | 'openai' | 'openrouter'
  /** Model to use */
  model?: string
  /** Maximum steps to plan */
  maxSteps?: number
}

function getDefaultPlannerConfig(): Required<PlannerConfig> {
  const weak = getWeakModelConfig()
  return {
    provider: weak.provider as 'anthropic' | 'openai' | 'openrouter',
    model: weak.model,
    maxSteps: 15,
  }
}

// ============================================================================
// Planner Implementation
// ============================================================================

/**
 * Agent Planner - generates task plans and recovery strategies
 */
export class AgentPlanner {
  private config: Required<PlannerConfig>

  constructor(config: PlannerConfig = {}) {
    this.config = { ...getDefaultPlannerConfig(), ...config }
  }

  /**
   * Create a plan for accomplishing a goal
   */
  async plan(goal: string, context?: string): Promise<TaskPlan> {
    const auth = await getAuth(this.config.provider)
    if (!auth) {
      throw new Error(`No authentication for provider: ${this.config.provider}`)
    }

    const client = await createClient(this.config.provider)
    const tools = getToolDefinitions()

    const systemPrompt = this.buildPlanningPrompt(tools)
    const userPrompt = this.buildGoalPrompt(goal, context)

    const messages: ChatMessage[] = [
      { role: 'system', content: systemPrompt },
      { role: 'user', content: userPrompt },
    ]

    // Get plan from LLM
    let responseContent = ''
    const stream = client.stream(messages, {
      provider: this.config.provider,
      model: this.config.model,
      authMethod: auth.type,
    })

    for await (const delta of stream) {
      if (delta.content) {
        responseContent += delta.content
      }
    }

    // Parse the plan from response
    return this.parsePlanResponse(responseContent, goal)
  }

  /**
   * Generate a recovery plan for a failed step
   */
  async planRecovery(
    failedStep: AgentStep,
    completedSteps: AgentStep[],
    goal: string
  ): Promise<RecoveryPlan> {
    const auth = await getAuth(this.config.provider)
    if (!auth) {
      throw new Error(`No authentication for provider: ${this.config.provider}`)
    }

    const client = await createClient(this.config.provider)
    const tools = getToolDefinitions()

    const systemPrompt = this.buildRecoveryPrompt(tools)
    const userPrompt = this.buildRecoveryGoalPrompt(failedStep, completedSteps, goal)

    const messages: ChatMessage[] = [
      { role: 'system', content: systemPrompt },
      { role: 'user', content: userPrompt },
    ]

    let responseContent = ''
    const stream = client.stream(messages, {
      provider: this.config.provider,
      model: this.config.model,
      authMethod: auth.type,
    })

    for await (const delta of stream) {
      if (delta.content) {
        responseContent += delta.content
      }
    }

    return this.parseRecoveryResponse(responseContent, failedStep)
  }

  /**
   * Classify an error to determine recovery strategy
   */
  classifyError(error: string): RecoveryStrategy {
    const errorLower = error.toLowerCase()

    // Permission/access errors - try alternate approach
    if (
      errorLower.includes('permission denied') ||
      errorLower.includes('eacces') ||
      errorLower.includes('access denied')
    ) {
      return 'alternate'
    }

    // Not found errors - might need decomposition
    if (
      errorLower.includes('not found') ||
      errorLower.includes('enoent') ||
      errorLower.includes('no such file')
    ) {
      return 'decompose'
    }

    // Timeout errors - retry with smaller scope
    if (errorLower.includes('timeout') || errorLower.includes('timed out')) {
      return 'retry'
    }

    // Validation errors - might need alternate approach
    if (errorLower.includes('validation') || errorLower.includes('invalid')) {
      return 'alternate'
    }

    // Syntax/parse errors - likely unrecoverable
    if (errorLower.includes('syntax error') || errorLower.includes('parse error')) {
      return 'abort'
    }

    // Connection errors - retry
    if (errorLower.includes('connection') || errorLower.includes('network')) {
      return 'retry'
    }

    // Default to retry
    return 'retry'
  }

  // ==========================================================================
  // Private Methods
  // ==========================================================================

  private buildPlanningPrompt(tools: ToolDefinition[]): string {
    const toolList = tools.map((t) => `- ${t.name}: ${t.description}`).join('\n')

    return `You are a task planning assistant. Your job is to break down goals into clear, executable steps.

# Available Tools
${toolList}

# Planning Rules
1. Break complex goals into 3-${this.config.maxSteps} concrete steps
2. Each step should be specific and actionable
3. Identify which tools will be needed for each step
4. Consider dependencies between steps
5. Rate complexity of each step (1=simple, 5=complex)
6. Identify potential risks and blockers
7. Define clear success criteria

# Output Format
You must respond with a valid JSON object in this exact format:
{
  "summary": "Brief summary of the approach",
  "steps": [
    {
      "stepNumber": 1,
      "description": "What this step does",
      "toolsRequired": ["tool_name"],
      "expectedOutput": "What we expect to get",
      "dependsOn": [],
      "complexity": 1
    }
  ],
  "risks": ["potential risk 1"],
  "successCriteria": ["criterion 1"]
}

Only output the JSON, no other text.`
  }

  private buildGoalPrompt(goal: string, context?: string): string {
    let prompt = `Create a plan to accomplish this goal:\n\n${goal}`
    if (context) {
      prompt += `\n\nAdditional context:\n${context}`
    }
    return prompt
  }

  private buildRecoveryPrompt(tools: ToolDefinition[]): string {
    const toolList = tools.map((t) => `- ${t.name}: ${t.description}`).join('\n')

    return `You are a recovery planning assistant. Your job is to analyze failed steps and recommend recovery strategies.

# Available Tools
${toolList}

# Recovery Strategies
- retry: Same approach, try again (for transient errors)
- alternate: Use a different tool or method
- decompose: Break the step into smaller sub-steps
- rollback: Undo changes and try a different path
- skip: Skip this step if not essential
- abort: Cannot recover, stop execution

# Analysis Rules
1. Understand WHY the step failed
2. Determine if the goal is still achievable
3. Choose the most appropriate recovery strategy
4. If alternate/decompose, provide specific new steps

# Output Format
{
  "failureAnalysis": "Why the step failed",
  "strategy": "retry|alternate|decompose|rollback|skip|abort",
  "alternativeSteps": [/* only if strategy is alternate/decompose */],
  "actions": ["specific action 1"],
  "goalRecoverable": true
}

Only output the JSON, no other text.`
  }

  private buildRecoveryGoalPrompt(
    failedStep: AgentStep,
    completedSteps: AgentStep[],
    goal: string
  ): string {
    const completedSummary = completedSteps
      .map((s) => `- Step ${s.turn}: ${s.description} (${s.status})`)
      .join('\n')

    const toolCallsSummary = failedStep.toolsCalled
      .map(
        (t: ToolCallInfo) =>
          `  - ${t.name}: ${t.success ? 'success' : 'FAILED'} - ${t.result?.slice(0, 100)}`
      )
      .join('\n')

    return `# Original Goal
${goal}

# Completed Steps
${completedSummary || 'None'}

# Failed Step
Step ${failedStep.turn}: ${failedStep.description}
Error: ${failedStep.error || 'Unknown error'}
Tool calls:
${toolCallsSummary || '  None'}

Analyze this failure and recommend a recovery strategy.`
  }

  private parsePlanResponse(response: string, goal: string): TaskPlan {
    try {
      // Try to extract JSON from response
      const jsonMatch = response.match(/\{[\s\S]*\}/)
      if (!jsonMatch) {
        return this.createFallbackPlan(goal)
      }

      const parsed = JSON.parse(jsonMatch[0])

      const steps: PlannedStep[] = (parsed.steps || []).map(
        (s: Record<string, unknown>, i: number) => ({
          stepNumber: (s.stepNumber as number) || i + 1,
          description: (s.description as string) || 'Execute step',
          toolsRequired: (s.toolsRequired as string[]) || [],
          expectedOutput: (s.expectedOutput as string) || '',
          dependsOn: (s.dependsOn as number[]) || [],
          complexity: (s.complexity as number) || 2,
        })
      )

      return {
        goal,
        summary: (parsed.summary as string) || 'Execute the task',
        steps,
        totalComplexity: steps.reduce((acc, s) => acc + s.complexity, 0),
        risks: (parsed.risks as string[]) || [],
        successCriteria: (parsed.successCriteria as string[]) || ['Task completed successfully'],
      }
    } catch {
      return this.createFallbackPlan(goal)
    }
  }

  private createFallbackPlan(goal: string): TaskPlan {
    return {
      goal,
      summary: 'Execute the task step by step',
      steps: [
        {
          stepNumber: 1,
          description: 'Analyze the goal and gather context',
          toolsRequired: ['read', 'glob', 'grep'],
          expectedOutput: 'Understanding of the task requirements',
          dependsOn: [],
          complexity: 2,
        },
        {
          stepNumber: 2,
          description: 'Execute the main task',
          toolsRequired: [],
          expectedOutput: 'Task completed',
          dependsOn: [1],
          complexity: 3,
        },
        {
          stepNumber: 3,
          description: 'Verify the results',
          toolsRequired: ['read'],
          expectedOutput: 'Confirmation that task succeeded',
          dependsOn: [2],
          complexity: 1,
        },
      ],
      totalComplexity: 6,
      risks: ['Unforeseen complications'],
      successCriteria: ['Task completed as requested'],
    }
  }

  private parseRecoveryResponse(response: string, failedStep: AgentStep): RecoveryPlan {
    try {
      const jsonMatch = response.match(/\{[\s\S]*\}/)
      if (!jsonMatch) {
        return this.createFallbackRecovery(failedStep)
      }

      const parsed = JSON.parse(jsonMatch[0])

      const alternativeSteps = (parsed.alternativeSteps || []).map(
        (s: Record<string, unknown>, i: number) => ({
          stepNumber: (s.stepNumber as number) || i + 1,
          description: (s.description as string) || 'Alternative step',
          toolsRequired: (s.toolsRequired as string[]) || [],
          expectedOutput: (s.expectedOutput as string) || '',
          dependsOn: (s.dependsOn as number[]) || [],
          complexity: (s.complexity as number) || 2,
        })
      )

      return {
        failedStep,
        failureAnalysis: (parsed.failureAnalysis as string) || 'Unknown failure',
        strategy: this.validateStrategy((parsed.strategy as string) || 'retry'),
        alternativeSteps: alternativeSteps.length > 0 ? alternativeSteps : undefined,
        actions: (parsed.actions as string[]) || ['Retry the operation'],
        goalRecoverable: parsed.goalRecoverable !== false,
      }
    } catch {
      return this.createFallbackRecovery(failedStep)
    }
  }

  private createFallbackRecovery(failedStep: AgentStep): RecoveryPlan {
    const strategy = this.classifyError(failedStep.error || '')

    return {
      failedStep,
      failureAnalysis: failedStep.error || 'Step execution failed',
      strategy,
      actions:
        strategy === 'retry'
          ? ['Wait briefly and retry the operation']
          : ['Try an alternative approach'],
      goalRecoverable: strategy !== 'abort',
    }
  }

  private validateStrategy(strategy: string): RecoveryStrategy {
    const validStrategies: RecoveryStrategy[] = [
      'retry',
      'alternate',
      'decompose',
      'rollback',
      'skip',
      'abort',
    ]
    return validStrategies.includes(strategy as RecoveryStrategy)
      ? (strategy as RecoveryStrategy)
      : 'retry'
  }
}

// ============================================================================
// Factory Functions
// ============================================================================

/**
 * Create a task plan for a goal
 */
export async function planTask(
  goal: string,
  context?: string,
  config?: PlannerConfig
): Promise<TaskPlan> {
  const planner = new AgentPlanner(config)
  return planner.plan(goal, context)
}

/**
 * Create a recovery plan for a failed step
 */
export async function planRecovery(
  failedStep: AgentStep,
  completedSteps: AgentStep[],
  goal: string,
  config?: PlannerConfig
): Promise<RecoveryPlan> {
  const planner = new AgentPlanner(config)
  return planner.planRecovery(failedStep, completedSteps, goal)
}
