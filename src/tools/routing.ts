/**
 * Delta9 Routing Tools
 *
 * Tools for task routing, complexity detection, and agent dispatch.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { analyzeComplexity, describeComplexity } from '../routing/complexity.js'
import { routeTask, describeRouteDecision } from '../routing/task-router.js'
import {
  routeToCategory,
  describeCategoryRoute,
  getAllCategories,
  getCategoryConfig,
  isValidCategory,
  type TaskCategory,
} from '../routing/categories.js'
import { loadConfig } from '../lib/config.js'

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

      // Load config for model lookups
      const config = loadConfig(process.cwd())

      // Routing logic based on task type
      let agent: string
      let model: string
      let reason: string

      if (
        ['ui', 'frontend', 'component', 'style', 'css', 'react', 'vue'].some(
          (k) => type.includes(k) || desc.includes(k)
        )
      ) {
        agent = 'ui-ops'
        model = config.support.uiOps.model
        reason = 'Frontend/UI task - UI-Ops specializes in components, styling, and accessibility'
      } else if (
        ['test', 'spec', 'coverage', 'jest', 'vitest', 'playwright'].some(
          (k) => type.includes(k) || desc.includes(k)
        )
      ) {
        agent = 'qa'
        model = config.support.qa.model
        reason = 'Testing task - QA agent writes comprehensive tests'
      } else if (
        ['doc', 'readme', 'api doc', 'comment', 'jsdoc'].some(
          (k) => type.includes(k) || desc.includes(k)
        )
      ) {
        agent = 'scribe'
        model = config.support.scribe.model
        reason = 'Documentation task - Scribe writes clear, comprehensive docs'
      } else if (
        ['search', 'find', 'grep', 'locate', 'where'].some(
          (k) => type.includes(k) || desc.includes(k)
        )
      ) {
        agent = 'scout'
        model = config.support.scout.model
        reason = 'Search task - Scout performs fast codebase searches'
      } else if (
        ['research', 'lookup', 'example', 'library', 'package'].some(
          (k) => type.includes(k) || desc.includes(k)
        )
      ) {
        agent = 'intel'
        model = config.support.intel.model
        reason = 'Research task - Intel searches docs, GitHub, and web'
      } else if (
        ['stuck', 'blocked', 'help', 'advice', 'guidance'].some(
          (k) => type.includes(k) || desc.includes(k)
        )
      ) {
        agent = 'strategist'
        model = config.support.strategist.model
        reason = 'Guidance task - Strategist provides mid-execution advice'
      } else if (
        ['image', 'screenshot', 'diagram', 'visual'].some(
          (k) => type.includes(k) || desc.includes(k)
        )
      ) {
        // Visual tasks go to UI ops (FACADE) since SPECTRE was removed
        agent = 'ui-ops'
        model = config.support.uiOps.model
        reason = 'Visual task - FACADE handles UI and visual elements'
      } else {
        agent = 'operator'
        model = config.operators.tier2Model
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

  /**
   * Route a task to the most appropriate agent
   */
  const route_task = tool({
    description: `Route a task to the most appropriate agent based on keywords, complexity, and context.

This is a more sophisticated routing system than recommend_agent:
- Analyzes task description for keyword patterns
- Considers complexity analysis
- Accounts for context (previous failures, budget constraints)
- Provides confidence score and fallback recommendations

Returns:
- agent: recommended agent (operator, scout, intel, strategist, etc.)
- model: default model for that agent
- confidence: 0-1 confidence in routing
- reason: explanation for the routing
- fallbackAgent: alternative if primary unavailable`,

    args: {
      taskDescription: s.string().describe('Full description of the task to route'),
      taskType: s.string().optional().describe('Explicit task type hint'),
      previousFailures: s.number().optional().describe('Number of previous failed attempts'),
      budgetConstrained: s.boolean().optional().describe('Whether to prefer cheaper agents'),
    },

    async execute(args, _ctx) {
      const decision = routeTask({
        taskDescription: args.taskDescription,
        taskType: args.taskType,
        context: {
          previousFailures: args.previousFailures,
          budgetConstrained: args.budgetConstrained,
        },
      })

      return JSON.stringify({
        success: true,
        ...decision,
        humanReadable: describeRouteDecision(decision),
      })
    },
  })

  /**
   * Route task to category and get settings
   */
  const route_to_category = tool({
    description: `Route a task to a category and get the configured temperature, model, and agent.

Categories:
- planning: Strategic planning and architecture (temp 0.7, Opus)
- coding: General implementation (temp 0.3, Sonnet)
- testing: Test writing and QA (temp 0.2, Sonnet)
- documentation: Docs, comments, README (temp 0.5, Gemini)
- research: Information lookup (temp 0.4, Sonnet)
- ui: Frontend and UI work (temp 0.4, Gemini)
- refactoring: Code refactoring (temp 0.2, Opus)
- bugfix: Bug fixing (temp 0.2, Sonnet)

Returns category match with model, temperature, and recommended agent.`,

    args: {
      taskDescription: s.string().describe('Task description to categorize'),
      forceCategory: s
        .string()
        .optional()
        .describe('Force a specific category (planning, coding, testing, etc.)'),
      forceModel: s.string().optional().describe('Override the model for this task'),
      forceTemperature: s.number().optional().describe('Override the temperature (0-1)'),
    },

    async execute(args, _ctx) {
      // Validate forceCategory if provided
      if (args.forceCategory && !isValidCategory(args.forceCategory)) {
        return JSON.stringify({
          success: false,
          error: `Invalid category: ${args.forceCategory}`,
          validCategories: getAllCategories(),
        })
      }

      const result = routeToCategory(
        args.taskDescription,
        undefined, // Use default configs
        {
          forceCategory: args.forceCategory as TaskCategory | undefined,
          forceModel: args.forceModel,
          forceTemperature: args.forceTemperature,
        }
      )

      return JSON.stringify({
        success: true,
        category: result.primary.category,
        categoryName: result.primary.config.name,
        confidence: result.primary.confidence,
        reason: result.primary.reason,
        matchedKeywords: result.primary.matchedKeywords,
        effectiveModel: result.effectiveModel,
        effectiveTemperature: result.effectiveTemperature,
        recommendedAgent: result.recommendedAgent,
        secondaryCategories: result.secondary.map((m) => ({
          category: m.category,
          confidence: m.confidence,
        })),
        humanReadable: describeCategoryRoute(result),
      })
    },
  })

  /**
   * List all categories and their configurations
   */
  const list_categories = tool({
    description: `List all task categories with their configured models and temperatures.

Shows each category's:
- Model
- Temperature
- Preferred agent
- Budget priority
- Keywords`,

    args: {
      category: s.string().optional().describe('Specific category to show details for'),
    },

    async execute(args, _ctx) {
      if (args.category) {
        if (!isValidCategory(args.category)) {
          return JSON.stringify({
            success: false,
            error: `Invalid category: ${args.category}`,
            validCategories: getAllCategories(),
          })
        }

        const config = getCategoryConfig(args.category as TaskCategory)
        return JSON.stringify({
          success: true,
          category: args.category,
          config: {
            name: config.name,
            description: config.description,
            model: config.model,
            temperature: config.temperature,
            preferredAgent: config.preferredAgent,
            fallbackAgents: config.fallbackAgents,
            budgetPriority: config.budgetPriority,
            keywords: config.keywords.slice(0, 10), // Limit to first 10 keywords
          },
        })
      }

      // List all categories
      const categories = getAllCategories().map((cat) => {
        const config = getCategoryConfig(cat)
        return {
          category: cat,
          name: config.name,
          model: config.model,
          temperature: config.temperature,
          preferredAgent: config.preferredAgent,
          budgetPriority: config.budgetPriority,
        }
      })

      return JSON.stringify({
        success: true,
        categories,
      })
    },
  })

  return {
    analyze_complexity,
    recommend_agent,
    route_task,
    route_to_category,
    list_categories,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type RoutingTools = ReturnType<typeof createRoutingTools>
