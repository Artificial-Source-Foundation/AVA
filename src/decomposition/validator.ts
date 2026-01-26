/**
 * Delta9 Task Decomposition Validator
 *
 * Validates decomposition quality by checking:
 * - Circular dependencies
 * - File overlaps
 * - Acceptance criteria coverage
 * - Subtask granularity
 * - Description clarity
 */

import {
  type Decomposition,
  type DecompositionQuality,
  type ValidationIssue,
  type DecompositionEngineConfig,
  DEFAULT_DECOMPOSITION_CONFIG,
} from './types.js'

// =============================================================================
// Validator Class
// =============================================================================

export class DecompositionValidator {
  private config: Required<DecompositionEngineConfig>

  constructor(config?: DecompositionEngineConfig) {
    this.config = { ...DEFAULT_DECOMPOSITION_CONFIG, ...config }
  }

  /**
   * Validate a decomposition and return quality report
   */
  validate(decomposition: Decomposition): DecompositionQuality {
    const issues: ValidationIssue[] = []
    const suggestions: string[] = []

    // Run all checks
    this.checkCircularDeps(decomposition, issues)
    this.checkFileOverlaps(decomposition, issues, suggestions)
    this.checkAcceptanceCriteria(decomposition, issues, suggestions)
    this.checkGranularity(decomposition, issues, suggestions)
    this.checkDescriptions(decomposition, issues, suggestions)
    this.checkDependencyValidity(decomposition, issues)
    this.checkSubtaskCount(decomposition, issues, suggestions)

    // Calculate score based on issues
    const score = this.calculateScore(issues, decomposition.subtasks.length)

    return {
      score,
      issues,
      suggestions,
      passed: score >= this.config.minQualityScore,
    }
  }

  // ===========================================================================
  // Circular Dependency Check
  // ===========================================================================

  private checkCircularDeps(decomposition: Decomposition, issues: ValidationIssue[]): void {
    const subtaskIds = new Set(decomposition.subtasks.map((s) => s.id))
    const visited = new Set<string>()
    const recursionStack = new Set<string>()

    const hasCycle = (subtaskId: string, path: string[]): string[] | null => {
      if (recursionStack.has(subtaskId)) {
        // Found a cycle, return the path
        const cycleStart = path.indexOf(subtaskId)
        return path.slice(cycleStart)
      }

      if (visited.has(subtaskId)) {
        return null
      }

      visited.add(subtaskId)
      recursionStack.add(subtaskId)

      const subtask = decomposition.subtasks.find((s) => s.id === subtaskId)
      if (subtask?.dependencies) {
        for (const depId of subtask.dependencies) {
          if (subtaskIds.has(depId)) {
            const cycle = hasCycle(depId, [...path, subtaskId])
            if (cycle) {
              return cycle
            }
          }
        }
      }

      recursionStack.delete(subtaskId)
      return null
    }

    for (const subtask of decomposition.subtasks) {
      visited.clear()
      recursionStack.clear()
      const cycle = hasCycle(subtask.id, [])
      if (cycle) {
        issues.push({
          type: 'circular_dep',
          subtaskId: subtask.id,
          message: `Circular dependency detected: ${cycle.join(' -> ')} -> ${cycle[0]}`,
          severity: 'error',
        })
        break // Only report one cycle
      }
    }
  }

  // ===========================================================================
  // File Overlap Check
  // ===========================================================================

  private checkFileOverlaps(
    decomposition: Decomposition,
    issues: ValidationIssue[],
    suggestions: string[]
  ): void {
    const fileOwners = new Map<string, string[]>()

    // Build map of files to subtasks that modify them
    for (const subtask of decomposition.subtasks) {
      if (subtask.files) {
        for (const file of subtask.files) {
          const owners = fileOwners.get(file) || []
          owners.push(subtask.id)
          fileOwners.set(file, owners)
        }
      }
    }

    // Check for overlaps
    for (const [file, owners] of fileOwners) {
      if (owners.length > 1) {
        issues.push({
          type: 'overlapping_files',
          message: `File "${file}" is modified by multiple subtasks: ${owners.join(', ')}`,
          severity: 'warning',
        })
      }
    }

    if (fileOwners.size > 0 && issues.some((i) => i.type === 'overlapping_files')) {
      suggestions.push(
        'Consider adding dependencies between subtasks that modify the same file, or consolidate them into one subtask.'
      )
    }
  }

  // ===========================================================================
  // Acceptance Criteria Check
  // ===========================================================================

  private checkAcceptanceCriteria(
    decomposition: Decomposition,
    issues: ValidationIssue[],
    suggestions: string[]
  ): void {
    let missingCount = 0

    for (const subtask of decomposition.subtasks) {
      if (
        !subtask.acceptanceCriteria ||
        subtask.acceptanceCriteria.length < this.config.minAcceptanceCriteria
      ) {
        issues.push({
          type: 'missing_criteria',
          subtaskId: subtask.id,
          message: `Subtask "${subtask.title}" has insufficient acceptance criteria (has ${subtask.acceptanceCriteria?.length || 0}, needs ${this.config.minAcceptanceCriteria})`,
          severity: 'warning',
        })
        missingCount++
      }

      // Check for vague criteria
      if (subtask.acceptanceCriteria) {
        for (const criterion of subtask.acceptanceCriteria) {
          if (this.isVagueCriterion(criterion)) {
            issues.push({
              type: 'too_vague',
              subtaskId: subtask.id,
              message: `Acceptance criterion is too vague: "${criterion}"`,
              severity: 'warning',
            })
          }
        }
      }
    }

    if (missingCount > 0) {
      suggestions.push(
        `Add specific, measurable acceptance criteria to ${missingCount} subtask(s).`
      )
    }
  }

  private isVagueCriterion(criterion: string): boolean {
    const vaguePatterns = [
      /^works?$/i,
      /^done$/i,
      /^complete$/i,
      /^finished$/i,
      /^implemented$/i,
      /^it works$/i,
      /^make it work$/i,
      /^should work$/i,
    ]

    const normalized = criterion.trim()
    if (normalized.length < 10) {
      return true
    }

    return vaguePatterns.some((p) => p.test(normalized))
  }

  // ===========================================================================
  // Granularity Check
  // ===========================================================================

  private checkGranularity(
    decomposition: Decomposition,
    issues: ValidationIssue[],
    suggestions: string[]
  ): void {
    let tooLargeCount = 0

    for (const subtask of decomposition.subtasks) {
      // Check complexity
      if (subtask.estimatedComplexity === 'high') {
        const fileCount = (subtask.files?.length || 0) + (subtask.filesReadonly?.length || 0)
        const criteriaCount = subtask.acceptanceCriteria?.length || 0

        // High complexity with many files or criteria suggests it should be broken down
        if (fileCount > 5 || criteriaCount > 5) {
          issues.push({
            type: 'too_large',
            subtaskId: subtask.id,
            message: `Subtask "${subtask.title}" may be too large (${fileCount} files, ${criteriaCount} criteria)`,
            severity: 'warning',
          })
          tooLargeCount++
        }
      }

      // Check description length as proxy for complexity
      if (subtask.description.length > 500) {
        issues.push({
          type: 'too_large',
          subtaskId: subtask.id,
          message: `Subtask "${subtask.title}" has a very long description (${subtask.description.length} chars), consider splitting`,
          severity: 'warning',
        })
        tooLargeCount++
      }
    }

    if (tooLargeCount > 0) {
      suggestions.push(
        `Consider breaking down ${tooLargeCount} large subtask(s) into smaller, more focused tasks.`
      )
    }
  }

  // ===========================================================================
  // Description Clarity Check
  // ===========================================================================

  private checkDescriptions(
    decomposition: Decomposition,
    issues: ValidationIssue[],
    suggestions: string[]
  ): void {
    let vagueCount = 0

    for (const subtask of decomposition.subtasks) {
      // Check title
      if (subtask.title.length < 5) {
        issues.push({
          type: 'too_vague',
          subtaskId: subtask.id,
          message: `Subtask title is too short: "${subtask.title}"`,
          severity: 'warning',
        })
        vagueCount++
      }

      // Check description
      if (subtask.description.length < 20) {
        issues.push({
          type: 'too_vague',
          subtaskId: subtask.id,
          message: `Subtask description is too brief: "${subtask.description}"`,
          severity: 'warning',
        })
        vagueCount++
      }

      // Check for placeholder text
      const placeholderPatterns = [/TODO/i, /TBD/i, /placeholder/i, /lorem ipsum/i, /xxx/i]
      for (const pattern of placeholderPatterns) {
        if (pattern.test(subtask.description) || pattern.test(subtask.title)) {
          issues.push({
            type: 'too_vague',
            subtaskId: subtask.id,
            message: `Subtask "${subtask.title}" contains placeholder text`,
            severity: 'warning',
          })
          vagueCount++
          break
        }
      }
    }

    if (vagueCount > 0) {
      suggestions.push(
        `Improve clarity of ${vagueCount} subtask description(s) with specific implementation details.`
      )
    }
  }

  // ===========================================================================
  // Dependency Validity Check
  // ===========================================================================

  private checkDependencyValidity(decomposition: Decomposition, issues: ValidationIssue[]): void {
    const subtaskIds = new Set(decomposition.subtasks.map((s) => s.id))

    for (const subtask of decomposition.subtasks) {
      if (subtask.dependencies) {
        for (const depId of subtask.dependencies) {
          // Check self-dependency
          if (depId === subtask.id) {
            issues.push({
              type: 'self_dep',
              subtaskId: subtask.id,
              message: `Subtask "${subtask.title}" depends on itself`,
              severity: 'error',
            })
          }
          // Check for non-existent dependency
          else if (!subtaskIds.has(depId)) {
            issues.push({
              type: 'missing_deps',
              subtaskId: subtask.id,
              message: `Subtask "${subtask.title}" depends on non-existent subtask: ${depId}`,
              severity: 'error',
            })
          }
        }
      }
    }
  }

  // ===========================================================================
  // Subtask Count Check
  // ===========================================================================

  private checkSubtaskCount(
    decomposition: Decomposition,
    issues: ValidationIssue[],
    suggestions: string[]
  ): void {
    const count = decomposition.subtasks.length

    if (count === 0) {
      issues.push({
        type: 'too_vague',
        message: 'Decomposition has no subtasks',
        severity: 'error',
      })
    } else if (count === 1) {
      suggestions.push(
        'Consider if this task truly needs decomposition, or if it can be executed directly.'
      )
    } else if (count > this.config.maxSubtasks) {
      issues.push({
        type: 'too_large',
        message: `Decomposition has ${count} subtasks (max recommended: ${this.config.maxSubtasks})`,
        severity: 'warning',
      })
      suggestions.push(
        `Consider grouping related subtasks or creating a higher-level decomposition with ${this.config.maxSubtasks} or fewer subtasks.`
      )
    }

    // Check for file-based strategy without files
    if (decomposition.strategy === 'file_based') {
      const subtasksWithoutFiles = decomposition.subtasks.filter(
        (s) => !s.files || s.files.length === 0
      )
      if (subtasksWithoutFiles.length === decomposition.subtasks.length) {
        issues.push({
          type: 'no_files',
          message: 'File-based strategy used but no subtasks have files specified',
          severity: 'warning',
        })
        suggestions.push(
          'For file-based decomposition, specify which files each subtask will modify.'
        )
      }
    }
  }

  // ===========================================================================
  // Score Calculation
  // ===========================================================================

  private calculateScore(issues: ValidationIssue[], subtaskCount: number): number {
    if (subtaskCount === 0) {
      return 0
    }

    // Start with perfect score
    let score = 1.0

    // Deduct for errors (major impact)
    const errors = issues.filter((i) => i.severity === 'error')
    score -= errors.length * 0.2

    // Deduct for warnings (minor impact)
    const warnings = issues.filter((i) => i.severity === 'warning')
    score -= warnings.length * 0.05

    // Normalize based on subtask count (more subtasks = more potential issues)
    const normalizedScore = Math.max(0, Math.min(1, score))

    return Math.round(normalizedScore * 100) / 100
  }
}
