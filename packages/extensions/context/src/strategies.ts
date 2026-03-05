export {
  amortizedForgettingStrategy,
  createAmortizedForgettingStrategy,
} from './strategies/amortized-forgetting.js'
export { backwardFifoStrategy, createBackwardFifoStrategy } from './strategies/backward-fifo.js'
export {
  estimateTokens,
  PROTECTED_TOOLS,
  PRUNE_MIN_THRESHOLD,
  PRUNE_TOKEN_BUDGET,
} from './strategies/common.js'
export {
  createObservationMaskingStrategy,
  observationMaskingStrategy,
} from './strategies/observation-masking.js'
export { createPipelineStrategy } from './strategies/pipeline.js'
export { pruneStrategy } from './strategies/prune.js'
export { createSlidingWindowStrategy, slidingWindowStrategy } from './strategies/sliding-window.js'
export { summarizeStrategy } from './strategies/summarize.js'
export { targetForWindow, tieredCompactionStrategy } from './strategies/tiered-compaction.js'
export { truncateStrategy } from './strategies/truncate.js'

import { amortizedForgettingStrategy } from './strategies/amortized-forgetting.js'
import { backwardFifoStrategy } from './strategies/backward-fifo.js'
import { observationMaskingStrategy } from './strategies/observation-masking.js'
import { pruneStrategy } from './strategies/prune.js'
import { slidingWindowStrategy } from './strategies/sliding-window.js'
import { summarizeStrategy } from './strategies/summarize.js'
import { tieredCompactionStrategy } from './strategies/tiered-compaction.js'
import { truncateStrategy } from './strategies/truncate.js'

export const ALL_STRATEGIES = [
  tieredCompactionStrategy,
  pruneStrategy,
  backwardFifoStrategy,
  truncateStrategy,
  summarizeStrategy,
  slidingWindowStrategy,
  observationMaskingStrategy,
  amortizedForgettingStrategy,
]
