export function syncNestedScrollableBindings(
  tracked: Set<Element>,
  nextElements: Iterable<Element>,
  onEnter: EventListener,
  onLeave: EventListener
): Set<Element> {
  const next = new Set(nextElements)

  for (const element of tracked) {
    if (next.has(element)) continue
    element.removeEventListener('pointerenter', onEnter)
    element.removeEventListener('pointerleave', onLeave)
    tracked.delete(element)
  }

  for (const element of next) {
    if (tracked.has(element)) continue
    element.addEventListener('pointerenter', onEnter)
    element.addEventListener('pointerleave', onLeave)
    tracked.add(element)
  }

  return tracked
}
