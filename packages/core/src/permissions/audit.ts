/**
 * Audit Trail
 * Records every inspector decision for transparency and debugging
 */

// ============================================================================
// Types
// ============================================================================

/** Decision made by an inspector */
export type AuditDecision = 'allow' | 'block' | 'warn'

/** A single audit entry */
export interface AuditEntry {
  /** Timestamp of the decision */
  timestamp: number
  /** Tool that was inspected */
  tool: string
  /** Tool parameters (sanitized) */
  params: Record<string, unknown>
  /** Inspector that made the decision */
  inspector: string
  /** Decision made */
  decision: AuditDecision
  /** Confidence score (0-1) */
  confidence: number
  /** Reason for the decision */
  reason: string
  /** Optional threat category */
  category?: string
  /** Session ID */
  sessionId?: string
}

/** Filter for querying audit entries */
export interface AuditFilter {
  tool?: string
  inspector?: string
  decision?: AuditDecision
  sessionId?: string
  since?: number
  category?: string
}

// ============================================================================
// AuditTrail
// ============================================================================

/**
 * Records inspector decisions for transparency and debugging
 */
export class AuditTrail {
  private entries: AuditEntry[] = []
  private maxEntries: number

  constructor(maxEntries = 1000) {
    this.maxEntries = maxEntries
  }

  /**
   * Record an audit entry
   */
  record(entry: Omit<AuditEntry, 'timestamp'>): AuditEntry {
    const full: AuditEntry = { ...entry, timestamp: Date.now() }
    this.entries.push(full)

    // Trim old entries
    if (this.entries.length > this.maxEntries) {
      this.entries = this.entries.slice(-this.maxEntries)
    }

    return full
  }

  /**
   * Query entries with optional filters
   */
  query(filter?: AuditFilter): AuditEntry[] {
    if (!filter) return [...this.entries]

    return this.entries.filter((entry) => {
      if (filter.tool && entry.tool !== filter.tool) return false
      if (filter.inspector && entry.inspector !== filter.inspector) return false
      if (filter.decision && entry.decision !== filter.decision) return false
      if (filter.sessionId && entry.sessionId !== filter.sessionId) return false
      if (filter.since && entry.timestamp < filter.since) return false
      if (filter.category && entry.category !== filter.category) return false
      return true
    })
  }

  /**
   * Get all blocked entries
   */
  getBlocked(): AuditEntry[] {
    return this.entries.filter((e) => e.decision === 'block')
  }

  /**
   * Get all warnings
   */
  getWarnings(): AuditEntry[] {
    return this.entries.filter((e) => e.decision === 'warn')
  }

  /**
   * Get count of entries
   */
  get count(): number {
    return this.entries.length
  }

  /**
   * Export all entries
   */
  export(): AuditEntry[] {
    return [...this.entries]
  }

  /**
   * Clear all entries
   */
  clear(): void {
    this.entries = []
  }
}

// ============================================================================
// Singleton
// ============================================================================

let _auditTrail: AuditTrail | null = null

export function getAuditTrail(): AuditTrail {
  if (!_auditTrail) _auditTrail = new AuditTrail()
  return _auditTrail
}

export function setAuditTrail(trail: AuditTrail | null): void {
  _auditTrail = trail
}
