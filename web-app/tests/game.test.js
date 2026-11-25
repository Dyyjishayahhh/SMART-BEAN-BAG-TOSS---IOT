import { test } from 'node:test'
import assert from 'node:assert/strict'
import { createGateway } from '../server/game.js'
import { createMemoryStore } from '../server/store.js'
import { boardHealth, playerInsights } from '../server/intelligence.js'

const origin = 'https://game.example'
const id = '0123456789abcdef'
const settings = { accessCode: 'a-test-access-code', deviceToken: 'a-board-token-for-automated-tests' }
const makeState = patch => ({ roundId: '', phase: 'ready', game: 'precision', type: 'competition',
  player: 'Guest', level: 0, score: 0, count: 0, elapsedMs: 0, durationMs: 0, target: 0, ...patch })
const makeReport = (state, rounds = [], ack = null, blockedMs = [0, 0, 0]) => ({
  board: { state, sensors: { blockedMs, quickPulses: [0, 0, 0] }, dropped: false }, rounds, ack
})
const makeRound = (attempts, status = 'complete') => ({
  id, player: 'Guest', status, game: 'precision', type: 'practice', level: 2, attempts
})

test('browser authentication, board authentication and request origin', async () => {
  const gateway = createGateway({ store: createMemoryStore(), ...settings })
  const req = (query, options) => gateway(new Request(`${origin}/api/game?${query}`, options))
  assert.equal((await req('action=state')).status, 401)
  assert.equal((await req('action=device', { method: 'POST' })).status, 401)
  assert.equal((await req('action=join', { method: 'POST', headers: { origin: 'https://elsewhere.example' } })).status, 403)
  const response = await req('action=join', {
    method: 'POST', headers: { origin, 'content-type': 'application/json' },
    body: JSON.stringify({ code: settings.accessCode })
  })
  assert.equal(response.status, 200)
  const cookie = response.headers.get('set-cookie')
  assert.match(cookie, /HttpOnly; SameSite=Strict;.*Secure/)
  assert.equal((await req('action=state', { headers: { cookie: cookie.split(';')[0] } })).status, 200)
})

test('live settings keep the player and cabinet channels separate', async () => {
  const topics = { state: 'state-example', command: 'command-example' }
  const realtime = { host: 'example.supabase.co', key: 'sb_publishable_example' }
  const store = { async liveTopics() { return topics } }
  const gateway = createGateway({ store, ...settings, realtime })
  const request = (action, options) => gateway(new Request(`${origin}/api/game?action=${action}`, options))
  const boardHeaders = { authorization: 'Bearer ' + settings.deviceToken }
  assert.equal((await request('player-live')).status, 401)
  assert.equal((await request('device-live')).status, 401)
  const join = await request('join', {
    method: 'POST', headers: { origin, 'content-type': 'application/json' },
    body: JSON.stringify({ code: settings.accessCode })
  })
  const cookie = join.headers.get('set-cookie').split(';')[0]
  assert.equal((await request('device-live', { headers: { cookie } })).status, 401)
  assert.equal((await request('player-live', { headers: boardHeaders })).status, 401)
  const player = await request('player-live', { headers: { cookie } })
  const device = await request('device-live', { headers: boardHeaders })
  assert.deepEqual(await player.json(), { live: { ...realtime, topic: topics.state } })
  assert.deepEqual(await device.json(), { live: { ...realtime, topic: topics.command } })
})

test('a joined guest can correct the last scored hit in one action', async () => {
  const gateway = createGateway({ store: createMemoryStore(), ...settings })
  const cookies = new Map()
  async function call(action, body, device = false) {
    const headers = { origin, 'content-type': 'application/json', cookie: [...cookies].map(pair => pair.join('=')).join('; ') }
    if (device) headers.authorization = 'Bearer ' + settings.deviceToken
    const response = await gateway(new Request(`${origin}/api/game?action=${action}`, {
      method: body ? 'POST' : 'GET', headers, body: body && JSON.stringify(body)
    }))
    for (const header of response.headers.getSetCookie()) {
      const [name, value] = header.split(';')[0].split('=')
      cookies.set(name, value)
    }
    return { status: response.status, data: await response.json() }
  }
  const playing = makeState({ roundId: id, phase: 'playing', score: 30, count: 1, target: 20 })
  const shot = makeRound([[30, 30, 123]], 'active')
  const report = makeReport(playing, [shot])
  report.board.version = 4
  const command = { kind: 'correct', roundId: id, attempt: 1, count: 1 }
  assert.equal((await call('device', report, true)).status, 200)
  assert.equal((await call('control', command)).status, 401)
  assert.equal((await call('join', { code: settings.accessCode })).status, 200)
  const before = (await call('state')).data
  assert.equal(before.player.kind, 'guest')
  assert.equal(before.correctionReady, true)
  assert.deepEqual(before.state.lastHit, { number: 1, points: 30 })
  assert.equal((await call('control', { ...command, attempt: 2 })).status, 409)
  const queued = await call('control', command)
  assert.equal(queued.status, 202)
  assert.equal((await call('control', command)).status, 409)
  const corrected = makeReport({ ...playing, score: 0 },
    [{ ...shot, corrections: 1, attempts: [[30, 0, 123]] }],
    { id: queued.data.id, result: 'applied' })
  corrected.board.version = 4
  assert.equal((await call('device', corrected, true)).status, 200)
  const after = (await call('state')).data
  assert.equal(after.state.score, 0)
  assert.equal(6 - after.state.count, 5)
  assert.equal(after.state.target, playing.target)
  assert.deepEqual(after.state.lastThrow, { number: 1, outcome: 'miss' })
  assert.equal((await call('control', command)).status, 409)
})

test('both start paths, scoped controls, acknowledgement and durable-round semantics', async () => {
  const store = createMemoryStore()
  const gateway = createGateway({ store, ...settings, demoToss: () => {} })
  const call = async (action, body, device) => {
    const response = await gateway(new Request(`${origin}/api/game?action=${action}`, {
      method: body ? 'POST' : 'GET', headers: { origin, 'content-type': 'application/json',
        ...(device ? { authorization: `Bearer ${settings.deviceToken}` } : {}) },
      body: body ? JSON.stringify(body) : undefined
    }))
    return { status: response.status, data: await response.json() }
  }
  const send = report => call('device', report, true)
  const state = () => call('state')
  const control = body => call('control', body)
  const start = { kind: 'start', type: 'practice', game: 'precision' }
  assert.equal((await control(start)).status, 409)
  await send(makeReport(makeState()))

  const starts = await Promise.all([control(start), control(start)])
  assert.deepEqual(starts.map(result => result.status).sort(), [202, 409])
  const firstId = starts.find(result => result.status === 202).data.id
  const received = await send(makeReport(makeState()))
  assert.equal(received.data.command.level, 0)
  const playing = makeState({ roundId: id, phase: 'playing', type: 'practice', level: 2, target: 10 })
  await send(makeReport(playing, [makeRound([], 'active')], { id: firstId, result: 'applied' }))
  assert.equal((await state()).data.receipt.result, 'applied')

  assert.equal((await control({ kind: 'miss', roundId: id, count: 1 })).status, 409)
  const miss = await control({ kind: 'miss', roundId: id, count: 0 })
  assert.equal(miss.status, 202)
  const attempt = [10, 0, 630]
  await send(makeReport({ ...playing, count: 1, target: 20 }, [makeRound([attempt], 'active')], { id: miss.data.id, result: 'applied' }))
  assert.equal((await state()).data.state.count, 1)

  const reset = await control({ kind: 'reset', roundId: id, count: 1 })
  assert.equal(reset.status, 202)
  await send(makeReport(makeState(), [makeRound([attempt], 'canceled')], { id: reset.data.id, result: 'applied' }))
  assert.equal((await state()).data.history.length, 0)

  // Another round can begin physically without a queued Start request.
  const physicalId = 'abcdef0123456789'
  const physical = { ...playing, roundId: physicalId, type: 'competition', level: 0 }
  await send(makeReport(physical))
  assert.equal((await state()).data.state.roundId, physicalId)
  const saved = { ...makeRound([[10, 10, 800], [20, 30, 1600], [30, 30, 2400]]), id: physicalId, type: 'competition', level: 0 }
  const results = makeReport({ ...physical, phase: 'results', count: 3, score: 40, target: 0 }, [saved])
  await send(results)
  await send(results)
  const finished = (await state()).data
  assert.deepEqual(finished.history, [])
  assert.deepEqual(finished.state.lastThrow, { number: 3, outcome: 'hit' })
  assert.deepEqual(finished.state.lastFault, { number: 2, outcome: 'wrong' })
  assert.deepEqual(finished.state.shotOutcomes, ['hit', 'wrong', 'hit'])
  await send(makeReport(makeState(), [], null, [0, 2400, 0]))
  assert.match((await state()).data.health.detail, /20-point/)
})

test('account history stays private, guest scores can rank, and VS rounds are temporary', async () => {
  const gateway = createGateway({ store: createMemoryStore(), ...settings, demoToss: () => {} })
  const browser = () => {
    const cookies = new Map()
    return async (action, body) => {
      const response = await gateway(new Request(`${origin}/api/game?action=${action}`, {
        method: body ? 'POST' : 'GET',
        headers: { origin, 'content-type': 'application/json', cookie: [...cookies].map(pair => pair.join('=')).join('; ') },
        body: body && JSON.stringify(body)
      }))
      for (const header of response.headers.getSetCookie()) {
        const [name, value] = header.split(';')[0].split('=')
        cookies.set(name, value)
      }
      return { status: response.status, data: await response.json() }
    }
  }
  const send = async (state, rounds = [], ack = null) => {
    const report = makeReport(state, rounds, ack)
    report.board.version = 2
    const response = await gateway(new Request(`${origin}/api/game?action=device`, {
      method: 'POST', headers: { authorization: `Bearer ${settings.deviceToken}`, 'content-type': 'application/json' },
      body: JSON.stringify(report)
    }))
    assert.equal(response.status, 200)
    return response.json()
  }
  const shared = browser()
  const other = browser()
  await send(makeState())
  assert.equal((await shared('state')).data.player.kind, 'guest')
  const password = 'test-passphrase'
  const alice = (await shared('signup', { username: 'Alice', password })).data.player
  assert.equal(alice.name, 'Alice')
  const start = { kind: 'start', type: 'competition', game: 'chase' }
  assert.equal((await shared('control', start)).status, 202)
  const { command } = await send(makeState())
  assert.equal(command.player, 'Alice')
  const result = { ...makeRound([[10, 10, 900], [20, 30, 2100]]),
    startId: command.id, player: 'Alice', game: 'chase', type: 'competition', level: 0 }
  await send(makeState({ player: 'Alice', phase: 'results', roundId: id, game: 'chase', score: 10, count: 2 }),
    [result], { id: command.id, result: 'applied' })
  assert.equal((await shared('signout', {})).status, 200)
  const bob = (await shared('signup', { username: 'Bob', password })).data.player
  assert.equal(bob.name, 'Bob')
  const bobView = (await shared('state&player=Alice')).data
  assert.equal(bobView.player.name, 'Bob')
  assert.deepEqual(bobView.history, [])
  assert.deepEqual(bobView.throws, [])
  assert.deepEqual((await shared('leaderboard')).data.entries, [{ player: 'Alice', score: 10, rank: 1 }])
  assert.equal((await other('signin', { username: 'alice', password: 'wrong-password' })).status, 403)
  assert.equal((await other('signin', { username: 'alice', password })).status, 200)
  const aliceView = (await other('state')).data
  assert.equal(aliceView.history[0].score, 10)
  assert.equal(aliceView.throws[0].outcome, 'wrong')
  assert.equal(aliceView.insights.byTarget.find(row => row.target === 30).landed, 1)

  const guest = browser()
  assert.equal((await guest('state')).data.player.kind, 'guest')
  assert.equal((await guest('leaderboard')).status, 403)
  assert.equal((await guest('control', { ...start, versus: true, slot: 3 })).status, 400)
  assert.equal((await guest('control', { ...start, game: 'memory', level: 1, versus: true, slot: 1 })).status, 400)
  assert.equal((await guest('control', { ...start, versus: true, slot: 1, names: ['One', '  '] })).status, 400)
  const vsAccepted = await guest('control', { ...start, versus: true, slot: 1, names: ['One', 'Two'], type: 'practice' })
  assert.equal(vsAccepted.status, 202)
  const versus = (await send(makeState())).command
  assert.equal(versus.player, 'Player 1')
  assert.equal(versus.slot, 1)
  assert.equal(versus.names, undefined)
  assert.equal(versus.type, 'competition')
  const versusId = '5566778899aabbcc'
  const versusRound = { ...result, id: versusId, startId: versus.id, player: versus.player, attempts: [[30, 30, 1000]] }
  const vsState = makeState({ player: versus.player, phase: 'results', roundId: versusId, game: 'chase', score: 30, count: 1 })
  await send(vsState, [versusRound], { id: versus.id, result: 'applied' })
  const guestView = (await guest('state')).data
  assert.deepEqual(guestView.history, [])
  assert.equal(guestView.versusOwned, true)
  assert.equal(guestView.state.versus, true)
  assert.equal(guestView.state.score, 30)
  assert.equal((await other('state')).data.versusOwned, false)
  const back = { kind: 'reset', roundId: versusId, count: 1 }
  assert.equal((await other('control', back)).status, 403)
  const reset = await guest('control', back)
  assert.equal(reset.status, 202)
  await send(makeState(), [versusRound], { id: reset.data.id, result: 'applied' })
  assert.equal((await guest('state')).data.state.versus, undefined)
  assert.deepEqual((await shared('leaderboard')).data.entries, [{ player: 'Alice', score: 10, rank: 1 }])

  const guestStart = await guest('control', { ...start, nickname: 'Fast Guest', type: 'practice' })
  assert.equal(guestStart.status, 202)
  const guestCommand = (await send(makeState())).command
  assert.equal(guestCommand.player, 'Fast Guest')
  assert.equal(guestCommand.type, 'competition')
  const guestId = '9988776655443322'
  const guestRound = { ...versusRound, id: guestId, startId: guestCommand.id, player: guestCommand.player }
  const guestState = { ...vsState, player: guestCommand.player, roundId: guestId }
  await send(guestState, [guestRound], { id: guestCommand.id, result: 'applied' })
  const freshView = (await guest('state')).data
  assert.equal(freshView.player.name, 'Guest')
  assert.equal(freshView.state.guestRound, true)
  assert.deepEqual(freshView.history, [])
  assert.deepEqual(freshView.throws, [])
  assert.deepEqual((await shared('leaderboard')).data.entries,
    [{ player: 'Fast Guest (Guest)', score: 30, rank: 1 }, { player: 'Alice', score: 10, rank: 2 }])
})

test('Color Memory starts at Level 1, retries imperfect rounds, and advances through Level 10', async () => {
  const gateway = createGateway({ store: createMemoryStore(), ...settings, demoToss: () => {} })
  let cookie = ''
  const browser = async (action, body, knownPlayer = true) => {
    const response = await gateway(new Request(`${origin}/api/game?action=${action}`, {
      method: body ? 'POST' : 'GET', headers: { origin, 'content-type': 'application/json', cookie: knownPlayer ? cookie : '' },
      body: body && JSON.stringify(body)
    }))
    if (knownPlayer && response.headers.has('set-cookie')) cookie = response.headers.get('set-cookie').split(';')[0]
    return { status: response.status, data: await response.json() }
  }
  const send = async (state, rounds = [], ack = null, version = 3) => {
    const report = makeReport(state, rounds, ack)
    report.board.version = version
    const response = await gateway(new Request(`${origin}/api/game?action=device`, {
      method: 'POST', headers: { authorization: `Bearer ${settings.deviceToken}`, 'content-type': 'application/json' },
      body: JSON.stringify(report)
    }))
    assert.equal(response.status, 200)
    return response.json()
  }
  const start = { kind: 'start', game: 'memory', type: 'competition', level: 10 }
  await send(makeState(), [], null, 2)
  assert.equal((await browser('state')).data.memoryReady, false)
  assert.equal((await browser('control', start)).status, 409)
  await send(makeState())
  assert.equal((await browser('state')).data.memoryReady, true)
  assert.equal((await browser('control', { ...start, continueRoundId: id })).status, 409)
  assert.equal((await browser('control', start)).status, 202)
  let state = makeState()
  let latestRounds = []
  const levels = [1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10]
  for (const [index, level] of levels.entries()) {
    const { command } = await send(state, latestRounds)
    assert.equal(command.level, level)
    const roundId = String(index + 1).padStart(16, '0')
    const attempts = Array.from({ length: level + 2 }, (_, step) => {
      const target = [10, 20, 30][step % 3]
      return [target, index === 1 && step === 0 ? 0 : target, step * 650]
    })
    latestRounds = [{ id: roundId, startId: command.id, player: 'Guest', level, game: 'memory',
      type: 'competition', status: 'complete', attempts }]
    state = makeState({ roundId, phase: 'results', game: 'memory', level, count: attempts.length })
    await send(state, latestRounds, { id: command.id, result: 'applied' })
    const view = (await browser('state')).data
    const next = levels[index + 1] ?? 1
    assert.equal(view.state.nextMemoryLevel, next)
    assert.equal(view.state.memoryCleared, index !== 1)
    assert.equal(view.memoryCanContinue, true)
    const continuation = { ...start, continueRoundId: roundId }
    assert.equal((await browser('control', continuation, false)).status, 409)
    assert.equal((await browser('control', continuation)).status, 202)
  }
  assert.equal((await send(state, latestRounds)).command.level, 1)
})

test('practice adapts with history, coach uses target performance, and sensors flag abnormal readings', () => {
  const weak = makeRound([[10, 0, 100], [10, 20, 800], [10, 0, 1500],
    [20, 20, 2200], [20, 0, 2900], [30, 0, 3600]])
  const confident = makeRound([10, 20, 30, 10, 20, 30].map((target, index) => [target, target, index * 800]))
  assert.equal(playerInsights([]).level, 2)
  assert.equal(playerInsights([weak]).level, 1)
  assert.equal(playerInsights([confident]).level, 3)
  assert.match(playerInsights([weak]).coach.title, /10-point/)
  assert.equal(playerInsights([{ ...weak, type: 'competition' }]).level, 2)
  assert.match(boardHealth({ sensors: { blockedMs: [0, 0, 0], quickPulses: [6, 0, 0] } }, true).detail, /10-point/)
  assert.equal(boardHealth({}, false).title, 'Waiting for the board')
})
