import { randomBytes } from 'node:crypto'
import { shotDetails } from './game.js'
import { memoryFlashMs, memoryLeadMs, memoryResult, memoryStepMs } from './memory.js'

function targetList(game, level, previous) {
  if (game === 'memory') {
    const colors = [10, 20, 30]
    return Array.from({ length: level + 2 }, () => {
      const choices = colors.filter(color => color !== previous)
      previous = choices[Math.floor(Math.random() * choices.length)]
      return previous
    })
  }
  const options = game === 'chase' ? [10, 20, 30] : [
    [10, 10, 20, 20, 30, 30],
    [10, 10, 10, 10, 20, 30],
    [10, 10, 20, 20, 30, 30],
    [10, 20, 30, 30, 30, 30]
  ][level]
  if (game === 'chase' && level === 0) return previous === 10 ? [20, 30, 10] : [10, 20, 30]
  for (let i = options.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1))
    const saved = options[i]
    options[i] = options[j]
    options[j] = saved
  }
  if (game === 'chase' && options[0] === previous) options.push(options.shift())
  return options
}

const ready = () => ({ phase: 'ready', game: 'precision', type: 'competition', player: 'Guest',
  roundId: '', level: 0, target: 0, score: 0, count: 0, elapsedMs: 0, durationMs: 0 })

export function createDemoBoard(store) {
  let state = ready()
  let round
  let order = []
  let receipt = null
  let started = 0
  let preview = []
  let reporting = false

  function apply(command) {
    if (command.kind === 'start' && state.phase !== 'playing'
        && (!command.roundId || (state.phase === 'results' && state.roundId === command.roundId))) {
      const { game, type, player, level } = command
      order = targetList(game, level, 0)
      preview = game === 'memory' ? [...order] : []
      const roundId = randomBytes(8).toString('hex')
      started = Date.now()
      round = { id: roundId, startId: command.id, game, type, player, level, status: 'active', attempts: [] }
      state = { ...ready(), phase: 'playing', roundId, game, type, player, level, target: order.shift(),
        previewing: preview.length > 0, startId: command.id,
        durationMs: game === 'chase' ? [60000, 75000, 60000, 45000][level] : 0 }
      return 'applied'
    }
    if (command.kind === 'correct' && command.roundId === state.roundId && state.count === command.count) {
      const attempt = round.attempts[command.attempt - 1]
      if (state.previewing || !attempt || attempt[1] !== attempt[0]) return 'stale'
      state.score -= attempt[1]
      attempt[1] = 0
      round.corrections = (round.corrections ?? 0) + 1
      if (state.phase === 'results') finish()
      return 'applied'
    }
    if (command.roundId !== state.roundId) return 'stale'
    if (command.kind === 'reset') {
      if (state.phase === 'playing') round.status = 'canceled'
      state = ready()
      return 'applied'
    }
    if (command.kind === 'miss' && state.count === command.count && state.phase === 'playing') return record(0)
    return 'stale'
  }

  function finish() {
    round.status = 'complete'
    state = { ...state, phase: 'results', target: 0, ...memoryResult(round) }
  }

  function record(landed) {
    const now = Date.now()
    if (state.phase !== 'playing' || state.previewing) return 'stale'
    if (state.durationMs && now - started >= state.durationMs) {
      finish()
      return 'stale'
    }
    const aim = state.target
    round.attempts.push([aim, landed, now - started])
    state.count++
    if (aim === landed) state.score += aim
    if (state.game !== 'chase' || aim === landed) advance()
    return 'applied'
  }

  function advance() {
    const limit = state.game === 'precision' ? 6 : state.level + 2
    if (state.game !== 'chase' && state.count >= limit) {
      finish()
      return
    }
    if (!order.length) order = targetList(state.game, state.level, state.target)
    state.target = order.shift()
  }

  async function update() {
    if (reporting) return
    reporting = true
    try {
      if (state.phase === 'playing') {
        state.elapsedMs = Date.now() - started
        if (state.previewing && state.elapsedMs >= memoryLeadMs + preview.length * memoryStepMs(state.level)) {
          state.previewing = false
          started = Date.now()
          state.elapsedMs = 0
        }
        if (state.durationMs && state.elapsedMs >= state.durationMs) finish()
      }
      const previewMs = state.elapsedMs - memoryLeadMs
      const previewStep = memoryStepMs(state.level)
      const lit = state.previewing && previewMs >= 0 && previewMs % previewStep < memoryFlashMs(state.level)
      const boardState = { ...state, ...shotDetails(state.roundId ? round : null) }
      const command = await store.sync({
        board: { version: 4, state: state.game === 'memory'
          ? { ...boardState, target: 0, previewTarget: lit ? preview[Math.floor(previewMs / previewStep)] : 0 }
          : boardState, sensors: { blockedMs: [0, 0, 0], quickPulses: [0, 0, 0] }, dropped: false },
        rounds: round ? [structuredClone(round)] : [],
        ack: receipt
      })
      if (command && command.id !== receipt?.id) {
        receipt = { id: command.id, result: apply(command) }
      }
    } finally {
      reporting = false
    }
  }

  const timer = setInterval(update, 100)
  timer.unref()
  update()
  return { async toss(landed) { record(landed); await update() }, stop() { clearInterval(timer) } }
}
