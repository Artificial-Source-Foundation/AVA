export interface LatestRequestGate {
  begin: () => number
  isCurrent: (token: number) => boolean
}

export function createLatestRequestGate(): LatestRequestGate {
  let current = 0

  return {
    begin: () => {
      current += 1
      return current
    },
    isCurrent: (token) => token === current,
  }
}
