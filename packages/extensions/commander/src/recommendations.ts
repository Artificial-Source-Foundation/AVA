export interface Recommendation {
  summary: string
  nextAction: string
  parallel?: string
}

function extractChecklistItems(markdown: string): string[] {
  return markdown
    .split('\n')
    .map((line) => line.trim())
    .filter((line) => line.startsWith('- [ ]'))
    .map((line) => line.replace('- [ ]', '').trim())
}

export function buildRecommendation(
  completedTask: string,
  roadmapFiles: Array<{ path: string; content: string }>
): Recommendation | null {
  if (roadmapFiles.length === 0) return null

  const pending = roadmapFiles.flatMap((file) => extractChecklistItems(file.content))
  if (pending.length === 0) {
    return {
      summary: `Task ${completedTask} complete.`,
      nextAction: 'No pending roadmap tasks found.',
    }
  }

  return {
    summary: `Task ${completedTask} complete.`,
    nextAction: `${pending[0]} is now the best next step.`,
    parallel: pending[1] ? `${pending[1]} appears independent and can run in parallel.` : undefined,
  }
}

export function formatRecommendationMessage(rec: Recommendation): string {
  const parts = [rec.summary, `Recommendation: ${rec.nextAction}`]
  if (rec.parallel) parts.push(rec.parallel)
  return parts.join(' ')
}
