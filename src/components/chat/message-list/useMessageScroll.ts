/**
 * Message scroll management hook
 *
 * Handles auto-scroll to bottom, ResizeObserver-based scroll tracking,
 * scroll-up backfill for older messages, and scroll-to-message navigation.
 */

import { type Accessor, createEffect, createSignal, on, onCleanup, onMount } from 'solid-js'
import { syncNestedScrollableBindings } from './nested-scrollables'

export interface UseMessageScrollOptions {
  /** Whether auto-scroll is enabled in settings */
  autoScrollEnabled: Accessor<boolean>
  /** Whether the agent/model is currently streaming */
  isStreaming: Accessor<boolean>
  /** Number of messages currently hidden above the visible window */
  hiddenMessageCount: Accessor<number>
  /** Callback to load older messages when scrolled near top */
  onLoadOlder: () => void
}

export interface MessageScrollAPI {
  /** Ref callback — assign to the scroll container's `ref` prop */
  containerRef: HTMLDivElement | undefined
  setContainerRef: (el: HTMLDivElement) => void
  /** Whether auto-scroll is active (bottom-locked) */
  shouldAutoScroll: Accessor<boolean>
  /** Programmatically scroll to the bottom */
  scrollToBottom: () => void
  /** Scroll a specific message into view */
  scrollToMessage: (messageId: string) => void
  /** Mount the scroll listeners + ResizeObserver */
  setup: () => void
}

export function useMessageScroll(opts: UseMessageScrollOptions): MessageScrollAPI {
  let containerRef: HTMLDivElement | undefined
  let scrollRaf: number | undefined
  let resizeObserver: ResizeObserver | undefined
  let userScrolledUp = false
  let lastBackfillHiddenCount = -1
  // True while the pointer is hovering over a nested [data-scrollable] region.
  // Scroll events that occur during this time come from scrolling inside tool
  // output / diff viewers — they must not disable main-chat auto-scroll.
  let pointerOverNestedScrollable = false
  const trackedNestedScrollables = new Set<Element>()

  const [shouldAutoScroll, setShouldAutoScroll] = createSignal(true)

  // Reset when streaming starts
  createEffect(
    on(opts.isStreaming, (streaming) => {
      if (streaming) {
        userScrolledUp = false
        setShouldAutoScroll(true)
      }
    })
  )

  const setupResizeObserver = (): void => {
    if (!containerRef) return
    resizeObserver = new ResizeObserver(() => {
      if (!containerRef || !opts.autoScrollEnabled()) return
      if (userScrolledUp) return
      if (!shouldAutoScroll()) return
      // Direct assignment (bypasses smooth scroll CSS)
      containerRef.scrollTop = containerRef.scrollHeight
    })
    // Observe the scrollable content (first child) — its resize = content growth
    const content = containerRef.firstElementChild
    if (content) resizeObserver.observe(content)
    // Also observe the container itself (viewport resize)
    resizeObserver.observe(containerRef)
  }

  const handlePointerEnterNested = (): void => {
    pointerOverNestedScrollable = true
  }

  const handlePointerLeaveNested = (): void => {
    pointerOverNestedScrollable = false
  }

  /** Attach pointer-enter/leave tracking to all [data-scrollable] descendants. */
  const observeNestedScrollables = (): void => {
    if (!containerRef) return
    syncNestedScrollableBindings(
      trackedNestedScrollables,
      containerRef.querySelectorAll('[data-scrollable]'),
      handlePointerEnterNested,
      handlePointerLeaveNested
    )
  }

  /** Re-run whenever new [data-scrollable] nodes may have been added. */
  const nestedScrollObserver = new MutationObserver(() => {
    if (!containerRef) return
    observeNestedScrollables()
  })

  const handleScroll = (_event: Event): void => {
    if (!containerRef) return
    if (scrollRaf !== undefined) return

    // If the pointer is over a nested [data-scrollable] region (tool output,
    // diff viewer, etc.), this scroll event was caused by scrolling inside that
    // region overflowing into the outer container.  Don't change auto-scroll.
    if (pointerOverNestedScrollable) return

    scrollRaf = requestAnimationFrame(() => {
      scrollRaf = undefined
      if (!containerRef) return

      const { scrollTop, scrollHeight, clientHeight } = containerRef
      const distanceFromBottom = scrollHeight - scrollTop - clientHeight
      const streaming = opts.isStreaming()

      if (streaming) {
        // During streaming: detect if user scrolled up (away from bottom)
        userScrolledUp = distanceFromBottom > 300
        if (!userScrolledUp) setShouldAutoScroll(true)
      } else {
        // Re-lock auto-scroll when user returns to within 50px of bottom
        const nextAutoScroll = distanceFromBottom < 50
        if (nextAutoScroll !== shouldAutoScroll()) {
          setShouldAutoScroll(nextAutoScroll)
        }
      }

      // Scroll-up backfill: load older messages when near top
      const hiddenCount = opts.hiddenMessageCount()
      if (scrollTop >= 200) {
        lastBackfillHiddenCount = -1
      } else if (hiddenCount > 0 && hiddenCount !== lastBackfillHiddenCount) {
        lastBackfillHiddenCount = hiddenCount
        opts.onLoadOlder()
      }
    })
  }

  const setup = (): void => {
    onMount(() => {
      if (containerRef) {
        containerRef.scrollTop = containerRef.scrollHeight
        // Passive listener — critical for smooth scrolling in WebKitGTK.
        containerRef.addEventListener('scroll', handleScroll, { passive: true })
        setupResizeObserver()
        // Track pointer position relative to nested scrollable regions so
        // we can ignore scroll events that originate from within them.
        observeNestedScrollables()
        nestedScrollObserver.observe(containerRef, { childList: true, subtree: true })
      }
    })

    onCleanup(() => {
      if (scrollRaf !== undefined) cancelAnimationFrame(scrollRaf)
      containerRef?.removeEventListener('scroll', handleScroll)
      resizeObserver?.disconnect()
      nestedScrollObserver.disconnect()
      syncNestedScrollableBindings(
        trackedNestedScrollables,
        [],
        handlePointerEnterNested,
        handlePointerLeaveNested
      )
    })
  }

  const scrollToBottom = (): void => {
    if (containerRef) {
      containerRef.scrollTop = containerRef.scrollHeight
      setShouldAutoScroll(true)
    }
  }

  const scrollToMessage = (messageId: string): void => {
    if (!containerRef) return
    const el = containerRef.querySelector(`[data-message-id="${messageId}"]`)
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' })
    }
  }

  return {
    get containerRef() {
      return containerRef
    },
    setContainerRef: (el: HTMLDivElement) => {
      containerRef = el
    },
    shouldAutoScroll,
    scrollToBottom,
    scrollToMessage,
    setup,
  }
}
