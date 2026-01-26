/**
 * Delta9 Edit Error Recovery
 *
 * Detects Edit tool failures and injects recovery instructions.
 * Inspired by oh-my-opencode's edit error handling.
 *
 * Philosophy: "When edits fail, guide the agent to recover"
 */

// =============================================================================
// Types
// =============================================================================

/**
 * Types of edit errors that can be detected
 */
export type EditErrorType =
  | 'no_change'      // oldString and newString are the same
  | 'not_found'      // oldString not found in file
  | 'ambiguous'      // oldString found multiple times
  | 'file_missing'   // File does not exist
  | 'permission'     // Permission denied
  | 'syntax'         // Syntax error in edit
  | 'unknown'        // Unknown error

/**
 * Edit error detection result
 */
export interface EditErrorResult {
  /** Type of error detected */
  errorType: EditErrorType
  /** Original error message */
  originalMessage: string
  /** Additional context if available */
  context?: string
}

/**
 * Edit error pattern definition
 */
interface EditErrorPattern {
  pattern: RegExp
  errorType: EditErrorType
  context?: string
}

// =============================================================================
// Error Patterns
// =============================================================================

/**
 * Patterns to detect edit errors from output/error messages.
 * Order matters - more specific patterns should come first.
 */
const EDIT_ERROR_PATTERNS: EditErrorPattern[] = [
  // No change errors
  {
    pattern: /oldString and newString must be different/i,
    errorType: 'no_change',
    context: 'The replacement text is identical to the original',
  },
  {
    pattern: /no changes? (to make|made|detected)/i,
    errorType: 'no_change',
    context: 'No changes were necessary',
  },

  // Not found errors
  {
    pattern: /oldString not found/i,
    errorType: 'not_found',
    context: 'The text to replace was not found in the file',
  },
  {
    pattern: /could not find.*to replace/i,
    errorType: 'not_found',
    context: 'The search text was not found',
  },
  {
    pattern: /no match(es)? found/i,
    errorType: 'not_found',
    context: 'The pattern did not match any content',
  },
  {
    pattern: /string not found in file/i,
    errorType: 'not_found',
    context: 'The exact string was not found',
  },

  // Ambiguous errors (multiple matches)
  {
    pattern: /oldString found multiple times/i,
    errorType: 'ambiguous',
    context: 'Multiple matches found - need more context',
  },
  {
    pattern: /multiple occurrences/i,
    errorType: 'ambiguous',
    context: 'The text appears multiple times',
  },
  {
    pattern: /ambiguous match/i,
    errorType: 'ambiguous',
    context: 'Cannot determine which occurrence to replace',
  },

  // File missing errors
  {
    pattern: /file.+not found/i,
    errorType: 'file_missing',
    context: 'The file does not exist',
  },
  {
    pattern: /no such file/i,
    errorType: 'file_missing',
    context: 'File does not exist at the specified path',
  },
  {
    pattern: /ENOENT/i,
    errorType: 'file_missing',
    context: 'File or directory not found',
  },

  // Permission errors
  {
    pattern: /permission denied/i,
    errorType: 'permission',
    context: 'Insufficient permissions to modify file',
  },
  {
    pattern: /EACCES/i,
    errorType: 'permission',
    context: 'Access denied',
  },

  // Syntax errors
  {
    pattern: /syntax error/i,
    errorType: 'syntax',
    context: 'The edit would create invalid syntax',
  },
  {
    pattern: /parse error/i,
    errorType: 'syntax',
    context: 'Cannot parse the content',
  },
]

// =============================================================================
// Recovery Messages
// =============================================================================

/**
 * Recovery instructions for each error type
 */
const RECOVERY_INSTRUCTIONS: Record<EditErrorType, string> = {
  no_change: `[EDIT ERROR - NO CHANGE DETECTED]

The replacement text is identical to the original. This is likely a mistake.

IMMEDIATE ACTIONS:
1. Check if you copied the wrong text
2. Verify you made the intended changes to newString
3. If no change is needed, move on to the next task`,

  not_found: `[EDIT ERROR - TEXT NOT FOUND]

The text you're trying to replace was not found in the file.

IMMEDIATE ACTIONS:
1. READ the file immediately to see its ACTUAL current state
2. The file content may have changed or be different than expected
3. Copy the EXACT text from the file (including whitespace/indentation)
4. Retry with the correct oldString`,

  ambiguous: `[EDIT ERROR - MULTIPLE MATCHES]

The text to replace appears multiple times in the file.

IMMEDIATE ACTIONS:
1. READ the file to see all occurrences
2. Include MORE context in oldString (surrounding lines)
3. Make oldString unique enough to match only one location
4. Consider using line numbers if available`,

  file_missing: `[EDIT ERROR - FILE NOT FOUND]

The file you're trying to edit does not exist.

IMMEDIATE ACTIONS:
1. Verify the file path is correct
2. Use Glob to search for the file
3. If creating a new file, use Write tool instead
4. Check if the file was renamed or moved`,

  permission: `[EDIT ERROR - PERMISSION DENIED]

You don't have permission to modify this file.

IMMEDIATE ACTIONS:
1. Check if the file is read-only
2. Verify you're in the correct directory
3. Report this as a blocker via task_complete`,

  syntax: `[EDIT ERROR - SYNTAX ERROR]

The edit would create invalid syntax in the file.

IMMEDIATE ACTIONS:
1. Review the new content for syntax issues
2. Ensure proper quoting and escaping
3. Check for mismatched brackets or quotes
4. Validate the edit produces valid code`,

  unknown: `[EDIT ERROR - UNKNOWN ERROR]

An unexpected error occurred during the edit.

IMMEDIATE ACTIONS:
1. READ the file to verify its current state
2. Try the edit again with exact content
3. If error persists, report as blocker`,
}

// =============================================================================
// Detection Functions
// =============================================================================

/**
 * Detect if output/error indicates an edit failure.
 *
 * @param output - Tool output string
 * @param error - Optional error object
 * @returns EditErrorResult if error detected, null otherwise
 */
export function detectEditError(output: string, error?: Error): EditErrorResult | null {
  // Check error message first
  if (error) {
    const errorMessage = error.message
    for (const { pattern, errorType, context } of EDIT_ERROR_PATTERNS) {
      if (pattern.test(errorMessage)) {
        return {
          errorType,
          originalMessage: errorMessage,
          context,
        }
      }
    }
  }

  // Check output string
  for (const { pattern, errorType, context } of EDIT_ERROR_PATTERNS) {
    if (pattern.test(output)) {
      return {
        errorType,
        originalMessage: output.slice(0, 200), // Truncate for context
        context,
      }
    }
  }

  // Check for generic failure indicators
  const lowerOutput = output.toLowerCase()
  if (
    lowerOutput.includes('error') ||
    lowerOutput.includes('failed') ||
    lowerOutput.includes('cannot')
  ) {
    // Only return unknown if it looks like an actual error
    if (
      lowerOutput.includes('edit') ||
      lowerOutput.includes('write') ||
      lowerOutput.includes('file')
    ) {
      return {
        errorType: 'unknown',
        originalMessage: output.slice(0, 200),
      }
    }
  }

  return null
}

/**
 * Generate recovery message for an edit error.
 *
 * @param result - Edit error detection result
 * @returns Formatted recovery message
 */
export function generateRecoveryMessage(result: EditErrorResult): string {
  const instructions = RECOVERY_INSTRUCTIONS[result.errorType]

  const parts = [
    instructions,
    '',
    '---',
    '',
    `Error Details: ${result.originalMessage}`,
  ]

  if (result.context) {
    parts.push(`Context: ${result.context}`)
  }

  parts.push(
    '',
    'DO NOT retry blindly. READ the file first to understand its actual state.'
  )

  return parts.join('\n')
}

/**
 * Check if a tool name is an edit tool that should be monitored.
 *
 * @param toolName - Name of the tool
 * @returns True if this is an edit tool
 */
export function isEditTool(toolName: string): boolean {
  const editTools = [
    'Edit',
    'edit',
    'Write',
    'write',
    'MultiEdit',
    'file_edit',
    'file_write',
  ]
  return editTools.includes(toolName)
}

/**
 * Get error type label for display.
 *
 * @param errorType - Edit error type
 * @returns Human-readable label
 */
export function getEditErrorLabel(errorType: EditErrorType): string {
  const labels: Record<EditErrorType, string> = {
    no_change: 'No Change',
    not_found: 'Text Not Found',
    ambiguous: 'Multiple Matches',
    file_missing: 'File Missing',
    permission: 'Permission Denied',
    syntax: 'Syntax Error',
    unknown: 'Unknown Error',
  }
  return labels[errorType]
}
