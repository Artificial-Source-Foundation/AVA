/**
 * Message scroll management hook
 *
 * Handles auto-scroll to bottom, ResizeObserver-based scroll tracking,
 * scroll-up backfill for older messages, and scroll-to-message navigation.
 */

import { type Accessor, createEffect, createSignal, on, onCleanup, onMount } from 'solid-js'

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
  let resizeRaf: number | undefined
  let resizeObserver: ResizeObserver | undefined
  let userScrolledUp = false
  let lastBackfillHiddenCount = -1
  let pointerOverNestedScrollable = false

  const [shouldAutoScroll, setShouldAutoScroll] = createSignal(true)

  const prefersReducedMotion = (): boolean =>
    typeof window !== 'undefined' && window.matchMedia('(prefers-reduced-motion: reduce)').matches

  const scrollToEdge = (behavior: ScrollBehavior): void => {
    if (!containerRef) return
    containerRef.scrollTo({
      top: containerRef.scrollHeight,
      behavior: prefersReducedMotion() ? 'auto' : behavior,
    })
  }

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
    let resizeCount = 0
    resizeObserver = new ResizeObserver((entries) => {
      if (!containerRef || !opts.autoScrollEnabled()) return
      if (userScrolledUp) return
      if (!shouldAutoScroll()) return

      // Skip resize observations that don't meaningfully change content height
      // This reduces thrash in WebKitGTK during streaming
      const entry = entries[0]
      if (!entry) return

      // Coalesce rapid resize events (streaming) into a single rAF
      if (resizeRaf !== undefined) {
        resizeCount++
        return
      }

      resizeRaf = requestAnimationFrame(() => {
        resizeRaf = undefined
        if (!containerRef) return

        // If we skipped many frames during streaming, force 'auto' behavior
        const behavior =
          opts.isStreaming() && resizeCount > 2 ? 'auto' : opts.isStreaming() ? 'auto' : 'smooth'
        resizeCount = 0
        scrollToEdge(behavior)
      })
    })
    // Observe the scrollable content (first child) — its resize = content growth
    const content = containerRef.firstElementChild
    if (content) resizeObserver.observe(content)
  }

  const getNestedScrollable = (target: EventTarget | null): HTMLElement | null => {
    if (!(target instanceof Element)) return null
    return target.closest('[data-scrollable]')
  }

  const handlePointerOver = (event: PointerEvent): void => {
    pointerOverNestedScrollable = getNestedScrollable(event.target) !== null
  }

  const handlePointerOut = (event: PointerEvent): void => {
    const nextScrollable = getNestedScrollable(event.relatedTarget)
    pointerOverNestedScrollable = nextScrollable !== null
  }

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
        scrollToEdge('auto')
        // Passive listener — critical for smooth scrolling in WebKitGTK.
        // capture: false ensures we're not blocking native scroll handling.
        containerRef.addEventListener('scroll', handleScroll, { passive: true, capture: false })
        containerRef.addEventListener('pointerover', handlePointerOver, {
          passive: true,
          capture: false,
        })
        containerRef.addEventListener('pointerout', handlePointerOut, {
          passive: true,
          capture: false,
        })
        setupResizeObserver()
      }
    })

    onCleanup(() => {
      if (scrollRaf !== undefined) cancelAnimationFrame(scrollRaf)
      if (resizeRaf !== undefined) cancelAnimationFrame(resizeRaf)
      containerRef?.removeEventListener('scroll', handleScroll, { capture: false })
      containerRef?.removeEventListener('pointerover', handlePointerOver, { capture: false })
      containerRef?.removeEventListener('pointerout', handlePointerOut, { capture: false })
      resizeObserver?.disconnect()
    })
  }

  const scrollToBottom = (): void => {
    if (containerRef) {
      scrollToEdge('smooth')
      setShouldAutoScroll(true)
    }
  }

  const scrollToMessage = (messageId: string): void => {
    if (!containerRef) return
    const el = containerRef.querySelector(`[data-message-id="${CSS.escape(messageId)}"]`)
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
