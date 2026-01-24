/**
 * Delta9 Routing Tools
 *
 * Tools for task routing, complexity detection, and agent dispatch.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { analyzeComplexity, describeComplexity } from '../routing/complexity.js'

// Use the tool's built-in schema
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create routing tools
 */
export function createRoutingTools(): Record<string, ToolDefinition> {
  /**
   * Analyze task complexity to determine council mode
   */
  const analyze_complexity = tool({
    description: `Analyze a task description to determine its complexity and suggest the appropriate council mode.

Returns:
- complexity: low/medium/high/critical
- suggestedMode: none/quick/standard/xhigh
- confidence: 0-1
- reasons: why this complexity was determined
- indicators: keywords, scope, risk, estimated files`,

    args: {
      description: s.string().describe('Task or request description to analyze'),
    },

    async execute(args, _ctx) {
      const analysis = analyzeComplexity(args.description)

      return JSON.stringify({
        success: true,
        ...analysis,
        humanReadable: describeComplexity(analysis),
      })
    },
  })

  /**
   * Get recommended agent for a task type
   */
  const recommend_agent = tool({
    description: `Get the recommended agent type for a specific task.

Task types and recommended agents:
- ui/frontend/component/style → UI-Ops (Gemini)
- test/spec/coverage → QA (Sonnet)
- docs/readme/api-docs → Scribe (Gemini Flash)
- search/find/grep → Scout (Haiku)
- research/docs-lookup → Intel (GLM)
- stuck/blocked/help → Strategist (GPT)
- general code → Operator (Sonnet)`,

    args: {
      taskType: s.string().describe('Type of task (e.g., "ui", "test", "docs", "search")'),
      taskDescription: s.string().optional().describe('Full task description for better routing'),
    },

    async execute(args, _ctx) {
      const type = args.taskType.toLowerCase()
      const desc = (args.taskDescription || '').toLowerCase()

      // Routing logic based on task type
      let agent: string
      let model: string
      let reason: string

      if (['ui', 'frontend', 'component', 'style', 'css', 'react', 'vue'].some(k => type.includes(k) || desc.includes(k))) {
        agent = 'ui-ops'
        model = 'google/gemini-2.0-flash'
        reason = 'Frontend/UI task - UI-Ops specializes in components, styling, and accessibility'
      } else if (['test', 'spec', 'coverage', 'jest', 'vitest', 'playwright'].some(k => type.includes(k) || desc.includes(k))) {
        agent = 'qa'
        model = 'anthropic/claude-sonnet-4'
        reason = 'Testing task - QA agent writes comprehensive tests'
      } else if (['doc', 'readme', 'api doc', 'comment', 'jsdoc'].some(k => type.includes(k) || desc.includes(k))) {
        agent = 'scribe'
        model = 'google/gemini-2.0-flash'
        reason = 'Documentation task - Scribe writes clear, comprehensive docs'
      } else if (['search', 'find', 'grep', 'locate', 'where'].some(k => type.includes(k) || desc.includes(k))) {
        agent = 'scout'
        model = 'anthropic/claude-haiku-4'
        reason = 'Search task - Scout performs fast codebase searches'
      } else if (['research', 'lookup', 'example', 'library', 'package'].some(k => type.includes(k) || desc.includes(k))) {
        agent = 'intel'
        model = 'anthropic/claude-sonnet-4'
        reason = 'Research task - Intel searches docs, GitHub, and web'
      } else if (['stuck', 'blocked', 'help', 'advice', 'guidance'].some(k => type.includes(k) || desc.includes(k))) {
        agent = 'strategist'
        model = 'openai/gpt-4o'
        reason = 'Guidance task - Strategist provides mid-execution advice'
      } else if (['image', 'screenshot', 'diagram', 'visual'].some(k => type.includes(k) || desc.includes(k))) {
        agent = 'optics'
        model = 'google/gemini-2.0-flash'
        reason = 'Visual task - Optics handles images and diagrams'
      } else {
        agent = 'operator'
        model = 'anthropic/claude-sonnet-4'
        reason = 'General coding task - Operator handles implementation'
      }

      return JSON.stringify({
        success: true,
        recommendedAgent: agent,
        defaultModel: model,
        reason,
        taskType: type,
      })
    },
  })

  return {
    analyze_complexity,
    recommend_agent,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type RoutingTools = ReturnType<typeof createRoutingTools>
