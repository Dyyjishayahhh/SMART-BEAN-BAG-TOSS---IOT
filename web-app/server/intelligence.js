export const targets = [10, 20, 30]

function targetPerformance(attempts) {
  return targets.map(target => {
    const aimed = attempts.filter(([aim]) => aim === target)
    const hits = aimed.filter(([, landed]) => landed === target).length
    const landed = attempts.filter(([, hole]) => hole === target).length
    return { target, attempts: aimed.length, hits, landed }
  })
}

export function playerInsights(rounds) {
  const attempts = rounds.flatMap(round => round.attempts)
  const byTarget = targetPerformance(attempts)
  const eligible = byTarget.filter(target => target.attempts >= 3)
  const needsPractice = eligible
    .filter(target => target.hits / target.attempts < 0.9)
    .sort((a, b) => a.hits / a.attempts - b.hits / b.attempts || b.attempts - a.attempts)[0]
  const coach = needsPractice
    ? { title: `Aim for the ${needsPractice.target}-point hole`, detail: `You hit it ${needsPractice.hits} out of ${needsPractice.attempts} times. Give it extra practice.` }
    : eligible.length === 3
      ? { title: 'Keep your streak going', detail: 'You hit at least 9 out of 10 attempts on each target.' }
      : { title: 'Play a few rounds', detail: 'Your coach will use your throws to spot which target needs practice.' }

  const practiceRounds = rounds.filter(round => round.type === 'practice' && round.game !== 'memory').slice(0, 6)
  const practice = practiceRounds.flatMap(round => round.attempts)
  const hits = practice.filter(([aim, landed]) => aim === landed).length
  const evidence = { rounds: practiceRounds.length, throws: practice.length, hits }
  if (practice.length < 6) {
    return { byTarget, coach, evidence, level: 2, levelDetail: 'Starting level for Practice.' }
  }

  // Overlapping fuzzy memberships keep one hit near a threshold from dominating.
  const rate = hits / practice.length
  const clamp = value => Math.max(0, Math.min(1, value))
  const struggling = clamp((0.62 - rate) / 0.37)
  const steady = clamp(Math.min((rate - 0.25) / 0.30, (0.90 - rate) / 0.35))
  const confident = clamp((rate - 0.55) / 0.35)
  const level = Math.round((struggling + 2 * steady + 3 * confident) / (struggling + steady + confident))
  const levelDetail = [
    'More 10-point targets in Precision or extra time in Target Chase.',
    'Balanced targets in Precision or 60 seconds in Target Chase.',
    'More 30-point targets in Precision or a quicker Target Chase.'
  ][level - 1]
  return { byTarget, coach, evidence, level, levelDetail }
}

export function boardHealth(board, online) {
  if (!online || !board?.sensors) {
    return { title: 'Waiting for the board', detail: 'Turn on the board and give it a moment to connect.' }
  }
  if (board.dropped) {
    return { title: 'Some rounds may be missing', detail: 'The board ran out of space while offline. Check its Wi-Fi connection.', warning: true }
  }
  const blocked = targets.filter((_, index) => board.sensors.blockedMs[index] >= 2000)
  const noisy = targets.filter((_, index) => board.sensors.quickPulses[index] >= 6)
  if (blocked.length) {
    return { title: 'Check for a blocked hole', detail: `Clear the ${blocked.join(', ')}-point target${blocked.length === 1 ? '' : 's'}.`, warning: true }
  }
  if (noisy.length) {
    return { title: 'Check a target sensor', detail: `The ${noisy.join(', ')}-point target${noisy.length === 1 ? ' is' : 's are'} switching unusually fast.`, warning: true }
  }
  return { title: 'All targets ready', detail: 'No unusual readings from the board.' }
}
