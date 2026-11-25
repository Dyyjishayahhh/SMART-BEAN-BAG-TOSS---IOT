export const memoryMaxLevel = 10
export const memoryLeadMs = 600
export const memoryFlashMs = level => 900 - 55 * (level - 1)
export const memoryStepMs = level => memoryFlashMs(level) + 300 - 15 * (level - 1)

export function memoryResult(round) {
  if (round?.game !== 'memory' || round.status !== 'complete') return {}
  const completePattern = round.attempts.length === round.level + 2
  if (!completePattern && !round.corrections) return {}
  const memoryCleared = completePattern && !round.corrections && round.attempts.every(([target, landed]) => target === landed)
  return {
    memoryCleared,
    nextMemoryLevel: memoryCleared ? (round.level === memoryMaxLevel ? 1 : round.level + 1) : round.level
  }
}
