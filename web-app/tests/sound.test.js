import { test } from 'node:test'
import assert from 'node:assert/strict'
import { createGameAudio } from '../public/sound.js'

function setupAudio(t) {
  const played = []
  const stopped = []
  const gains = []
  const files = {
    '/sounds/hit-1.mp3?v=2': 1, '/sounds/hit-2.mp3?v=2': 2, '/sounds/hit-3.mp3?v=2': 3,
    '/sounds/miss.mp3?v=2': 4, '/sounds/miss-2.mp3?v=2': 5, '/sounds/miss-3.mp3?v=2': 6, '/sounds/miss-4.mp3?v=2': 7
  }
  t.mock.method(globalThis, 'fetch', async path => files[path] ? new Response(Uint8Array.of(files[path])) : new Response(null, { status: 404 }))
  let sample = 0
  t.mock.method(Math, 'random', () => sample)

  class TestAudioContext {
    state = 'suspended'
    currentTime = 0
    destination = {}

    async resume() { this.state = 'running' }
    async decodeAudioData(bytes) { return new Uint8Array(bytes)[0] }
    createDynamicsCompressor() { return { connect() {} } }
    createMediaElementSource() { return { connect() {} } }
    createGain() {
      const gain = { value: 1, setTargetAtTime(value) { this.value = value } }
      gains.push(gain)
      return { gain, connect() {} }
    }
    createBufferSource() {
      return {
        connect() {},
        disconnect() {},
        start() { played.push(this.buffer) },
        stop() { stopped.push(this.buffer) }
      }
    }
  }

  const originalAudioContext = Object.getOwnPropertyDescriptor(globalThis, 'AudioContext')
  const originalStorage = Object.getOwnPropertyDescriptor(globalThis, 'localStorage')
  const storage = new Map()
  Object.defineProperty(globalThis, 'AudioContext', { value: TestAudioContext, configurable: true })
  Object.defineProperty(globalThis, 'localStorage', {
    value: { getItem: key => storage.get(key), setItem: (key, value) => storage.set(key, value) },
    configurable: true
  })
  t.after(() => {
    if (originalAudioContext) Object.defineProperty(globalThis, 'AudioContext', originalAudioContext)
    else delete globalThis.AudioContext
    if (originalStorage) Object.defineProperty(globalThis, 'localStorage', originalStorage)
    else delete globalThis.localStorage
  })

  const music = {
    paused: true,
    async play() { this.paused = false },
    pause() { this.paused = true }
  }
  const toggle = new EventTarget()
  toggle.setAttribute = () => {}
  const audio = createGameAudio(music, toggle)
  return { played, stopped, gains, music, toggle, audio, choose(value) { sample = value } }
}

function reportedRound(game, roundId, outcomes = []) {
  return { game, roundId, count: outcomes.length, shotOutcomes: outcomes,
    lastThrow: outcomes.length ? { number: outcomes.length, outcome: outcomes.at(-1) } : null }
}

test('regular shots select from the new hit and miss pools, and the speaker control mutes them', async t => {
  const { audio, played, stopped, music, toggle, choose } = setupAudio(t)
  await audio.updateRound(null)

  audio.start()
  const outcomes = ['hit', 'hit', 'hit', 'miss', 'wrong', 'miss', 'wrong']
  const samples = [0, 0.4, 0.99, 0, 0.26, 0.55, 0.99]
  for (const [index, sample] of samples.entries()) {
    choose(sample)
    await audio.updateRound(reportedRound('chase', 'round-1', outcomes.slice(0, index + 1)))
  }
  assert.deepEqual(played, [1, 2, 3, 4, 5, 6, 7])
  assert.equal(music.paused, false)

  toggle.dispatchEvent(new Event('click'))
  audio.start()
  await audio.updateRound(reportedRound('chase', 'round-1', [...outcomes, 'hit']))
  assert.deepEqual(stopped, played)
  assert.equal(played.length, 7)
  assert.equal(music.paused, true)

  toggle.dispatchEvent(new Event('click'))
  await audio.updateRound(reportedRound('chase', 'round-1', [...outcomes, 'hit', 'hit']))
  assert.deepEqual(played, [1, 2, 3, 4, 5, 6, 7, 3])
  assert.equal(music.paused, false)
})

test('Memory plays misses as they happen and one success sound per cleared level', async t => {
  const { audio, played } = setupAudio(t)
  await audio.updateRound(null)
  audio.start()

  await audio.updateRound(reportedRound('memory', 'failed', ['hit', 'miss', 'wrong']))
  assert.deepEqual(played, [4, 4])

  await audio.updateRound(reportedRound('memory', 'level-1', ['hit', 'hit', 'hit']))
  assert.deepEqual(played, [4, 4])
  await audio.updateRound(reportedRound('memory', 'level-1', ['hit', 'hit', 'hit']), true)
  await audio.updateRound(reportedRound('memory', 'level-1', ['hit', 'hit', 'hit']), true)
  assert.deepEqual(played, [4, 4, 1])

  await audio.updateRound(reportedRound('memory', 'level-2', ['hit', 'hit']))
  assert.deepEqual(played, [4, 4, 1])
  await audio.updateRound(reportedRound('memory', 'level-2', ['hit', 'hit', 'hit', 'hit']), true)
  assert.deepEqual(played, [4, 4, 1, 1])
})

test('music and sound effects have independent volume levels that persist across page loads', t => {
  const { audio, gains, music, toggle } = setupAudio(t)
  assert.equal(audio.getVolume('music'), 28)
  assert.equal(audio.getVolume('effects'), 100)
  audio.setVolume('music', 45)
  assert.equal(music.volume, 0.45)

  audio.start()
  assert.deepEqual(gains.map(gain => gain.value), [1, 0.45])
  assert.equal(music.volume, 1)
  audio.setVolume('effects', 0)
  assert.deepEqual(gains.map(gain => gain.value), [0, 0.45])
  audio.setVolume('music', 65)
  assert.deepEqual(gains.map(gain => gain.value), [0, 0.65])

  toggle.dispatchEvent(new Event('click'))
  toggle.dispatchEvent(new Event('click'))
  assert.deepEqual(gains.map(gain => gain.value), [0, 0.65])
  const reopened = createGameAudio({ ...music }, toggle)
  assert.equal(reopened.getVolume('music'), 65)
  assert.equal(reopened.getVolume('effects'), 0)
})
