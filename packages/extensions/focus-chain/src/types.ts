/**
 * Focus chain types.
 */

export interface FocusItem {
  id: string
  description: string
  status: 'pending' | 'in_progress' | 'completed' | 'blocked'
  createdAt: number
  completedAt?: number
  parentId?: string
}

export interface FocusChain {
  sessionId: string
  items: FocusItem[]
  currentFocus?: string
}
