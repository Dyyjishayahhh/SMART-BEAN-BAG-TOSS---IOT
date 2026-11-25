const hitSounds = ['/sounds/hit-1.mp3', '/sounds/hit-2.mp3', '/sounds/hit-3.mp3']
const missSounds = ['/sounds/miss.mp3', '/sounds/miss-2.mp3', '/sounds/miss-3.mp3', '/sounds/miss-4.mp3']
const volumeKey = 'ishoot-volume'
const percent = (value, fallback) => Number.isFinite(value) ? Math.max(0, Math.min(100, Math.round(value))) : fallback

function savedVolumes() {
  let saved
  try { saved = JSON.parse(globalThis.localStorage?.getItem(volumeKey) ?? 'null') } catch {}
  return { music: percent(saved?.music, 28), effects: percent(saved?.effects, 100) }
}

export function createGameAudio(music, toggle) {
  let enabled = false
  let mutedByPlayer = false
  let context
  let effects
  let clips
  let revision = 0
  let heardRound = ''
  let heardThrough = 0
  let heardCorrections = 0
  let hasSeenBoard = false
  let celebratedRound = ''
  const volumes = savedVolumes()
  const gains = {}
  const voices = new Set()
  music.volume = volumes.music / 100

  function connectVolume(channel, source) {
    const node = context.createGain()
    node.gain.value = volumes[channel] / 100
    node.connect(context.destination)
    source.connect(node)
    gains[channel] = node
  }

  function setVolume(channel, value) {
    const level = percent(value, volumes[channel])
    volumes[channel] = level
    if (gains[channel]) gains[channel].gain.setTargetAtTime(level / 100, context.currentTime, 0.02)
    else if (channel === 'music') music.volume = level / 100
    try { globalThis.localStorage?.setItem(volumeKey, JSON.stringify(volumes)) } catch {}
    return level
  }

  function reflect() {
    toggle.setAttribute('aria-pressed', String(enabled))
    toggle.title = enabled ? 'Mute game audio' : 'Turn on game audio'
  }

  function startMixer() {
    const AudioContext = globalThis.AudioContext ?? globalThis.webkitAudioContext
    if (!AudioContext) return
    try {
      if (!context) {
        context = new AudioContext()
        effects = context.createDynamicsCompressor()
        connectVolume('effects', effects)
        try {
          connectVolume('music', context.createMediaElementSource(music))
          music.volume = 1
        } catch {}
        clips = Promise.all([...hitSounds, ...missSounds].map(async file => {
          try {
            const response = await fetch(file + '?v=2')
            if (response.ok) return [file, await context.decodeAudioData(await response.arrayBuffer())]
          } catch {}
          return [file, null]
        })).then(entries => new Map(entries))
      }
      if (context.state !== 'running') context.resume().catch(() => {})
    } catch {}
  }

  function start() {
    if (mutedByPlayer) return
    enabled = true
    reflect()
    startMixer()
    if (music.paused) music.play().catch(() => {})
  }

  function stopShots() {
    revision++
    for (const voice of voices) voice.stop()
    voices.clear()
  }

  async function playShots(outcomes) {
    if (!enabled || !clips || !outcomes.length) return
    const version = revision
    const requestedAt = performance.now()
    const bank = await clips
    if (!enabled || revision !== version || context.state !== 'running' || performance.now() - requestedAt > 1800) return

    const hits = hitSounds.map(file => bank.get(file)).filter(Boolean)
    const misses = missSounds.map(file => bank.get(file)).filter(Boolean)
    for (const [index, outcome] of outcomes.entries()) {
      const group = outcome === 'hit' ? hits : misses
      const clip = group[Math.floor(Math.random() * group.length)]
      if (!clip) continue

      const voice = context.createBufferSource()
      voice.buffer = clip
      voice.connect(effects)
      voice.onended = () => {
        voices.delete(voice)
        voice.disconnect()
      }
      voices.add(voice)
      voice.start(context.currentTime + index * 0.35)
    }
  }

  function updateRound(round, memoryCleared = false) {
    const wasWatching = hasSeenBoard
    hasSeenBoard = true
    const roundId = round?.roundId ?? ''
    if (roundId !== heardRound) {
      stopShots()
      heardRound = roundId
      heardThrough = wasWatching ? 0 : round?.count ?? 0
      heardCorrections = round?.corrections ?? 0
    }

    const latest = round?.lastThrow
    const cues = []
    if (latest && latest.number > heardThrough) {
      const recent = round.shotOutcomes?.length ? round.shotOutcomes : [latest.outcome]
      const fresh = recent.slice(Math.max(0, heardThrough - latest.number + recent.length))
      heardThrough = latest.number
      cues.push(...(round.game === 'memory' ? fresh.filter(outcome => outcome !== 'hit') : fresh))
    }
    const corrections = round?.corrections ?? 0
    if (corrections > heardCorrections && !cues.includes('miss')) cues.push('miss')
    heardCorrections = corrections
    if (memoryCleared && roundId && roundId !== celebratedRound) {
      celebratedRound = roundId
      if (wasWatching) cues.push('hit')
    }
    return playShots(cues)
  }

  toggle.addEventListener('click', () => {
    mutedByPlayer = enabled
    if (!mutedByPlayer) {
      start()
      return
    }
    enabled = false
    music.pause()
    stopShots()
    reflect()
  })

  return { start, updateRound, getVolume: channel => volumes[channel], setVolume }
}
