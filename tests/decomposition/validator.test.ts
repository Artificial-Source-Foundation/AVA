/**
 * Delta9 Decomposition Validator Tests
 */

import { describe, it, expect } from 'vitest'
import {
  DecompositionValidator,
  type Decomposition,
  type Subtask,
} from '../../src/decomposition/index.js'

// =============================================================================
// Test Helpers
// =============================================================================

function createValidDecomposition(): Decomposition {
  return {
    id: 'decomp-1',
    parentTaskId: 'task-1',
    taskDescription: 'Implement user authentication',
    strategy: 'feature_based',
    subtasks: [
      {
        id: 'sub-1',
        title: 'Create user model',
        description: 'Add User schema with email and password fields',
        estimatedComplexity: 'low',
        order: 1,
        files: ['src/models/user.ts'],
        acceptanceCriteria: ['User model exports correctly', 'Has password hashing'],
      },
      {
        id: 'sub-2',
        title: 'Create auth service',
        description: 'Add authentication service with login/logout methods',
        estimatedComplexity: 'medium',
        order: 2,
        files: ['src/services/auth.ts'],
        dependencies: ['sub-1'],
        acceptanceCriteria: ['Login returns JWT', 'Logout invalidates token'],
      },
    ],
    totalEstimatedComplexity: 'medium',
    createdAt: new Date().toISOString(),
  }
}

// =============================================================================
// Validator Tests
// =============================================================================

describe('DecompositionValidator', () => {
  const validator = new DecompositionValidator()

  describe('validate()', () => {
    it('should pass for valid decomposition', () => {
      const decomposition = createValidDecomposition()
      const result = validator.validate(decomposition)

      expect(result.passed).toBe(true)
      expect(result.score).toBeGreaterThan(0.7)
    })

    it('should return score between 0 and 1', () => {
      const decomposition = createValidDecomposition()
      const result = validator.validate(decomposition)

      expect(result.score).toBeGreaterThanOrEqual(0)
      expect(result.score).toBeLessThanOrEqual(1)
    })
  })

  describe('circular dependency detection', () => {
    it('should detect circular dependencies', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].dependencies = ['sub-2'] // Creates cycle: sub-1 -> sub-2 -> sub-1

      const result = validator.validate(decomposition)

      const circularIssue = result.issues.find(i => i.type === 'circular_dep')
      expect(circularIssue).toBeDefined()
      expect(circularIssue!.severity).toBe('error')
    })

    it('should detect self-dependency', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].dependencies = ['sub-1'] // Self-dependency

      const result = validator.validate(decomposition)

      const selfDepIssue = result.issues.find(i => i.type === 'self_dep')
      expect(selfDepIssue).toBeDefined()
      expect(selfDepIssue!.severity).toBe('error')
    })

    it('should detect missing dependency reference', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].dependencies = ['non-existent-id']

      const result = validator.validate(decomposition)

      const missingDepIssue = result.issues.find(i => i.type === 'missing_deps')
      expect(missingDepIssue).toBeDefined()
      expect(missingDepIssue!.severity).toBe('error')
    })
  })

  describe('file overlap detection', () => {
    it('should warn about overlapping files', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[1].files = ['src/models/user.ts'] // Same as sub-1

      const result = validator.validate(decomposition)

      const overlapIssue = result.issues.find(i => i.type === 'overlapping_files')
      expect(overlapIssue).toBeDefined()
      expect(overlapIssue!.severity).toBe('warning')
    })
  })

  describe('acceptance criteria validation', () => {
    it('should warn about missing acceptance criteria', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].acceptanceCriteria = []

      const result = validator.validate(decomposition)

      const missingCriteriaIssue = result.issues.find(i => i.type === 'missing_criteria')
      expect(missingCriteriaIssue).toBeDefined()
      expect(missingCriteriaIssue!.severity).toBe('warning')
    })

    it('should warn about vague acceptance criteria', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].acceptanceCriteria = ['works', 'done']

      const result = validator.validate(decomposition)

      const vagueIssue = result.issues.find(i => i.type === 'too_vague')
      expect(vagueIssue).toBeDefined()
    })
  })

  describe('granularity validation', () => {
    it('should warn about subtasks that are too large', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].estimatedComplexity = 'high'
      decomposition.subtasks[0].files = [
        'file1.ts', 'file2.ts', 'file3.ts', 'file4.ts', 'file5.ts', 'file6.ts'
      ]

      const result = validator.validate(decomposition)

      const tooLargeIssue = result.issues.find(i => i.type === 'too_large')
      expect(tooLargeIssue).toBeDefined()
      expect(tooLargeIssue!.severity).toBe('warning')
    })

    it('should warn about overly long descriptions', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].description = 'A'.repeat(600)

      const result = validator.validate(decomposition)

      const tooLargeIssue = result.issues.find(i => i.type === 'too_large')
      expect(tooLargeIssue).toBeDefined()
    })
  })

  describe('description clarity validation', () => {
    it('should warn about short titles', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].title = 'Fix'

      const result = validator.validate(decomposition)

      const vagueIssue = result.issues.find(i => i.type === 'too_vague' && i.subtaskId === 'sub-1')
      expect(vagueIssue).toBeDefined()
    })

    it('should warn about short descriptions', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].description = 'Do it'

      const result = validator.validate(decomposition)

      const vagueIssue = result.issues.find(i => i.type === 'too_vague')
      expect(vagueIssue).toBeDefined()
    })

    it('should warn about placeholder text', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks[0].description = 'TODO: fill in details later'

      const result = validator.validate(decomposition)

      const vagueIssue = result.issues.find(i => i.type === 'too_vague' && i.message.includes('placeholder'))
      expect(vagueIssue).toBeDefined()
    })
  })

  describe('subtask count validation', () => {
    it('should error on empty subtasks', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks = []

      const result = validator.validate(decomposition)

      const emptyIssue = result.issues.find(i => i.type === 'too_vague' && i.message.includes('no subtasks'))
      expect(emptyIssue).toBeDefined()
      expect(emptyIssue!.severity).toBe('error')
    })

    it('should suggest review for single subtask', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks = [decomposition.subtasks[0]]

      const result = validator.validate(decomposition)

      expect(result.suggestions.length).toBeGreaterThan(0)
      expect(result.suggestions.some(s => s.includes('truly needs decomposition'))).toBe(true)
    })

    it('should warn about too many subtasks', () => {
      const decomposition = createValidDecomposition()
      decomposition.subtasks = Array.from({ length: 15 }, (_, i) => ({
        id: `sub-${i}`,
        title: `Subtask ${i}`,
        description: 'A reasonable description for this subtask',
        estimatedComplexity: 'low' as const,
        order: i + 1,
        acceptanceCriteria: ['Criterion 1', 'Criterion 2'],
      }))

      const result = validator.validate(decomposition)

      const tooManyIssue = result.issues.find(i => i.type === 'too_large' && i.message.includes('subtasks'))
      expect(tooManyIssue).toBeDefined()
    })
  })

  describe('file-based strategy validation', () => {
    it('should warn about file-based strategy without files', () => {
      const decomposition = createValidDecomposition()
      decomposition.strategy = 'file_based'
      decomposition.subtasks.forEach(s => {
        delete s.files
      })

      const result = validator.validate(decomposition)

      const noFilesIssue = result.issues.find(i => i.type === 'no_files')
      expect(noFilesIssue).toBeDefined()
      expect(result.suggestions.some(s => s.includes('file-based'))).toBe(true)
    })
  })

  describe('scoring', () => {
    it('should reduce score for errors', () => {
      const validDecomposition = createValidDecomposition()
      const validResult = validator.validate(validDecomposition)

      const invalidDecomposition = createValidDecomposition()
      invalidDecomposition.subtasks[0].dependencies = ['sub-1'] // Self-dep error
      const invalidResult = validator.validate(invalidDecomposition)

      expect(invalidResult.score).toBeLessThan(validResult.score)
    })

    it('should reduce score for warnings', () => {
      const cleanDecomposition = createValidDecomposition()
      const cleanResult = validator.validate(cleanDecomposition)

      const warningDecomposition = createValidDecomposition()
      warningDecomposition.subtasks[0].acceptanceCriteria = []
      const warningResult = validator.validate(warningDecomposition)

      expect(warningResult.score).toBeLessThan(cleanResult.score)
    })
  })
})
