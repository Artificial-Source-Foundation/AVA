/**
 * Delta9 Routing Module
 *
 * Task routing, complexity detection, and agent dispatch.
 */

export {
  analyzeComplexity,
  complexityToCouncilMode,
  shouldTriggerCouncil,
  describeComplexity,
  type ComplexityAnalysis,
} from './complexity.js'

export {
  routeTask,
  canAgentModifyFiles,
  isSupportAgent,
  getAvailableAgents,
  describeRouteDecision,
  type RoutableAgent,
  type RouteDecision,
  type TaskRouterInput,
} from './task-router.js'

export {
  detectCategory,
  routeToCategory,
  getCategoryConfig,
  getAllCategories,
  isValidCategory,
  describeCategoryRoute,
  getCategoryBudgetAllowance,
  getCategoryTemperatureRange,
  DEFAULT_CATEGORY_CONFIGS,
  type TaskCategory,
  type CategoryConfig,
  type CategoryMatch,
  type CategoryRouteResult,
} from './categories.js'
