import type { AgentMode, Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'

export interface ArchitectConfig {
  plannerProvider: string
  plannerModel: string
  executorProvider: string
  executorModel: string
  maxPlanSteps: number
}

const DEFAULT_ARCHITECT_CONFIG: ArchitectConfig = {
  plannerProvider: 'anthropic',
  plannerModel: 'claude-opus-4-6',
  executorProvider: 'anthropic',
  executorModel: 'claude-sonnet-4-6',
  maxPlanSteps: 10,
}

const READ_ONLY_TOOL_NAMES = new Set(['read_file', 'glob', 'grep', 'ls', 'websearch'])

function plannerGuidance(config: ArchitectConfig): string {
  return [
    'ARCHITECT MODE (Planning Phase)',
    `Planner model: ${config.plannerProvider}/${config.plannerModel}`,
    `Executor model: ${config.executorProvider}/${config.executorModel}`,
    `Generate at most ${config.maxPlanSteps} implementation steps.`,
    'You are an architect. Analyze the task and produce a step-by-step implementation plan.',
    'Do NOT write code. Output a numbered list of changes with file paths and descriptions.',
  ].join('\n')
}

export function buildExecutorSystemPrompt(basePrompt: string, plan: string): string {
  return [
    basePrompt,
    '',
    'ARCHITECT MODE (Execution Phase)',
    'You are an executor. Follow this plan exactly:',
    plan,
    'Implement each step. Do not deviate from the plan unless impossible, and explain any necessary deviation.',
  ].join('\n')
}

export function getArchitectPlanningTools(tools: ToolDefinition[]): ToolDefinition[] {
  return tools.filter((tool) => READ_ONLY_TOOL_NAMES.has(tool.name))
}

export function getArchitectExecutionTools(tools: ToolDefinition[]): ToolDefinition[] {
  return tools
}

export function createArchitectMode(input: ArchitectConfig): AgentMode {
  const config: ArchitectConfig = {
    ...DEFAULT_ARCHITECT_CONFIG,
    ...input,
    maxPlanSteps: Math.max(1, input.maxPlanSteps || DEFAULT_ARCHITECT_CONFIG.maxPlanSteps),
  }

  return {
    name: 'architect',
    description: 'Two-model workflow: expensive model plans, cheaper model executes',
    filterTools(tools: ToolDefinition[]): ToolDefinition[] {
      return getArchitectPlanningTools(tools)
    },
    systemPrompt(base: string): string {
      return `${base}\n\n${plannerGuidance(config)}`
    },
  }
}

export function registerArchitectMode(
  api: ExtensionAPI,
  config: Partial<ArchitectConfig> = {}
): Disposable {
  const merged: ArchitectConfig = {
    ...DEFAULT_ARCHITECT_CONFIG,
    ...config,
  }
  return api.registerAgentMode(createArchitectMode(merged))
}
