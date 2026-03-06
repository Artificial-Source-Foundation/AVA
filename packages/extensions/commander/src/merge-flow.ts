import type { ToolContext } from '@ava/core-v2/tools'
import type { EngineerResult } from './engineer-loop.js'

export interface MergeResult {
  success: boolean
  mergedBranches: string[]
  redelegated: string[]
  conflicts: string[]
  summary: string
}

export interface EngineerMergeInput extends EngineerResult {
  agentId: string
  worktreeBranch?: string
}

export async function techLeadMerge(
  leadId: string,
  engineerResults: EngineerMergeInput[],
  context: ToolContext,
  deps?: {
    runCommand?: (command: string, cwd: string) => Promise<{ success: boolean; output: string }>
  }
): Promise<MergeResult> {
  const runCommand = deps?.runCommand
  const mergedBranches: string[] = []
  const redelegated: string[] = []
  const conflicts: string[] = []

  context.onEvent?.({ type: 'praxis:merge-started', leadId, count: engineerResults.length })

  for (const result of engineerResults) {
    if (!result.worktreeBranch) {
      redelegated.push(result.agentId)
      continue
    }

    if (!result.approved) {
      redelegated.push(result.agentId)
      continue
    }

    const mergeCmd = `git merge ${result.worktreeBranch}`
    if (runCommand) {
      const merge = await runCommand(mergeCmd, context.workingDirectory)
      if (!merge.success) {
        conflicts.push(result.worktreeBranch)
        continue
      }
    }

    mergedBranches.push(result.worktreeBranch)
  }

  if (runCommand) {
    await runCommand('npm run test:run', context.workingDirectory)
    await runCommand('npx tsc --noEmit', context.workingDirectory)
  }

  context.onEvent?.({
    type: 'praxis:merge-complete',
    leadId,
    merged: mergedBranches.length,
    conflicts: conflicts.length,
  })
  context.onEvent?.({ type: 'praxis:lead-complete', leadId, success: conflicts.length === 0 })

  const success = conflicts.length === 0
  return {
    success,
    mergedBranches,
    redelegated,
    conflicts,
    summary: `Merged ${mergedBranches.length}, redelegated ${redelegated.length}, conflicts ${conflicts.length}.`,
  }
}
