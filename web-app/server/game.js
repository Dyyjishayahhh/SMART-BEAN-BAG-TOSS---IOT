import { createHmac, randomUUID, timingSafeEqual } from 'node:crypto'
import { boardHealth, playerInsights, targets } from './intelligence.js'
import { hashPassword, identityService, validCredentials, verifyPassword } from './identity.js'
import { supabaseStore } from './store.js'
import { memoryMaxLevel, memoryResult } from './memory.js'

const games = ['precision', 'chase', 'memory']
const types = ['practice', 'competition']
const phases = ['ready', 'playing', 'results']
const roundStatuses = ['active', 'complete', 'canceled']
const receiptResults = ['applied', 'stale', 'wait', 'expired']
const onlineWindow = 17000
const cookie = 'ishoot_player'
const sessionSeconds = 8 * 3600
const maxLoggedAttempts = 384
const maxThrowCount = 15000
const boardRoundId = value => typeof value === 'string' && /^[a-f0-9]{16}$/.test(value)
const commandId = value => typeof value === 'string' && /^[a-f0-9-]{36}$/.test(value)
const validPlayer = name => typeof name === 'string' && /^[a-zA-Z0-9][a-zA-Z0-9 _-]{0,23}$/.test(name) && name === name.trim()
const validVersusName = name => typeof name === 'string' && name.trim().length > 0 && name.trim().length <= 24
const bounded = (value, max) => Number.isInteger(value) && value >= 0 && value <= max
const oneOf = (items, value) => items.includes(value)
const bodyLimit = 30000

class RequestError extends Error {
  constructor(status, message) {
    super(message)
    this.status = status
  }
}

function json(data, status = 200, headers = {}) {
  const outgoing = new Headers(headers)
  outgoing.set('cache-control', 'no-store')
  return Response.json(data, { status, headers: outgoing })
}

async function readBody(request) {
  if (!request.headers.get('content-type')?.toLowerCase().startsWith('application/json')) {
    throw new RequestError(415, 'Send a JSON request.')
  }
  const reader = request.body?.getReader()
  if (!reader) throw new RequestError(400, 'Missing request body.')
  const decoder = new TextDecoder()
  let length = 0
  let text = ''
  for (;;) {
    const chunk = await reader.read()
    if (chunk.done) break
    length += chunk.value.byteLength
    if (length > bodyLimit) {
      await reader.cancel()
      throw new RequestError(413, 'Request is too large.')
    }
    text += decoder.decode(chunk.value, { stream: true })
  }
  try {
    const body = JSON.parse(text + decoder.decode())
    if (!body || Array.isArray(body) || typeof body !== 'object') throw new Error()
    return body
  } catch {
    throw new RequestError(400, 'Invalid request body.')
  }
}

function sameSecret(given = '', expected) {
  if (typeof given !== 'string') return false
  const a = Buffer.from(given)
  const b = Buffer.from(expected)
  return a.length === b.length && timingSafeEqual(a, b)
}

function validatedReport(report) {
  const { board, rounds, ack } = report
  const state = board?.state
  const sensors = board?.sensors
  const maxMemory = [3, 4].includes(board?.version) ? memoryMaxLevel : 5
  const validCounts = values => Array.isArray(values) && values.length === 3 && values.every(value => bounded(value, 0xffffffff))
  if (!state || !oneOf(phases, state.phase) || !oneOf(games, state.game)
      || !oneOf(types, state.type) || !validPlayer(state.player)
      || !bounded(state.level, state.game === 'memory' ? maxMemory : 3) || !bounded(state.score, 500000)
      || !bounded(state.count, maxThrowCount) || !bounded(state.elapsedMs, 0xffffffff)
      || !oneOf([0, 45000, 60000, 75000], state.durationMs)
      || !oneOf([0, ...targets], state.target)
      || (state.startId && !commandId(state.startId))
      || (state.previewing !== undefined && typeof state.previewing !== 'boolean')
      || (board.version !== undefined && !oneOf([1, 2, 3, 4], board.version))
      || (state.phase === 'ready' ? state.roundId !== '' : !boardRoundId(state.roundId))
      || !validCounts(sensors?.blockedMs) || !validCounts(sensors?.quickPulses)
      || typeof board.dropped !== 'boolean') {
    throw new RequestError(400, 'Invalid board state.')
  }
  if (!Array.isArray(rounds) || rounds.length > 4 || rounds.some(round =>
    !round || !boardRoundId(round.id) || !validPlayer(round.player)
    || !oneOf(games, round.game) || !oneOf(types, round.type)
    || !bounded(round.level, round.game === 'memory' ? maxMemory : 3)
    || (round.startId && !commandId(round.startId))
    || !oneOf(roundStatuses, round.status)
    || (round.corrections !== undefined && !bounded(round.corrections, maxLoggedAttempts))
    || !Array.isArray(round.attempts) || round.attempts.length > maxLoggedAttempts
    || round.attempts.some(attempt => !Array.isArray(attempt) || attempt.length !== 3
      || !oneOf(targets, attempt[0]) || !oneOf([0, ...targets], attempt[1]) || !bounded(attempt[2], 0xffffffff)))) {
    throw new RequestError(400, 'Invalid round data.')
  }
  if (ack !== null && (!commandId(ack?.id) || !oneOf(receiptResults, ack.result))) {
    throw new RequestError(400, 'Invalid command receipt.')
  }
  const current = rounds.find(round => round.id === state.roundId)
  return {
    board: { version: board.version, sensors, dropped: board.dropped,
      state: { ...state, target: state.game === 'memory' ? 0 : state.target, ...shotDetails(current),
        ...memoryResult(current) } },
    rounds, ack
  }
}

export function shotDetails(current) {
  const latest = current?.attempts.at(-1)
  const lastThrow = latest ? { number: current.attempts.length, outcome: outcome(latest) } : null
  const faultIndex = current?.attempts.findLastIndex(attempt => outcome(attempt) !== 'hit') ?? -1
  const lastFault = faultIndex < 0 ? null : { number: faultIndex + 1, outcome: outcome(current.attempts[faultIndex]) }
  const shotOutcomes = current?.attempts.slice(-6).map(outcome) ?? []
  const hitIndex = current?.attempts.findLastIndex(attempt => outcome(attempt) === 'hit') ?? -1
  const lastHit = hitIndex < 0 ? null : { number: hitIndex + 1, points: current.attempts[hitIndex][0] }
  return { lastThrow, lastFault, shotOutcomes, lastHit, corrections: current?.corrections ?? 0 }
}

function outcome([aim, landed]) {
  return landed === 0 ? 'miss' : aim === landed ? 'hit' : 'wrong'
}

function summarizedRound(round) {
  const score = round.attempts.reduce((sum, [aim, landed]) => sum + (aim === landed ? aim : 0), 0)
  return { id: round.id, game: round.game, type: round.type, level: round.level, score, count: round.attempts.length }
}

function recentThrows(rounds) {
  return rounds.flatMap(round => round.attempts.slice().reverse().map(attempt => ({
    game: round.game, aimed: attempt[0], landed: attempt[1], outcome: outcome(attempt)
  }))).slice(0, 24)
}

export function createGateway({ store, accessCode, deviceToken, sessionKey, demoToss, realtime, secure = true }) {
  const signature = value => createHmac('sha256', deviceToken).update(`${accessCode}:${value}`).digest('base64url')
  const identity = identityService(sessionKey ?? signature('identity-v2'), secure)

  async function liveSettings(channel) {
    if (!realtime) return null
    const topic = (await store.liveTopics())?.[channel]
    return topic ? { ...realtime, topic } : null
  }


  function authenticated(request) {
    if (demoToss) return true
    const token = request.headers.get('cookie')?.split(';').map(value => value.trim())
      .find(value => value.startsWith(`${cookie}=`))?.slice(cookie.length + 1)
    const [expiry, mac, extra] = token?.split('.') ?? []
    return /^\d{10,12}$/.test(expiry) && Number(expiry) > Date.now() / 1000
      && extra === undefined && sameSecret(mac, signature(expiry))
  }

  return async function gateway(request) {
    try {
      const url = new URL(request.url)
      const action = url.searchParams.get('action')
      if (action === 'device' || action === 'device-live') {
        if (!sameSecret(request.headers.get('authorization') ?? '', `Bearer ${deviceToken}`)) {
          return json({ error: 'Board credentials did not match.' }, 401)
        }
        if (action === 'device-live') {
          if (request.method !== 'GET') throw new RequestError(405, 'Use GET.')
          return json({ live: await liveSettings('command') })
        }
        if (request.method !== 'POST') throw new RequestError(405, 'Use POST.')
        const command = await store.sync(validatedReport(await readBody(request)))
        return json({ command })
      }

      if (request.method === 'POST' && request.headers.get('origin') !== url.origin) {
        return json({ error: 'Open the game directly to use its controls.' }, 403)
      }
      if (action === 'join' && request.method === 'POST') {
        const { code } = await readBody(request)
        if (!sameSecret(code, accessCode)) return json({ error: 'That game code didn’t match.' }, 401)
        const expiry = String(Math.floor(Date.now() / 1000) + sessionSeconds)
        const options = `Path=/; HttpOnly; SameSite=Strict; Max-Age=${sessionSeconds}${secure ? '; Secure' : ''}`
        return json({ joined: true }, 200, { 'set-cookie': `${cookie}=${expiry}.${signature(expiry)}; ${options}` })
      }
      if (!authenticated(request)) return json({ error: 'Join the game to continue.' }, 401)
      if (action === 'player-live' && request.method === 'GET') return json({ live: await liveSettings('state') })
      const visitor = identity.read(request)
      if ((action === 'signup' || action === 'signin') && request.method === 'POST') {
        const { username, password } = await readBody(request)
        if (!validCredentials(username, password)) {
          throw new RequestError(400, 'Use a 3–24 character username with letters, numbers, underscores or hyphens, and a password of at least 8 characters.')
        }
        const key = username.toLowerCase()
        if (!await store.authBudget(visitor.guest.id, key)) {
          throw new RequestError(429, 'Too many sign-in attempts. Try again in a few minutes.')
        }
        let account
        if (action === 'signup') {
          account = await store.createAccount({ username, username_key: key, password_hash: await hashPassword(password) })
          if (!account) throw new RequestError(409, 'That username is already taken.')
        } else {
          account = await store.findAccount(key)
          if (!await verifyPassword(password, account?.password_hash)) {
            throw new RequestError(403, 'The username or password didn’t match.')
          }
        }
        const session = identity.signIn(visitor, account)
        const headers = new Headers(visitor.headers)
        headers.append('set-cookie', session.cookie)
        return json({ player: session.player }, 200, headers)
      }
      if (action === 'signout' && request.method === 'POST') {
        return json({ signedOut: true }, 200, { 'set-cookie': identity.signOut() })
      }
      if (action === 'leaderboard' && request.method === 'GET') {
        if (visitor.player.kind !== 'account') throw new RequestError(403, 'Sign in to view the leaderboard.')
        return json({ entries: await store.leaderboard() })
      }
      if (action === 'state' && request.method === 'GET') {
        const view = await store.view(visitor.player.kind === 'account' ? visitor.player.id : null)
        const online = Date.now() - Date.parse(view.seenAt) <= onlineWindow
        const pending = view.pending && Date.parse(view.pending.expiresAt) > Date.now() ? view.pending : null
        return json({
          online, demo: Boolean(demoToss), state: view.state ?? null, receipt: view.receipt ?? null,
          memoryReady: [3, 4].includes(view.version), versusReady: [2, 3, 4].includes(view.version), player: visitor.player,
          correctionReady: view.version === 4,
          memoryCanContinue: !view.memoryOwner || view.memoryOwner === visitor.player.id,
          versusOwned: view.versusOwner === 'g:' + visitor.guest.id,
          pending: pending ? { id: pending.id, kind: pending.kind, expiresAt: pending.expiresAt } : null,
          insights: playerInsights(view.rounds), health: boardHealth(view, online),
          history: view.rounds.slice(0, 8).map(summarizedRound), throws: recentThrows(view.rounds)
        }, 200, visitor.headers)
      }
      if (action === 'toss' && demoToss && request.method === 'POST') {
        const { landed } = await readBody(request)
        if (!oneOf(targets, landed)) throw new RequestError(400, 'Pick a target.')
        await demoToss(landed)
        return json({ recorded: true })
      }
      if (action === 'control' && request.method === 'POST') {
        const body = await readBody(request)
        const command = { id: randomUUID(), kind: body.kind, expiresAt: new Date(Date.now() + 16000).toISOString() }
        if (body.kind === 'start') {
          if (!oneOf(games, body.game) || !oneOf(types, body.type)) {
            throw new RequestError(400, 'Choose a play style and a game.')
          }
          const versus = body.versus === true
          if (versus && !oneOf([1, 2], body.slot)) {
            throw new RequestError(400, 'Choose Player 1 or Player 2 for the VS match.')
          }
          if (versus && body.game === 'memory') throw new RequestError(400, 'Color Memory is a single-player game.')
          if (versus && (!Array.isArray(body.names) || body.names.length !== 2 || !body.names.every(validVersusName))) {
            throw new RequestError(400, 'Enter a name for both players.')
          }
          const player = visitor.player
          const guest = !versus && player.kind === 'guest'
          const guestName = typeof body.nickname === 'string' ? body.nickname.trim() || 'Guest' : 'Guest'
          if (guest && !validPlayer(guestName)) throw new RequestError(400, 'Use letters, numbers, spaces, underscores, or hyphens for a guest name.')
          if (guest && body.entry !== undefined && !commandId(body.entry)) throw new RequestError(400, 'Choose a new guest session.')
          const type = versus || guest ? 'competition' : body.type
          let level = 0
          if (body.game === 'memory') {
            level = 1
            if (body.continueRoundId !== undefined) {
              const view = await store.view(null)
              const state = view.state
              if (!boardRoundId(body.continueRoundId) || state?.roundId !== body.continueRoundId
                  || state.phase !== 'results' || state.game !== 'memory' || state.type !== type
                  || !bounded(state.nextMemoryLevel, memoryMaxLevel) || !state.nextMemoryLevel
                  || (view.memoryOwner && view.memoryOwner !== player.id)) {
                throw new RequestError(409, 'This Memory round has changed. Start again at Level 1.')
              }
              level = state.nextMemoryLevel
              command.roundId = state.roundId
            }
          } else if (type === 'practice') {
            level = playerInsights((await store.view(player.id)).rounds).level
          }
          Object.assign(command, { player: versus ? 'Player ' + body.slot : guest ? guestName : player.name,
            ownerKey: versus ? 'g:' + visitor.guest.id : player.id, game: body.game, type, level, versus,
            ...(versus ? { slot: body.slot } : {}),
            ...(guest ? { guestRankKey: player.id + ':' + (body.entry ?? command.id) } : {}) })
        } else if (body.kind === 'correct') {
          if (!boardRoundId(body.roundId) || !bounded(body.count, maxThrowCount)
              || !bounded(body.attempt, maxLoggedAttempts) || body.attempt === 0) {
            throw new RequestError(400, 'Choose the current round’s last scored hit.')
          }
          Object.assign(command, { roundId: body.roundId, count: body.count, attempt: body.attempt })
        } else if (body.kind === 'reset' || body.kind === 'miss') {
          if (!boardRoundId(body.roundId) || !bounded(body.count, maxThrowCount)) throw new RequestError(400, 'The round has changed.')
          Object.assign(command, { roundId: body.roundId, count: body.count, visitorKey: 'g:' + visitor.guest.id })
        } else {
          throw new RequestError(400, 'Unknown game control.')
        }
        const result = await store.queue(command)
        if (result === 'queued') return json({ id: command.id, expiresAt: command.expiresAt }, 202, visitor.headers)
        if (result === 'offline') return json({ error: 'The board is offline. Check that it’s powered on.' }, 409)
        if (result === 'busy') return json({ error: 'Give the board a moment to respond.' }, 409)
        if (result === 'upgrade') return json({ error: 'Update the board firmware to use that game or play style.' }, 409)
        if (result === 'owned') return json({ error: 'Use the screen that started this VS match to control it.' }, 403)
        return json({ error: 'The round has changed. Check the current round and try again.' }, 409)
      }
      return json({ error: 'This game action is unavailable.' }, 404)
    } catch (error) {
      if (error instanceof RequestError) return json({ error: error.message }, error.status)
      console.error('I-Shoot Mo request failed', error.name)
      return json({ error: 'The game server is temporarily unavailable.' }, 503)
    }
  }
}

let live
export function handleGame(request) {
  const url = process.env.SUPABASE_URL?.trim()
  const secret = process.env.SUPABASE_SECRET_KEY?.trim()
  const accessCode = process.env.PLAYER_ACCESS_CODE?.trim() ?? ''
  const deviceToken = process.env.DEVICE_TOKEN?.trim() ?? ''
  const publishableKey = process.env.SUPABASE_PUBLISHABLE_KEY?.trim() ?? ''
  if (!url || !secret || accessCode.length < 12 || deviceToken.length < 32) {
    return json({ error: 'The game server is waiting for its deployment settings.' }, 503)
  }
  const realtime = /^sb_publishable_[\w-]+$/.test(publishableKey)
    ? { host: new URL(url).hostname, key: publishableKey } : undefined
  live ??= createGateway({ store: supabaseStore(url, secret), accessCode, deviceToken,
    realtime, sessionKey: createHmac('sha256', secret).update('ishoot-player-sessions:' + accessCode).digest() })
  return live(request)
}
