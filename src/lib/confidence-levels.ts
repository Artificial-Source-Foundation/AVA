/**
 * Delta9 Confidence Level Constants
 *
 * Centralized confidence thresholds and helpers for consistent
 * scoring across the codebase.
 */

// =============================================================================
// Constants
// =============================================================================

/**
 * Standard confidence thresholds used throughout Delta9
 */
export const CONFIDENCE = {
  /** Very high confidence (90%+) */
  VERY_HIGH: 0.9,
  /** High confidence (70%+) */
  HIGH: 0.7,
  /** Moderate confidence (50%+) */
  MODERATE: 0.5,
  /** Low confidence (30%+) */
  LOW: 0.3,
  /** Minimum acceptable confidence */
  MINIMUM: 0.3,
  /** Maximum confidence cap */
  MAX: 0.95,
  /** Threshold for blending multiple results */
  BLEND_THRESHOLD: 0.6,
  /** Secondary match threshold */
  SECONDARY_THRESHOLD: 0.4,
} as const

// =============================================================================
// Types
// =============================================================================

/**
 * Human-readable confidence level labels
 */
export type ConfidenceLevel = 'Very High' | 'High' | 'Moderate' | 'Low' | 'Very Low'

// =============================================================================
// Helpers
// =============================================================================

/**
 * Convert a numeric confidence score to a human-readable label
 *
 * @param confidence - Numeric confidence value (0-1)
 * @returns Human-readable confidence level
 */
export function getConfidenceLabel(confidence: number): ConfidenceLevel {
  if (confidence >= CONFIDENCE.VERY_HIGH) return 'Very High'
  if (confidence >= CONFIDENCE.HIGH) return 'High'
  if (confidence >= CONFIDENCE.MODERATE) return 'Moderate'
  if (confidence >= CONFIDENCE.LOW) return 'Low'
  return 'Very Low'
}

/**
 * Check if confidence is at or above the high threshold
 *
 * @param confidence - Numeric confidence value (0-1)
 * @returns True if confidence is high or very high
 */
export function isHighConfidence(confidence: number): boolean {
  return confidence >= CONFIDENCE.HIGH
}

/**
 * Check if confidence meets minimum threshold
 *
 * @param confidence - Numeric confidence value (0-1)
 * @returns True if confidence is acceptable
 */
export function meetsMinimumConfidence(confidence: number): boolean {
  return confidence >= CONFIDENCE.MINIMUM
}

/**
 * Clamp confidence to valid range [0, MAX]
 *
 * @param confidence - Raw confidence value
 * @returns Clamped confidence value
 */
export function clampConfidence(confidence: number): number {
  return Math.max(0, Math.min(CONFIDENCE.MAX, confidence))
}
