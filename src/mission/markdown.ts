/**
 * Delta9 Mission Markdown Generator
 *
 * Generates human-readable mission.md from mission state.
 */

import { format } from 'date-fns'
import type { Mission, Objective, Task } from '../types/mission.js'

// =============================================================================
// Status Icons
// =============================================================================

const STATUS_ICONS = {
  mission: {
    planning: '\u{1F4DD}',      // Memo
    approved: '\u{2705}',       // Check mark
    in_progress: '\u{1F504}',   // Arrows
    paused: '\u{23F8}',         // Pause
    completed: '\u{1F389}',     // Party
    aborted: '\u{274C}',        // X
  },
  objective: {
    pending: '\u{23F3}',        // Hourglass
    in_progress: '\u{1F504}',   // Arrows
    completed: '\u{2705}',      // Check
    failed: '\u{274C}',         // X
  },
  task: {
    pending: '[ ]',
    blocked: '[\u{1F512}]',     // Lock
    in_progress: '[\u{1F504}]', // Arrows
    completed: '[x]',
    failed: '[\u{274C}]',       // X
  },
}

// =============================================================================
// Formatters
// =============================================================================

function formatDate(isoDate: string): string {
  try {
    return format(new Date(isoDate), 'MMM d, yyyy h:mm a')
  } catch {
    return isoDate
  }
}

function formatCost(amount: number): string {
  return `$${amount.toFixed(2)}`
}

function progressBar(percentage: number, width: number = 20): string {
  const filled = Math.round((percentage / 100) * width)
  const empty = width - filled
  return `[${'█'.repeat(filled)}${'░'.repeat(empty)}] ${percentage}%`
}

// =============================================================================
// Section Generators
// =============================================================================

function generateHeader(mission: Mission): string {
  const icon = STATUS_ICONS.mission[mission.status]

  return `# ${icon} Mission: ${mission.description}

**ID**: \`${mission.id}\`
**Status**: ${mission.status.toUpperCase()}
**Complexity**: ${mission.complexity.toUpperCase()}
**Council Mode**: ${mission.councilMode}
**Created**: ${formatDate(mission.createdAt)}
**Updated**: ${formatDate(mission.updatedAt)}
`
}

function generateBudget(mission: Mission): string {
  const { spent, limit, breakdown } = mission.budget
  const percentage = limit > 0 ? Math.round((spent / limit) * 100) : 0

  return `## Budget

${progressBar(percentage)}

| Category | Spent |
|----------|-------|
| Council | ${formatCost(breakdown.council)} |
| Operators | ${formatCost(breakdown.operators)} |
| Validators | ${formatCost(breakdown.validators)} |
| Support | ${formatCost(breakdown.support)} |
| **Total** | **${formatCost(spent)}** / ${formatCost(limit)} |
`
}

function generateCouncilSummary(mission: Mission): string {
  if (!mission.councilSummary) {
    return ''
  }

  const { mode, consensus, disagreementsResolved, confidenceAvg, opinions } = mission.councilSummary

  let section = `## Council Summary

**Mode**: ${mode}
**Confidence**: ${Math.round(confidenceAvg * 100)}%

### Consensus
${consensus.map(c => `- ${c}`).join('\n')}
`

  if (disagreementsResolved && disagreementsResolved.length > 0) {
    section += `
### Resolved Disagreements
${disagreementsResolved.map(d => `- ${d}`).join('\n')}
`
  }

  if (opinions && opinions.length > 0) {
    section += `
### Oracle Opinions
${opinions.map(o => `
#### ${o.oracle} (${Math.round(o.confidence * 100)}%)
${o.recommendation}
${o.caveats ? `\n*Caveats*: ${o.caveats.join(', ')}` : ''}
`).join('\n')}
`
  }

  return section
}

function generateProgress(mission: Mission): string {
  let total = 0
  let completed = 0

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      total++
      if (task.status === 'completed') completed++
    }
  }

  const percentage = total > 0 ? Math.round((completed / total) * 100) : 0

  return `## Progress

${progressBar(percentage)}
**${completed}/${total}** tasks completed
`
}

function generateTask(task: Task): string {
  const icon = STATUS_ICONS.task[task.status]
  let line = `- ${icon} ${task.description}`

  if (task.status === 'in_progress' && task.assignedTo) {
    line += ` ← *${task.assignedTo}*`
  }

  if (task.status === 'completed' && task.validation) {
    line += ` ✓`
  }

  if (task.status === 'failed' && task.error) {
    line += ` *(${task.error})*`
  }

  return line
}

function generateObjective(objective: Objective, index: number): string {
  const icon = STATUS_ICONS.objective[objective.status]

  const completedTasks = objective.tasks.filter(t => t.status === 'completed').length
  const totalTasks = objective.tasks.length
  const progress = totalTasks > 0 ? Math.round((completedTasks / totalTasks) * 100) : 0

  let section = `### ${icon} Objective ${index + 1}: ${objective.description}

*Progress: ${completedTasks}/${totalTasks} (${progress}%)*

${objective.tasks.map(t => generateTask(t)).join('\n')}
`

  if (objective.checkpoint) {
    section += `\n*Checkpoint*: \`${objective.checkpoint}\`\n`
  }

  return section
}

function generateObjectives(mission: Mission): string {
  if (mission.objectives.length === 0) {
    return `## Objectives

*No objectives defined yet*
`
  }

  return `## Objectives

${mission.objectives.map((o, i) => generateObjective(o, i)).join('\n')}
`
}

function generateFooter(mission: Mission): string {
  const lines: string[] = ['---', '']

  if (mission.approvedAt) {
    lines.push(`*Approved*: ${formatDate(mission.approvedAt)}`)
  }

  if (mission.completedAt) {
    lines.push(`*Completed*: ${formatDate(mission.completedAt)}`)
  }

  lines.push('')
  lines.push('*Generated by Delta9*')

  return lines.join('\n')
}

// =============================================================================
// Main Generator
// =============================================================================

/**
 * Generate mission.md content from mission state
 */
export function generateMissionMarkdown(mission: Mission): string {
  const sections = [
    generateHeader(mission),
    generateProgress(mission),
    generateBudget(mission),
    generateCouncilSummary(mission),
    generateObjectives(mission),
    generateFooter(mission),
  ]

  return sections.filter(s => s.length > 0).join('\n')
}
