/**
 * Resize Handlers Hook
 *
 * Drag-to-resize handlers for sidebar, right panel, and bottom panel.
 */

interface ResizeCallbacks {
  sidebarWidth: () => number
  setSidebarWidth: (w: number) => void
  rightPanelWidth: () => number
  setRightPanelWidth: (w: number) => void
  bottomPanelHeight: () => number
  setBottomPanelHeight: (h: number) => void
}

interface ResizeHandlers {
  startSidebarResize: (e: MouseEvent) => void
  startRightResize: (e: MouseEvent) => void
  startBottomResize: (e: MouseEvent) => void
}

export function createResizeHandlers(callbacks: ResizeCallbacks): ResizeHandlers {
  const startSidebarResize = (e: MouseEvent) => {
    e.preventDefault()
    const startX = e.clientX
    const startWidth = callbacks.sidebarWidth()

    const onMove = (moveEvent: MouseEvent) => {
      const delta = moveEvent.clientX - startX
      callbacks.setSidebarWidth(startWidth + delta)
    }

    const onUp = () => {
      document.removeEventListener('mousemove', onMove)
      document.removeEventListener('mouseup', onUp)
    }

    document.addEventListener('mousemove', onMove)
    document.addEventListener('mouseup', onUp)
  }

  const startRightResize = (e: MouseEvent) => {
    e.preventDefault()
    const startX = e.clientX
    const startWidth = callbacks.rightPanelWidth()

    const onMove = (moveEvent: MouseEvent) => {
      const delta = startX - moveEvent.clientX
      callbacks.setRightPanelWidth(startWidth + delta)
    }

    const onUp = () => {
      document.removeEventListener('mousemove', onMove)
      document.removeEventListener('mouseup', onUp)
    }

    document.addEventListener('mousemove', onMove)
    document.addEventListener('mouseup', onUp)
  }

  const startBottomResize = (e: MouseEvent) => {
    e.preventDefault()
    const startY = e.clientY
    const startHeight = callbacks.bottomPanelHeight()

    const onMove = (moveEvent: MouseEvent) => {
      const delta = startY - moveEvent.clientY
      callbacks.setBottomPanelHeight(startHeight + delta)
    }

    const onUp = () => {
      document.removeEventListener('mousemove', onMove)
      document.removeEventListener('mouseup', onUp)
    }

    document.addEventListener('mousemove', onMove)
    document.addEventListener('mouseup', onUp)
  }

  return { startSidebarResize, startRightResize, startBottomResize }
}
