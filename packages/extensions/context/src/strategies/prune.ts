import type { ContextStrategy } from '@ava/core-v2/extensions'

import { createBackwardFifoStrategy } from './backward-fifo.js'

export const pruneStrategy: ContextStrategy = {
  ...createBackwardFifoStrategy(),
  name: 'prune',
  description: 'Prune old tool outputs with backward-scanned FIFO',
}
