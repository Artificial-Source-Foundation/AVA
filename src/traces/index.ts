/**
 * Delta9 Decision Trace Module
 *
 * Structured recording of WHY decisions were made.
 */

// Types
export {
  DecisionTypeSchema,
  DecisionTraceSchema,
  TraceQuerySchema,
  TraceResultSchema,
  PrecedentChainSchema,
  TraceStatsSchema,
  type DecisionType,
  type DecisionTrace,
  type TraceQuery,
  type TraceResult,
  type PrecedentChain,
  type TraceStats,
  type CreateTraceInput,
} from './types.js'

// Store
export { TraceStore, getTraceStore, resetTraceStore, type TraceStoreOptions } from './store.js'
