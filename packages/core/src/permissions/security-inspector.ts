/**
 * Security Inspector
 * Pattern-based threat detection with confidence scores
 *
 * Inspired by Goose's security inspector. Detects suspicious patterns in
 * tool calls before they execute.
 */

import type { RiskLevel } from './types.js'

// ============================================================================
// Types
// ============================================================================

/** Threat categories */
export type ThreatCategory =
  | 'file_access'
  | 'command_injection'
  | 'data_exfiltration'
  | 'privilege_escalation'
  | 'resource_abuse'

/** Result of a security inspection */
export interface SecurityResult {
  /** Whether the tool call should be blocked */
  blocked: boolean
  /** Confidence score (0-1) */
  confidence: number
  /** Threat category if detected */
  category?: ThreatCategory
  /** Risk level */
  risk: RiskLevel
  /** Human-readable reason */
  reason: string
}

/** A security pattern to match against */
export interface SecurityPattern {
  /** Pattern to match (regex or string match) */
  pattern: RegExp
  /** Which tool parameter to match against */
  field: string
  /** Threat category */
  category: ThreatCategory
  /** Risk level when matched */
  risk: RiskLevel
  /** Confidence score (0-1) */
  confidence: number
  /** Reason for flagging */
  reason: string
  /** Whether to block (true) or warn (false) */
  block: boolean
}

// ============================================================================
// Built-in Patterns
// ============================================================================

export const SECURITY_PATTERNS: SecurityPattern[] = [
  // Command injection
  {
    pattern: /;\s*rm\s+-rf/,
    field: 'command',
    category: 'command_injection',
    risk: 'critical',
    confidence: 0.95,
    reason: 'Command chain with destructive rm -rf',
    block: true,
  },
  {
    pattern: /\$\(.*\)|`.*`/,
    field: 'command',
    category: 'command_injection',
    risk: 'medium',
    confidence: 0.6,
    reason: 'Command substitution detected',
    block: false,
  },
  {
    pattern: /\|\s*sh\b|\|\s*bash\b/,
    field: 'command',
    category: 'command_injection',
    risk: 'high',
    confidence: 0.85,
    reason: 'Piped execution to shell',
    block: true,
  },
  {
    pattern: /eval\s+/,
    field: 'command',
    category: 'command_injection',
    risk: 'high',
    confidence: 0.8,
    reason: 'Dynamic eval execution',
    block: true,
  },
  {
    pattern: /:()\s*{\s*:|:&\s*}\s*;/,
    field: 'command',
    category: 'resource_abuse',
    risk: 'critical',
    confidence: 0.99,
    reason: 'Fork bomb detected',
    block: true,
  },

  // Privilege escalation
  {
    pattern: /chmod\s+[47]77/,
    field: 'command',
    category: 'privilege_escalation',
    risk: 'high',
    confidence: 0.8,
    reason: 'Insecure permission change (world-writable)',
    block: false,
  },
  {
    pattern: /chown\s+-R\s+root/,
    field: 'command',
    category: 'privilege_escalation',
    risk: 'high',
    confidence: 0.85,
    reason: 'Recursive ownership change to root',
    block: true,
  },

  // Data exfiltration
  {
    pattern: /curl.*-d\s+@|curl.*--data-binary\s+@/,
    field: 'command',
    category: 'data_exfiltration',
    risk: 'high',
    confidence: 0.75,
    reason: 'Uploading file contents via curl',
    block: false,
  },
  {
    pattern: /base64.*\|\s*curl|curl.*\|\s*base64/,
    field: 'command',
    category: 'data_exfiltration',
    risk: 'high',
    confidence: 0.85,
    reason: 'Base64 encoding with network transfer',
    block: true,
  },

  // File access — sensitive paths
  {
    pattern: /\/etc\/shadow|\/etc\/passwd/,
    field: 'path',
    category: 'file_access',
    risk: 'critical',
    confidence: 0.95,
    reason: 'Access to system credential files',
    block: true,
  },
  {
    pattern: /\.ssh\/id_rsa|\.ssh\/id_ed25519|\.gnupg\//,
    field: 'path',
    category: 'file_access',
    risk: 'critical',
    confidence: 0.95,
    reason: 'Access to private keys',
    block: true,
  },
  {
    pattern: /\.env\.production|\.env\.local/,
    field: 'path',
    category: 'file_access',
    risk: 'medium',
    confidence: 0.7,
    reason: 'Access to production environment variables',
    block: false,
  },

  // Resource abuse
  {
    pattern: /while\s+true|for\s*\(\s*;\s*;\s*\)/,
    field: 'command',
    category: 'resource_abuse',
    risk: 'medium',
    confidence: 0.6,
    reason: 'Potential infinite loop',
    block: false,
  },
  {
    pattern: /dd\s+.*if=\/dev\/zero|dd\s+.*of=\/dev\//,
    field: 'command',
    category: 'resource_abuse',
    risk: 'critical',
    confidence: 0.95,
    reason: 'Direct disk operations',
    block: true,
  },
]

// ============================================================================
// Security Inspector
// ============================================================================

/**
 * Inspects tool calls for security threats
 */
export class SecurityInspector {
  private patterns: SecurityPattern[]
  private blockThreshold: number

  constructor(patterns: SecurityPattern[] = SECURITY_PATTERNS, blockThreshold = 0.7) {
    this.patterns = patterns
    this.blockThreshold = blockThreshold
  }

  /**
   * Inspect a tool call for security threats
   */
  inspect(tool: string, params: Record<string, unknown>): SecurityResult {
    let highestRisk: SecurityResult = {
      blocked: false,
      confidence: 0,
      risk: 'low',
      reason: 'No threats detected',
    }

    for (const pattern of this.patterns) {
      const value = this.extractField(tool, params, pattern.field)
      if (!value) continue

      if (pattern.pattern.test(value)) {
        const shouldBlock = pattern.block && pattern.confidence >= this.blockThreshold

        if (pattern.confidence > highestRisk.confidence) {
          highestRisk = {
            blocked: shouldBlock,
            confidence: pattern.confidence,
            category: pattern.category,
            risk: pattern.risk,
            reason: pattern.reason,
          }
        }
      }
    }

    return highestRisk
  }

  /**
   * Extract the field value to match against
   */
  private extractField(
    tool: string,
    params: Record<string, unknown>,
    field: string
  ): string | null {
    // Direct param match
    if (field in params && typeof params[field] === 'string') {
      return params[field] as string
    }

    // Tool-specific field extraction
    if (field === 'command' && tool === 'bash' && typeof params.command === 'string') {
      return params.command
    }

    if (field === 'path') {
      // Try common path field names
      for (const key of ['path', 'file_path', 'filePath', 'file', 'target']) {
        if (typeof params[key] === 'string') return params[key] as string
      }
    }

    return null
  }

  /**
   * Add custom patterns
   */
  addPattern(pattern: SecurityPattern): void {
    this.patterns.push(pattern)
  }

  /**
   * Get all patterns
   */
  getPatterns(): SecurityPattern[] {
    return [...this.patterns]
  }

  /**
   * Set block threshold
   */
  setBlockThreshold(threshold: number): void {
    this.blockThreshold = Math.max(0, Math.min(1, threshold))
  }

  /**
   * Get block threshold
   */
  getBlockThreshold(): number {
    return this.blockThreshold
  }
}
