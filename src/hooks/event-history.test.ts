import { createRoot } from 'solid-js'
import { describe, expect, it } from 'vitest'
import { createBoundedEventHistory } from './event-history'

describe('createBoundedEventHistory', () => {
  it('caps retained events to the configured maximum', () => {
    createRoot((dispose) => {
      const history = createBoundedEventHistory<number>(3)

      history.append(1)
      history.append(2)
      history.append(3)
      history.append(4)

      expect(history.events()).toEqual([2, 3, 4])
      dispose()
    })
  })

  it('clears retained events without replacing the accessor contract', () => {
    createRoot((dispose) => {
      const history = createBoundedEventHistory<string>(5)
      history.append('a')
      history.append('b')

      history.clear()

      expect(history.events()).toEqual([])
      history.append('c')
      expect(history.events()).toEqual(['c'])
      dispose()
    })
  })

  it('reads only newly appended events after saturation', () => {
    createRoot((dispose) => {
      const history = createBoundedEventHistory<number>(3)

      history.append(1)
      history.append(2)
      history.append(3)

      const initialCursor = history.cursor()

      history.append(4)
      history.append(5)

      expect(history.events()).toEqual([3, 4, 5])
      expect(history.readSince(initialCursor)).toEqual({
        cursor: history.cursor(),
        events: [4, 5],
      })

      dispose()
    })
  })
})
