/**
 * Inspector Pipeline
 * Three-stage inspection chain for tool calls
 *
 * Pipeline: SecurityInspector → PermissionCheck → RepetitionInspector
 * First blocker stops the chain. All decisions are recorded in AuditTrail.
 *
 * Inspired by Goose's 3-inspector architecture.
 */

import type { AuditDecision, AuditTrail } from './audit.js'
import { getAuditTrail } from './audit.js'
import type { RepetitionInspector } from './repetition-inspector.js'
import type { SecurityInspector } from './security-inspector.js'

// ============================================================================
// Types
// ============================================================================

/** Result of the full pipeline inspection */
export interface PipelineResult {
  /** Whether the tool call is allowed */
  allowed: boolean
  /** Which inspector blocked (if any) */
  blockedBy?: string
  /** Reason for blocking */
  reason: string
  /** Combined confidence (highest from blockers) */
  confidence: number
  /** All inspector results in order */
  results: InspectorResult[]
}

/** Result from a single inspector */
export interface InspectorResult {
  inspector: string
  decision: AuditDecision
  confidence: number
  reason: string
  category?: string
}

/** An inspector that can be plugged into the pipeline */
export interface Inspector {
  /** Inspector name (for audit trail) */
  name: string
  /** Inspect a tool call */
  inspect(tool: string, params: Record<string, unknown>): InspectorResult
}

// ============================================================================
// Inspector Adapters
// ============================================================================

/**
 * Wrap SecurityInspector as a pipeline Inspector
 */
export function securityAdapter(inspector: SecurityInspector): Inspector {
  return {
    name: 'security',
    inspect(tool: string, params: Record<string, unknown>): InspectorResult {
      const result = inspector.inspect(tool, params)
      return {
        inspector: 'security',
        decision: result.blocked ? 'block' : result.confidence > 0.5 ? 'warn' : 'allow',
        confidence: result.confidence,
        reason: result.reason,
        category: result.category,
      }
    },
  }
}

/**
 * Wrap RepetitionInspector as a pipeline Inspector
 */
export function repetitionAdapter(inspector: RepetitionInspector): Inspector {
  return {
    name: 'repetition',
    inspect(tool: string, params: Record<string, unknown>): InspectorResult {
      const result = inspector.check(tool, params)
      return {
        inspector: 'repetition',
        decision: result.detected ? 'block' : 'allow',
        confidence: result.detected ? 0.9 : 0,
        reason: result.reason || 'No repetition detected',
      }
    },
  }
}

// ============================================================================
// Pipeline
// ============================================================================

/**
 * Inspector pipeline that runs inspectors in order
 * First blocker stops the chain
 */
export class InspectorPipeline {
  private inspectors: Inspector[] = []
  private auditTrail: AuditTrail

  constructor(auditTrail?: AuditTrail) {
    this.auditTrail = auditTrail ?? getAuditTrail()
  }

  /**
   * Add an inspector to the pipeline
   */
  addInspector(inspector: Inspector): void {
    this.inspectors.push(inspector)
  }

  /**
   * Remove an inspector by name
   */
  removeInspector(name: string): boolean {
    const before = this.inspectors.length
    this.inspectors = this.inspectors.filter((i) => i.name !== name)
    return this.inspectors.length < before
  }

  /**
   * Get all inspector names
   */
  getInspectorNames(): string[] {
    return this.inspectors.map((i) => i.name)
  }

  /**
   * Run the pipeline on a tool call
   */
  inspect(tool: string, params: Record<string, unknown>, sessionId?: string): PipelineResult {
    const results: InspectorResult[] = []
    let blocked = false
    let blockedBy: string | undefined
    let reason = 'Allowed'
    let highestConfidence = 0

    for (const inspector of this.inspectors) {
      const result = inspector.inspect(tool, params)
      results.push(result)

      // Record in audit trail
      this.auditTrail.record({
        tool,
        params,
        inspector: result.inspector,
        decision: result.decision,
        confidence: result.confidence,
        reason: result.reason,
        category: result.category,
        sessionId,
      })

      if (result.decision === 'block') {
        blocked = true
        blockedBy = result.inspector
        reason = result.reason
        highestConfidence = Math.max(highestConfidence, result.confidence)
        break // First blocker stops the chain
      }

      highestConfidence = Math.max(highestConfidence, result.confidence)
    }

    return {
      allowed: !blocked,
      blockedBy,
      reason,
      confidence: highestConfidence,
      results,
    }
  }

  /**
   * Get inspector count
   */
  get length(): number {
    return this.inspectors.length
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a default pipeline: Security → Repetition
 * (Permission checks remain in the existing system for now)
 */
export function createDefaultPipeline(
  security: SecurityInspector,
  repetition: RepetitionInspector,
  auditTrail?: AuditTrail
): InspectorPipeline {
  const pipeline = new InspectorPipeline(auditTrail)
  pipeline.addInspector(securityAdapter(security))
  pipeline.addInspector(repetitionAdapter(repetition))
  return pipeline
}
