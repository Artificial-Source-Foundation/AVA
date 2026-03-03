export interface SamplingCandidate {
  id: string
  success: boolean
  output: string
  estimatedCost: number
}

export interface SamplerConfig {
  n: number
}

export interface SamplingResult {
  best: SamplingCandidate
  candidates: SamplingCandidate[]
}

export type CandidateGenerator = (index: number) => Promise<SamplingCandidate>

export function scoreCandidate(candidate: SamplingCandidate): number {
  const successScore = candidate.success ? 1 : 0
  const qualityScore = Math.min(candidate.output.trim().length / 200, 1)
  const costPenalty = Math.min(candidate.estimatedCost / 100, 1)
  return successScore * 0.6 + qualityScore * 0.3 + (1 - costPenalty) * 0.1
}

export function selectBestCandidate(candidates: SamplingCandidate[]): SamplingCandidate {
  if (candidates.length === 0) {
    throw new Error('No candidates to score')
  }

  let best = candidates[0]!
  let bestScore = scoreCandidate(best)

  for (const candidate of candidates.slice(1)) {
    const score = scoreCandidate(candidate)
    if (score > bestScore) {
      best = candidate
      bestScore = score
    }
  }

  return best
}

export async function sampleBestOfN(
  config: SamplerConfig,
  generateCandidate: CandidateGenerator
): Promise<SamplingResult> {
  const n = Math.max(1, config.n)
  const candidates = await Promise.all(
    Array.from({ length: n }, (_, index) => generateCandidate(index))
  )

  return {
    best: selectBestCandidate(candidates),
    candidates,
  }
}
