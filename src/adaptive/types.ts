/**
 * Adaptive Council Types
 *
 * System for learning which oracles perform best for different task types.
 */

import { z } from 'zod'

// =============================================================================
// Task Categories for Learning
// =============================================================================

export const taskCategorySchema = z.enum([
  'architecture',
  'algorithm',
  'ui_frontend',
  'api_backend',
  'database',
  'testing',
  'security',
  'performance',
  'documentation',
  'refactoring',
  'debugging',
  'devops',
  'general',
])

export type TaskCategory = z.infer<typeof taskCategorySchema>

// =============================================================================
// Oracle Performance Record
// =============================================================================

export const oraclePerformanceSchema = z.object({
  oracleId: z.string(),
  category: taskCategorySchema,
  totalConsultations: z.number().int().default(0),
  successfulRecommendations: z.number().int().default(0),
  averageConfidence: z.number().min(0).max(1).default(0.5),
  averageResponseTime: z.number().default(0), // ms
  consensusRate: z.number().min(0).max(1).default(0.5), // How often oracle agrees with final decision
  lastUpdated: z.string(),
  performanceScore: z.number().min(0).max(100).default(50),
})

export type OraclePerformance = z.infer<typeof oraclePerformanceSchema>

// =============================================================================
// Adaptive Council Configuration
// =============================================================================

export const adaptiveConfigSchema = z.object({
  /** Enable adaptive learning */
  enabled: z.boolean().default(true),
  /** Minimum consultations before using learned weights */
  minSamplesForAdaptation: z.number().int().min(3).max(50).default(10),
  /** Learning rate (how fast weights change) */
  learningRate: z.number().min(0.01).max(0.5).default(0.1),
  /** Decay rate for old performance data (per day) */
  decayRate: z.number().min(0).max(0.1).default(0.02),
  /** Weight for recency in performance calculation */
  recencyWeight: z.number().min(0).max(1).default(0.3),
  /** Enable dynamic oracle selection */
  dynamicSelection: z.boolean().default(true),
  /** Exploration rate (chance to try non-optimal oracle) */
  explorationRate: z.number().min(0).max(0.3).default(0.1),
})

export type AdaptiveConfig = z.infer<typeof adaptiveConfigSchema>

// =============================================================================
// Consultation Record
// =============================================================================

export const consultationRecordSchema = z.object({
  id: z.string(),
  missionId: z.string(),
  taskId: z.string(),
  category: taskCategorySchema,
  oracleId: z.string(),
  recommendation: z.string(),
  confidence: z.number().min(0).max(1),
  responseTime: z.number(), // ms
  wasAccepted: z.boolean().optional(), // Was recommendation followed?
  taskOutcome: z.enum(['success', 'failure', 'partial', 'unknown']).optional(),
  consensusMatch: z.boolean().optional(), // Did oracle match consensus?
  timestamp: z.string(),
})

export type ConsultationRecord = z.infer<typeof consultationRecordSchema>

// =============================================================================
// Oracle Selection Result
// =============================================================================

export interface OracleSelectionResult {
  selectedOracles: string[]
  weights: Record<string, number>
  reason: string
  isExploration: boolean
  categoryScores: Record<string, Record<string, number>>
}

// =============================================================================
// Performance Update
// =============================================================================

export interface PerformanceUpdate {
  oracleId: string
  category: TaskCategory
  wasSuccessful: boolean
  confidence: number
  responseTime: number
  matchedConsensus: boolean
}

// =============================================================================
// Default Configuration
// =============================================================================

export const DEFAULT_ADAPTIVE_CONFIG: AdaptiveConfig = {
  enabled: true,
  minSamplesForAdaptation: 10,
  learningRate: 0.1,
  decayRate: 0.02,
  recencyWeight: 0.3,
  dynamicSelection: true,
  explorationRate: 0.1,
}

// =============================================================================
// Category Keywords (for auto-detection)
// =============================================================================

export const CATEGORY_KEYWORDS: Record<TaskCategory, string[]> = {
  architecture: ['architecture', 'design', 'structure', 'pattern', 'system', 'module', 'component'],
  algorithm: ['algorithm', 'sort', 'search', 'optimization', 'complexity', 'data structure'],
  ui_frontend: [
    'ui',
    'frontend',
    'component',
    'react',
    'vue',
    'css',
    'style',
    'layout',
    'responsive',
  ],
  api_backend: ['api', 'endpoint', 'route', 'controller', 'service', 'backend', 'rest', 'graphql'],
  database: ['database', 'query', 'migration', 'schema', 'sql', 'nosql', 'orm', 'prisma'],
  testing: ['test', 'spec', 'mock', 'stub', 'coverage', 'unit', 'integration', 'e2e'],
  security: ['security', 'auth', 'authentication', 'authorization', 'jwt', 'oauth', 'encryption'],
  performance: ['performance', 'optimize', 'cache', 'speed', 'memory', 'latency', 'benchmark'],
  documentation: ['docs', 'documentation', 'readme', 'comment', 'jsdoc', 'api docs'],
  refactoring: ['refactor', 'cleanup', 'restructure', 'rename', 'extract', 'simplify'],
  debugging: ['debug', 'bug', 'fix', 'error', 'issue', 'crash', 'exception'],
  devops: ['deploy', 'ci', 'cd', 'docker', 'kubernetes', 'pipeline', 'infrastructure'],
  general: [],
}
