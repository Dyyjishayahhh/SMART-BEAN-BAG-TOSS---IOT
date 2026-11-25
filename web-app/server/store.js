import { randomUUID } from 'node:crypto'

const recent = (rounds, owner) => rounds
  .filter(round => round.status === 'complete' && round.ownerKey === owner)
  .slice(0, 18)

export function createMemoryStore() {
  let board = {}
  let pending = null
  let receipt = null
  const rounds = new Map()
  const guestScores = new Map()
  const accounts = new Map()
  const tickets = new Map()
  const attempts = new Map()
  return {
    async view(owner) {
      return { ...board, receipt, pending, rounds: owner ? recent([...rounds.values()].reverse(), owner) : [] }
    },
    async createAccount(account) {
      if (accounts.has(account.username_key)) return null
      const entry = { ...account, id: randomUUID() }
      accounts.set(account.username_key, entry)
      return entry
    },
    async findAccount(key) {
      return accounts.get(key)
    },
    async authBudget(browser, key) {
      const window = Math.floor(Date.now() / 600000)
      return [[`browser:${browser}`, 18], [`name:${key}`, 8]].map(([id, limit]) => {
        const previous = attempts.get(id)
        const count = previous?.window === window ? previous.count + 1 : 1
        attempts.set(id, { count, window })
        return count <= limit
      }).every(Boolean)
    },
    async leaderboard() {
      const best = new Map()
      for (const row of guestScores.values()) {
        const player = row.player === 'Guest' ? row.player : row.player + ' (Guest)'
        if (!best.has(row.key) || row.score > best.get(row.key).score) best.set(row.key, { player, score: row.score })
      }
      for (const round of rounds.values()) {
        if (round.status !== 'complete' || round.game !== 'chase' || round.type !== 'competition') continue
        const key = round.ownerKey ?? 'legacy:' + round.player.toLowerCase()
        const score = round.attempts.reduce((total, [aim, hit]) => total + (aim === hit ? aim : 0), 0)
        if (!best.has(key) || score > best.get(key).score) best.set(key, { player: round.player, score })
      }
      let rank = 0
      let previous
      return [...best.values()].sort((a, b) => b.score - a.score).slice(0, 25).map(row => {
        if (row.score !== previous) rank++
        previous = row.score
        return { ...row, rank }
      })
    },
    async queue(command) {
      if (!board.seenAt || Date.now() - Date.parse(board.seenAt) > 17000) return 'offline'
      if (pending && Date.parse(pending.expiresAt) > Date.now()) return 'busy'
      if (command.kind === 'start' && (command.game === 'memory' && ![3, 4].includes(board.version)
          || command.versus && ![2, 3, 4].includes(board.version))) return 'upgrade'
      if (command.kind === 'correct' && board.version !== 4) return 'upgrade'
      if (!commandFits(command, board.state)) return 'stale'
      if (['miss', 'reset'].includes(command.kind) && board.versusOwner && board.versusOwner !== command.visitorKey) return 'owned'
      pending = command
      if (command.kind === 'start' && command.ownerKey) tickets.set(command.id, { ...command, roundId: '' })
      return 'queued'
    },
    async sync(report) {
      const fallback = pending?.kind === 'start' && report.ack?.id === pending.id
        && report.ack.result === 'applied' ? pending.id : null
      if (pending && pending.id === report.ack?.id) {
        receipt = report.ack
        pending = null
      }
      if (pending && Date.parse(pending.expiresAt) <= Date.now()) {
        receipt = { id: pending.id, result: 'expired' }
        pending = null
      }
      board = { ...report.board, seenAt: new Date().toISOString() }
      for (const round of report.rounds) {
        const previous = rounds.get(round.id)
        const corrections = round.corrections ?? 0
        const ticket = tickets.get(round.startId || (round.id === board.state.roundId ? fallback : null))
        const valid = ticket && ticket.player === round.player && ticket.game === round.game
          && (!ticket.roundId || ticket.roundId === round.id)
        if (valid) ticket.roundId = round.id
        if (valid && round.game === 'memory' && round.id === board.state.roundId) board.memoryOwner = ticket.ownerKey
        if (valid && ticket.versus) {
          if (round.id === board.state.roundId) {
            board.versusOwner = ticket.ownerKey
            board.state = { ...board.state, startId: ticket.id, versus: true }
          }
          continue
        }
        const guestRankKey = valid ? ticket.guestRankKey : !round.startId && round.player === 'Guest' ? 'cabinet:guest' : null
        if (guestRankKey) {
          if (round.id === board.state.roundId) {
            board.state = { ...board.state, guestRound: true, startId: valid ? ticket.id : '' }
          }
          if (round.status === 'complete' && round.game === 'chase' && round.type === 'competition') {
            const score = round.attempts.reduce((total, [aim, hit]) => total + (aim === hit ? aim : 0), 0)
            const previousScore = guestScores.get(round.id)
            if (!previousScore || (previousScore.key === guestRankKey && previousScore.corrections < corrections && score < previousScore.score)) {
              guestScores.set(round.id, { key: guestRankKey, player: round.player, score, corrections })
            }
          }
          continue
        }
        const lastRevision = previous?.corrections ?? 0
        if (!previous || (previous.player === round.player && previous.attempts.length <= round.attempts.length
            && ((previous.status === 'active' && corrections >= lastRevision)
                || (previous.status === 'complete' && round.status === 'complete' && corrections > lastRevision)))) {
          rounds.delete(round.id)
          rounds.set(round.id, { ...round, startId: previous?.startId || (valid ? ticket.id : ''), ownerKey: previous?.ownerKey || (valid ? ticket.ownerKey : null) })
        }
      }
      const live = rounds.get(board.state.roundId)
      if (live?.startId) board.state = { ...board.state, startId: live.startId }
      return pending
    }
  }
}

function commandFits(command, state) {
  if (!state) return false
  if (command.kind === 'start') return state.phase !== 'playing'
    && (!command.roundId || (state.phase === 'results' && state.roundId === command.roundId))
  if (command.roundId !== state.roundId) return false
  if (command.kind === 'reset') return state.phase !== 'ready'
  if (command.kind === 'correct') return state.phase !== 'ready' && !state.previewing
    && command.count === state.count && command.attempt === state.lastHit?.number
  return state.phase === 'playing' && !state.previewing && command.count === state.count
}

export function supabaseStore(url, secret) {
  const rest = new URL('/rest/v1/', url)
  if (rest.protocol !== 'https:') throw new Error('SUPABASE_URL must use HTTPS')

  async function request(path, body, options = {}) {
    const response = await fetch(new URL(path, rest), {
      method: body === undefined ? 'GET' : 'POST',
      headers: { apikey: secret, 'content-type': 'application/json', ...options },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(7000)
    })
    if (!response.ok) {
      console.error('Supabase request failed', response.status, path.split('?')[0])
      throw new Error('Supabase request failed')
    }
    return response.json()
  }

  return {
    async view(owner) {
      const query = new URLSearchParams({
        select: 'payload', owner_key: `eq.${owner}`, status: 'eq.complete',
        order: 'updated_at.desc', limit: '18'
      })
      const [[board], rounds] = await Promise.all([
        request('ishoot_board?select=board,receipt,pending,seen_at&id=eq.1'),
        owner ? request(`ishoot_rounds?${query}`) : []
      ])
      return { ...board?.board, seenAt: board?.seen_at, receipt: board?.receipt,
        pending: board?.pending, rounds: rounds.map(round => round.payload) }
    },
    async createAccount(account) {
      const rows = await request('ishoot_players?on_conflict=username_key&select=id,username', account,
        { prefer: 'resolution=ignore-duplicates,return=representation' })
      return rows[0] ?? null
    },
    async findAccount(key) {
      const query = new URLSearchParams({ select: 'id,username,password_hash', username_key: `eq.${key}`, limit: '1' })
      const rows = await request(`ishoot_players?${query}`)
      return rows[0] ?? null
    },
    authBudget(browser, key) {
      return request('rpc/ishoot_auth_budget', { p_browser: browser, p_username: key })
    },
    leaderboard() {
      return request('rpc/ishoot_chase_leaderboard', {})
    },
    liveTopics() {
      return request('rpc/ishoot_live_topics', {})
    },
    queue(command) {
      return request('rpc/ishoot_queue', { p_command: command })
    },
    sync({ board, rounds, ack }) {
      return request('rpc/ishoot_sync', { p_board: board, p_rounds: rounds, p_ack: ack })
    }
  }
}
