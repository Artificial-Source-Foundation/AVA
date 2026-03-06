import { Archive, Copy, GitFork, Pencil, Route, Trash2 } from 'lucide-solid'
import type { ContextMenuItem } from '../../ui/ContextMenu'

export interface ContextMenuState {
  x: number
  y: number
  sessionId: string
}

interface SessionLike {
  id: string
  name: string
}

interface SessionContextMenuDeps {
  sessions: () => SessionLike[]
  requestRename: (sessionId: string) => void
  duplicateSession: (sessionId: string) => void
  forkSession: (sessionId: string, forkName: string) => void
  archiveSession: (sessionId: string) => void
  deleteSession: (sessionId: string) => void
  viewTrajectory: (sessionId: string) => void
}

export function buildSessionContextMenuItems(
  sessionId: string,
  deps: SessionContextMenuDeps
): ContextMenuItem[] {
  return [
    {
      label: 'Rename',
      icon: Pencil,
      action: () => deps.requestRename(sessionId),
    },
    {
      label: 'Duplicate',
      icon: Copy,
      action: () => deps.duplicateSession(sessionId),
    },
    {
      label: 'Fork from here',
      icon: GitFork,
      action: () => {
        const session = deps.sessions().find((entry) => entry.id === sessionId)
        if (session) {
          deps.forkSession(sessionId, `${session.name} (fork)`)
        }
      },
    },
    {
      label: 'View Trajectory',
      icon: Route,
      action: () => deps.viewTrajectory(sessionId),
    },
    { label: '', action: () => {}, separator: true },
    {
      label: 'Archive',
      icon: Archive,
      action: () => deps.archiveSession(sessionId),
    },
    {
      label: 'Delete',
      icon: Trash2,
      danger: true,
      action: () => deps.deleteSession(sessionId),
    },
  ]
}
