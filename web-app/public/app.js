import { watchBoard } from './live.js'
import { createGameAudio } from './sound.js?v=4'

const $ = id => document.getElementById(id)
const gameNames = { precision: '6 Throws', chase: '60 Seconds', memory: 'Color Memory' }
const typeNames = { practice: 'Practice', competition: 'Competition' }
const outcomes = { hit: 'Hit', miss: 'Miss', wrong: 'Wrong hole' }
const storageKey = 'ishoot-versus'
function restoreMatch() {
  try {
    const saved = JSON.parse(sessionStorage.getItem(storageKey))
    if (['precision', 'chase'].includes(saved?.game) && saved.names?.length === 2 && saved.scores?.length === 2) return saved
  } catch {}
  return null
}
let match = restoreMatch()
const nameFields = [$('versus-name-1'), $('versus-name-2')]
const versusNames = () => nameFields.map(field => field.value.trim())
let player = null
let identityRevision = 0
let guestEntry
let guestStart = ''
let guestRoundSeen = ''
let view
let confirmedAt = 0
let pending
let sending = false
let loading = false
let refreshRequested = false
let polling
let stopWatching
let previousPhase = 'ready'
let joining = false
let loadError = ''
let choosing = false
let feedbackRound = ''
let feedbackThrough = 0
let feedbackCorrections = 0
let feedbackTimer
let readingLeaderboard = false
let leaderboardLoaded = false
let accountTrigger
const audio = createGameAudio($('background-music'), $('music-toggle'))
const volumeMenu = $('volume-menu')
for (const channel of ['music', 'effects']) {
  const slider = $(channel + '-volume')
  const valueId = slider.id + '-value'
  slider.value = audio.getVolume(channel)
  setText(valueId, slider.value + '%')
  slider.addEventListener('input', () => {
    setText(valueId, audio.setVolume(channel, slider.valueAsNumber) + '%')
  })
}

function rememberMatch() {
  if (match) sessionStorage.setItem(storageKey, JSON.stringify(match))
  else sessionStorage.removeItem(storageKey)
}

async function api(action, body) {
  const query = new URLSearchParams({ action })
  const response = await fetch(`/api/game?${query}`, {
    method: body === undefined ? 'GET' : 'POST',
    headers: body === undefined ? undefined : { 'content-type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
    cache: 'no-store',
    signal: AbortSignal.timeout(9000)
  })
  const data = await response.json()
  if (!response.ok) throw Object.assign(new Error(data.error || 'The game could not respond.'), { status: response.status })
  return data
}

function setText(id, text) {
  if ($(id).textContent !== text) $(id).textContent = text
}

function setStatus(kind, text) {
  $('board-status').dataset.status = kind
  setText('status-text', text)
}

const selected = name => document.querySelector(`input[name="${name}"]:checked`).value
const select = (name, value) => { document.querySelector(`input[name="${name}"][value="${value}"]`).checked = true }
const playerLabel = (index, names) => names[index] || 'Player ' + (index + 1)

if (match) {
  select('players', 'versus')
  select('game', match.game)
}

function updateProgress() {
  const round = view?.state
  if (round?.phase !== 'playing' || round.game !== 'chase') return
  const elapsed = round.elapsedMs + (view.online ? performance.now() - confirmedAt : 0)
  setText('progress', `${Math.max(0, Math.ceil((round.durationMs - elapsed) / 1000))}s`)
}

function renderHistory(history) {
  const signature = JSON.stringify(history)
  const list = $('recent-rounds')
  if (list.dataset.items === signature) return
  list.dataset.items = signature
  $('history-empty').hidden = history.length > 0
  list.replaceChildren(...history.map(round => {
    const li = document.createElement('li')
    const score = document.createElement('strong')
    score.textContent = `${round.score} pts`
    const level = round.game === 'memory' ? ` · Level ${round.level}` : ''
    li.append(`${gameNames[round.game]} · ${typeNames[round.type]}${level}`, score)
    return li
  }))
}

function renderThrows(byTarget, throws) {
  const list = $('recent-throws')
  const signature = JSON.stringify([byTarget, throws])
  if (list.dataset.items === signature) return
  list.dataset.items = signature
  const table = $('target-stats')
  table.replaceChildren(...byTarget.map(stat => {
    const row = document.createElement('tr')
    const rate = stat.attempts ? `${stat.hits}/${stat.attempts} (${Math.round(stat.hits / stat.attempts * 100)}%)` : 'No attempts'
    for (const value of [`${stat.target} points`, stat.landed, rate]) {
      const cell = document.createElement('td')
      cell.textContent = value
      row.append(cell)
    }
    return row
  }))
  const total = byTarget.reduce((sum, stat) => sum + stat.landed, 0)
  let summary = 'After a round, you can compare where your throws landed and how often you hit each target.'
  if (total) {
    const counts = byTarget.map(stat => stat.landed)
    const most = Math.max(...counts)
    const least = Math.min(...counts)
    const holes = count => byTarget.filter(stat => stat.landed === count).map(stat => stat.target).join(', ')
    summary = most === least ? 'You landed the same number of throws in each hole.'
      : `Most often landed in ${holes(most)} (${most}). Least often landed in ${holes(least)} (${least}).`
  }
  setText('throw-summary', summary)
  list.replaceChildren(...throws.map(attempt => {
    const row = document.createElement('li')
    const title = document.createElement('span')
    title.textContent = `Aim ${attempt.aimed} · ${attempt.landed ? 'Landed ' + attempt.landed : 'No hole'}`
    const result = document.createElement('strong')
    result.textContent = outcomes[attempt.outcome]
    result.dataset.outcome = attempt.outcome
    row.append(title, result)
    return row
  }))
}

function clearShot() {
  delete document.querySelector('.game-panel').dataset.feedback
  $('shot-feedback').hidden = true
  setText('shot-feedback', '')
}

function updateShot(round) {
  const shot = round?.lastThrow
  if (!shot || !round.roundId) {
    if (round?.roundId !== feedbackRound) {
      feedbackRound = round?.roundId ?? ''
      feedbackThrough = 0
      feedbackCorrections = round?.corrections ?? 0
    }
    clearShot()
    return
  }
  if (feedbackRound !== round.roundId) {
    feedbackRound = round.roundId
    feedbackThrough = shot.number - 1
    feedbackCorrections = round.corrections ?? 0
  }
  const corrected = (round.corrections ?? 0) > feedbackCorrections
  feedbackCorrections = round.corrections ?? 0
  if (shot.number <= feedbackThrough && !corrected) return
  const candidate = round.lastFault ?? shot
  const fault = corrected ? { outcome: 'miss' }
    : candidate.number > feedbackThrough && candidate.outcome !== 'hit' ? candidate : null
  feedbackThrough = shot.number
  if (!fault) return
  clearTimeout(feedbackTimer)
  clearShot()
  document.querySelector('.game-panel').dataset.feedback = fault.outcome
  $('shot-feedback').hidden = false
  setText('shot-feedback', outcomes[fault.outcome] + ' · 0 points')
  feedbackTimer = setTimeout(clearShot, 2200)
}

function updateMatch(data) {
  if (!match?.commandId) return
  const round = data.state
  if (round?.startId === match.commandId) {
    if (match.roundId !== round.roundId || (round.phase === 'results' && match.scores[match.slot] !== round.score)) {
      match.roundId = round.roundId
      if (round.phase === 'results') match.scores[match.slot] = round.score
      rememberMatch()
    }
  } else if (match.scores[match.slot] === null &&
      ((data.receipt?.id === match.commandId && data.receipt.result !== 'applied')
        || (match.roundId && round?.phase === 'ready') || Date.now() > Date.parse(match.expiresAt) + 1200)) {
    match.commandId = ''
    match.roundId = ''
    rememberMatch()
  }
}

function showMemoryWin(round) {
  setText('memory-cleared', round.nextMemoryLevel === 1
    ? 'You cleared all 10 levels!' : `You cleared Level ${round.level}!`)
  setText('memory-win-stats', `${round.count} correct throws · ${round.score} points`)

  const confetti = $('memory-confetti')
  if (confetti.dataset.round === round.roundId) return
  confetti.dataset.round = round.roundId
  const arcs = [[4, 91], [12, 133], [18, 104], [26, 152], [30, 120], [36, 95], [42, 142], [48, 106]]
  const particles = document.createDocumentFragment()
  for (const direction of [1, -1]) {
    for (const [index, [drift, height]] of arcs.entries()) {
      const particle = document.createElement('span')
      particle.style.setProperty('--launch', direction === 1 ? '12%' : '88%')
      particle.style.setProperty('--drift', `${direction * drift}cqw`)
      particle.style.setProperty('--peak', `-${height}px`)
      particle.style.setProperty('--spin', `${direction * (270 + index * 71)}deg`)
      particle.style.setProperty('--delay', `${(index % 4) * 35}ms`)
      particles.append(particle)
    }
  }
  confetti.replaceChildren(particles)
}

function render() {
  const round = view?.state
  const phase = round?.phase ?? 'ready'
  const signedIn = player?.kind === 'account'
  const guest = player?.kind === 'guest'
  const versus = selected('players') === 'versus'
  const versusRound = versus && Boolean(match)
  const matchComplete = versusRound && match.scores[1] !== null
  const playerTwo = versusRound && match.scores[0] !== null
  const ownRound = versusRound && view?.versusOwned && round?.startId === match.commandId
  const playing = phase === 'playing' && (round.versus ? ownRound : !versusRound)
  const freshGuestResult = !round?.guestRound
    || (guestRoundSeen && round.roundId === guestRoundSeen) || (guestStart && round.startId === guestStart)
  const soloResults = phase === 'results' && !round?.versus && !versusRound && !choosing && freshGuestResult
  const memoryResult = soloResults && round.game === 'memory'
  const memoryContinuation = memoryResult && view.memoryCanContinue && round.nextMemoryLevel
  const memoryCleared = memoryContinuation && round.memoryCleared
  const inRound = versusRound || playing || soloResults
  const previewing = playing && Boolean(round.previewing)
  const memory = selected('game') === 'memory'
  const busy = sending || pending || (view?.pending && Date.parse(view.pending.expiresAt) > Date.now())
  const canControl = view?.online && !busy
  const startable = canControl && phase !== 'playing'
  const lastHit = round?.lastHit
  const correction = $('correction')
  const correctionRound = round?.roundId ?? ''
  if (correction.dataset.round !== correctionRound) {
    correction.dataset.round = correctionRound
    setText('correction-message', '')
  }
  correction.hidden = joining || !view?.correctionReady || phase === 'ready' || Boolean(round?.previewing)
  $('correct').disabled = !canControl || !lastHit
  setText('correct', lastHit ? `Mark ${lastHit.points}-point hit as miss` : 'Mark last hit as miss')
  $('join').hidden = !joining
  $('setup').hidden = joining || inRound
  $('round').hidden = joining || !inRound
  $('game-intro').hidden = inRound
  $('login-open').hidden = !player
  $('signup-open').hidden = !guest
  $('guest-play').hidden = !guest || inRound
  if (!player) closeAccount()
  setText('login-open', signedIn ? player.name : 'Log in')
  $('level-card').hidden = versus || !signedIn
  $('coach-card').hidden = versus || !signedIn
  $('history').hidden = versus || !signedIn
  $('leaderboard').hidden = versusRound
  $('player-form').hidden = signedIn
  $('signed-player').hidden = !signedIn
  $('solo-style').hidden = versus || !signedIn
  $('guest-settings').hidden = versus || !guest
  $('versus-setup').hidden = !versus
  $('memory-option').hidden = versus
  $('memory-settings').hidden = !memory
  setText('memory-help', view?.online && !view.memoryReady
    ? 'Upload the updated board firmware to unlock Color Memory.'
    : 'Watch the lights, then throw in the same order. Clear the pattern to move on to longer sequences with faster flashes.')
  const names = versus ? versusNames() : []
  const missingNames = versus && names.some(name => !name)
  $('start').disabled = !startable || missingNames || (memory && !view?.memoryReady) || (versus && !view?.versusReady)
  $('reset').disabled = !canControl
  $('reset').hidden = !playing
  $('miss').disabled = !canControl || previewing
  $('miss').hidden = !playing || previewing
  $('replay').hidden = !soloResults && !(versusRound && !playing)
  $('replay').disabled = !startable || (versusRound && !view?.versusReady) || (memoryResult && !view?.memoryReady)
  $('change-game').hidden = !soloResults && !(versusRound && !playing && !busy)
  setText('change-game', versusRound ? 'Back to dashboard' : 'Change game')
  $('demo-controls').hidden = !view?.demo || !playing || previewing
  $('memory-win').hidden = !memoryCleared
  $('round-score').hidden = memoryCleared || (!playing && !soloResults)
  $('target').hidden = memoryCleared || (!playing && !soloResults)
  $('targets').hidden = !playing || (round.game === 'memory' && !(view?.demo && previewing))
  $('versus-scoreboard').hidden = !versusRound
  $('versus-status').hidden = !versusRound
  for (const button of document.querySelectorAll('[data-landed]')) button.disabled = !canControl
  setText('start-label', busy ? 'Waiting for board…' : missingNames ? 'Enter both names to start'
    : versus ? 'Start ' + names[0] + '’s turn' : memory ? 'Start Level 1' : 'Start round')
  let title = previewing ? 'Watch the lights' : playing ? 'Round in play' : soloResults ? 'Round complete'
    : phase === 'playing' ? 'Cabinet in use' : 'Ready to toss?'
  let replayLabel = 'Play again'
  if (memoryCleared) {
    title = 'Congratulations!'
    replayLabel = round.nextMemoryLevel === 1 ? 'Play again from Level 1' : 'Continue to Level ' + round.nextMemoryLevel
    showMemoryWin(round)
  } else if (memoryContinuation) {
    title = 'Try Level ' + round.level + ' again'
    replayLabel = 'Retry Level ' + round.nextMemoryLevel
  } else if (memoryResult) {
    replayLabel = 'Start Level 1'
  }
  if (versusRound) {
    const turn = playing ? match.slot : playerTwo ? 1 : 0
    for (const slot of [0, 1]) {
      setText('match-name-' + (slot + 1), playerLabel(slot, match.names))
      const score = match.scores[slot] ?? (playing && match.slot === slot ? round.score : '—')
      setText('match-score-' + (slot + 1), String(score))
      $('match-card-' + (slot + 1)).setAttribute('aria-current', !matchComplete && turn === slot ? 'step' : 'false')
    }
    if (matchComplete) {
      const [first, second] = match.scores
      title = first === second ? 'It’s a tie!' : playerLabel(first > second ? 0 : 1, match.names) + ' wins!'
      delete $('versus-status').dataset.player
      setText('versus-status', 'Match complete')
    } else {
      const nextName = playerLabel(turn, match.names)
      title = nextName + '’s turn'
      $('versus-status').dataset.player = String(turn + 1)
      const instruction = playing ? 'Throw now' : 'Up next'
      const target = turn === 1 ? ` · ${match.scores[0]} points to beat` : ''
      setText('versus-status', `Player ${turn + 1} · ${instruction}${target}`)
      replayLabel = 'Start ' + nextName + '’s turn'
    }
  }
  setText('replay', busy ? 'Waiting for board…' : replayLabel)
  setText('game-title', title)
  setText('game-intro', joining ? 'Enter the code from the game organizer.' : 'Choose your game and start a round.')
  if (previousPhase !== phase) {
    if (document.activeElement === document.body || document.activeElement.closest('.game-panel')) {
      $('game-title').focus({ preventScroll: true })
    }
    previousPhase = phase
  }

  if (view) {
    setStatus(view.demo ? 'demo' : view.online ? 'online' : 'offline',
      loadError ? 'Connection unavailable' : view.demo ? 'Demo board' : view.online ? 'Board connected' : 'Board offline')
    const { level, levelDetail, coach, byTarget, evidence } = view.insights
    setText('level', `Level ${level}`)
    setText('level-detail', levelDetail)
    setText('level-evidence', evidence.throws < 6
      ? `You’re starting at Level 2. After 6 Practice throws, your hit rate sets the next level. You have ${evidence.throws}/6 so far.`
      : `Your next Practice round uses Level ${level}. That comes from ${evidence.hits} hits out of ${evidence.throws} throws (${Math.round(evidence.hits / evidence.throws * 100)}%) in up to 6 recent Practice rounds.`)
    setText('coach', coach.title)
    setText('coach-detail', coach.detail)
    setText('health', view.health.title)
    setText('health-detail', view.health.detail)
    $('health-card').classList.toggle('warning', Boolean(view.health.warning))
    renderHistory(view.history)
    renderThrows(byTarget, view.throws)
  }

  const chaseSeconds = !versus && signedIn && selected('type') === 'practice' ? [75, 60, 45][(view?.insights.level ?? 2) - 1] : 60
  setText('chase-length', chaseSeconds === 60 ? 'Unlimited throws' : `${chaseSeconds}s in Practice`)
  if (versusRound) {
    setText('round-mode', 'VS · ' + gameNames[match.game])
    setText('round-player', '')
  } else if (inRound) {
    setText('round-mode', `${gameNames[round.game]} · ${typeNames[round.type]}${round.game === 'memory' ? ' · Level ' + round.level : ''}`)
    setText('round-player', round.player)
  }
  if (playing || soloResults) {
    setText('score', String(round.score))
    setText('progress-label', soloResults ? 'Throws' : round.game === 'chase' ? 'Time left' : 'Throws left')
    if (soloResults) setText('progress', String(round.count))
    else if (round.game === 'precision') setText('progress', String(Math.max(0, 6 - round.count)))
    else if (round.game === 'memory') setText('progress', String(Math.max(0, round.level + 2 - round.count)))
    else updateProgress()
    const resultMessage = memoryContinuation ? 'Match every light in order to advance.'
      : round.guestRound && round.game === 'chase' ? 'Score entered on the leaderboard' : 'Round complete'
    setText('target', soloResults ? resultMessage : round.game === 'memory'
      ? previewing ? view.demo ? 'Memorize the colors below.' : 'Memorize the colors as they light up on the cabinet.' : 'Throw into the holes in the order you saw.'
      : `Aim for the ${round.target}-point hole`)
    for (const target of document.querySelectorAll('[data-target]')) {
      target.classList.toggle('active', Number(target.dataset.target) === round.target)
      target.classList.toggle('memory-lit', Boolean(view.demo && previewing && Number(target.dataset.target) === round.previewTarget))
    }
  }
  const shotRound = playing || soloResults || ownRound ? round : null
  updateShot(shotRound)
  if (view?.online && view.state) audio.updateRound(shotRound, memoryCleared)
}

function checkReceipt() {
  if (!pending) return
  if (view?.receipt?.id === pending.id) {
    const messages = {
      applied: '',
      wait: 'That throw was too soon after the previous one. Try again.',
      stale: 'The board moved on before that action. Check the round above.',
      expired: 'The board didn’t answer in time. Check its connection.'
    }
    if (pending.kind === 'correct') {
      setText('correction-message', view.receipt.result === 'applied'
        ? `${pending.points} points removed. The throw is recorded as a miss.`
        : messages[view.receipt.result] ?? '')
    } else {
      setText('message', messages[view.receipt.result] ?? '')
    }
    pending = null
  } else if (Date.now() > Date.parse(pending.expiresAt) + 1200) {
    setText(pending.kind === 'correct' ? 'correction-message' : 'message', 'The board didn’t answer in time. Check the round before trying again.')
    pending = null
  }
}

async function load() {
  if (loading) {
    refreshRequested = true
    return
  }
  clearTimeout(polling)
  loading = true
  const revision = identityRevision
  try {
    const data = await api('state')
    if (revision !== identityRevision) return
    if (player && player.id !== data.player.id) closePrivatePanels()
    player = data.player
    view = data
    if (!data.demo && !stopWatching) {
      stopWatching = watchBoard(async () => (await api('player-live')).live, () => {
        if (!document.hidden) load()
      })
    }
    if (data.state?.phase === 'playing' && !data.state.versus) {
      choosing = false
      if (data.state.guestRound) guestRoundSeen = data.state.roundId
    }
    confirmedAt = performance.now()
    joining = false
    if (loadError && $('message').textContent === loadError) setText('message', '')
    loadError = ''
    checkReceipt()
    updateMatch(data)
    const leaderboardRound = data.state?.roundId + ':' + (data.state?.corrections ?? 0)
    if ($('leaderboard').open && (!leaderboardLoaded || data.state?.phase === 'results' && leaderboardRound !== $('leaderboard').dataset.round)) {
      $('leaderboard').dataset.round = leaderboardRound
      loadLeaderboard()
    }
  } catch (error) {
    if (revision !== identityRevision) return
    if (error.status === 401) {
      joining = true
      stopWatching?.()
      stopWatching = undefined
      view = null
      player = null
      closePrivatePanels()
      setStatus('loading', 'Ready to join')
    } else {
      if (view) view.online = false
      setStatus('offline', 'Connection unavailable')
      loadError = error.message || 'Could not reach the game.'
      if (!sending && !pending) setText('message', loadError)
      checkReceipt()
    }
  } finally {
    loading = false
    render()
    const refreshNow = refreshRequested || revision !== identityRevision
    refreshRequested = false
    const delay = refreshNow ? 0 : document.hidden ? 8000
      : joining ? 12000 : view?.demo && view.state?.previewing ? 125
        : pending || view?.state?.phase === 'playing' ? 950 : 3800
    polling = setTimeout(load, delay)
  }
}

async function control(kind, replay = false) {
  if (sending || pending || !view?.online || view.pending) return
  audio.start()
  const message = kind === 'correct' ? 'correction-message' : 'message'
  sending = true
  setText(message, '')
  render()
  try {
    let nextMatch
    let body
    if (kind !== 'start') {
      body = { kind, roundId: view.state.roundId, count: view.state.count }
      if (kind === 'correct') body.attempt = view.state.lastHit.number
    } else if (selected('players') === 'versus') {
      const names = match?.names ?? versusNames()
      if (names.some(name => !name)) throw new Error('Enter a name for both players.')
      const previous = match ?? {
        game: selected('game'), names,
        scores: [null, null]
      }
      const scores = previous.scores[1] === null ? previous.scores : [null, null]
      const slot = scores[0] === null ? 0 : 1
      nextMatch = { ...previous, scores, slot, roundId: '' }
      body = { kind, game: previous.game, type: 'competition', versus: true, slot: slot + 1, names }
    } else {
      const guest = player?.kind === 'guest'
      body = {
        kind, game: replay ? view.state.game : selected('game'),
        type: guest ? 'competition' : replay ? view.state.type : selected('type')
      }
      if (replay && body.game === 'memory' && view.memoryCanContinue && view.state.nextMemoryLevel) {
        body.continueRoundId = view.state.roundId
      }
      if (guest) {
        guestEntry ??= crypto.randomUUID()
        body.entry = guestEntry
        body.nickname = $('guest-nickname').value.trim()
      }
    }
    const points = kind === 'correct' ? view.state.lastHit.points : undefined
    pending = { ...await api('control', body), kind, points }
    if (kind === 'start') {
      choosing = false
      if (nextMatch) {
        match = { ...nextMatch, commandId: pending.id, expiresAt: pending.expiresAt }
        rememberMatch()
      } else if (player?.kind === 'guest') {
        guestStart = pending.id
      }
    }
    setText(message, 'Waiting for the board…')
  } catch (error) {
    setText(message, error.message)
  } finally {
    sending = false
    render()
    load()
  }
}

function closePrivatePanels() {
  for (const card of document.querySelectorAll('.features details, #history')) card.open = false
  renderHistory([])
  renderThrows([], [])
  setText('level', 'Level 2')
  setText('level-detail', 'Starting level for Practice.')
  setText('level-evidence', 'Your next level is based on your recent Practice throws.')
  setText('coach', 'Play a few rounds')
  setText('coach-detail', '')
  leaderboardLoaded = false
  $('leaderboard-list').replaceChildren()
  setText('leaderboard-status', '')
}

function closeAccount(focus = false) {
  $('player-menu').hidden = true
  if (accountTrigger) {
    accountTrigger.setAttribute('aria-expanded', 'false')
    if (focus) accountTrigger.focus({ preventScroll: true })
    accountTrigger = undefined
  }
}

function openAccount(trigger, action) {
  audio.start()
  const showing = accountTrigger === trigger
  closeAccount()
  if (showing) return
  accountTrigger = trigger
  trigger.setAttribute('aria-expanded', 'true')
  $('player-menu').hidden = false
  setText('account-message', '')
  if (player?.kind === 'account') {
    $('signout').focus()
    return
  }
  $('account-submit').value = action
  setText('account-submit', action === 'signup' ? 'Sign up' : 'Log in')
  $('player-password').autocomplete = action === 'signup' ? 'new-password' : 'current-password'
  $('player-input').focus()
}

function switchAccount(account) {
  identityRevision++
  player = account
  view = null
  closePrivatePanels()
  guestEntry = undefined
  guestStart = ''
  guestRoundSeen = ''
  $('guest-nickname').value = ''
  $('player-password').value = ''
  $('player-input').value = ''
  setText('account-message', '')
  closeAccount()
  render()
  load()
}

$('login-open').addEventListener('click', event => openAccount(event.currentTarget, 'signin'))
$('signup-open').addEventListener('click', event => openAccount(event.currentTarget, 'signup'))
$('guest-play').addEventListener('click', () => {
  audio.start()
  closeAccount()
  if (selected('players') === 'versus') nameFields.find(field => !field.value.trim())?.focus()
  else $('guest-nickname').focus()
})
document.addEventListener('click', event => {
  if (accountTrigger && !event.target.closest('.top-actions')) closeAccount()
  if (volumeMenu.open && !volumeMenu.contains(event.target)) volumeMenu.open = false
})
document.addEventListener('keydown', event => {
  if (event.key !== 'Escape') return
  if (accountTrigger) closeAccount(true)
  if (volumeMenu.open) {
    volumeMenu.open = false
    volumeMenu.querySelector('summary').focus({ preventScroll: true })
  }
})

$('player-form').addEventListener('submit', async event => {
  event.preventDefault()
  const button = $('account-submit')
  if (button.disabled) return
  button.disabled = true
  audio.start()
  setText('account-message', '')
  try {
    const data = await api(button.value, {
      username: $('player-input').value.trim(), password: $('player-password').value
    })
    switchAccount(data.player)
  } catch (error) {
    setText('account-message', error.message)
  } finally {
    button.disabled = false
  }
})
$('signout').addEventListener('click', async () => {
  $('signout').disabled = true
  try {
    await api('signout', {})
    switchAccount(null)
  } catch (error) {
    setText('account-message', error.message)
  } finally {
    $('signout').disabled = false
  }
})
for (const field of nameFields) field.addEventListener('input', render)
$('guest-nickname').addEventListener('change', () => { guestEntry = undefined })
$('join').addEventListener('submit', async event => {
  event.preventDefault()
  audio.start()
  const button = event.submitter
  button.disabled = true
  try {
    await api('join', { code: $('game-code').value })
    $('game-code').value = ''
    setText('message', '')
    load()
  } catch (error) {
    setText('message', error.message)
  } finally {
    button.disabled = false
  }
})
$('correct').addEventListener('click', () => control('correct'))
for (const input of document.querySelectorAll('input[type="radio"]')) input.addEventListener('change', event => {
  if (event.target.name === 'players' && event.target.value === 'versus') {
    closeAccount()
    closePrivatePanels()
    choosing = true
    if (selected('game') === 'memory') select('game', 'precision')
  }
  render()
})
for (const kind of ['start', 'reset', 'miss']) $(kind).addEventListener('click', () => control(kind))
$('replay').addEventListener('click', () => control('start', true))
$('change-game').addEventListener('click', () => {
  const clearCabinet = match && view?.versusOwned && view.state?.startId === match.commandId && view.state.phase === 'results'
  if (match) {
    match = null
    rememberMatch()
    for (const field of nameFields) field.value = ''
    select('players', 'solo')
  }
  choosing = true
  render()
  $('game-title').focus({ preventScroll: true })
  if (clearCabinet) control('reset')
})
for (const button of document.querySelectorAll('[data-landed]')) button.addEventListener('click', async () => {
  audio.start()
  try {
    await api('toss', { landed: Number(button.dataset.landed) })
    load()
  } catch (error) {
    setText('message', error.message)
  }
})
document.addEventListener('visibilitychange', () => { if (!document.hidden) load() })

const statsTabs = ['rounds', 'throws']
function showStats(name, focus = false) {
  for (const tab of statsTabs) {
    const active = tab === name
    $(tab + '-tab').setAttribute('aria-selected', String(active))
    $(tab + '-tab').tabIndex = active ? 0 : -1
    $(tab + '-panel').hidden = !active
    if (active && focus) $(tab + '-tab').focus()
  }
}
for (const tab of statsTabs) {
  const button = $(tab + '-tab')
  button.addEventListener('click', () => showStats(tab))
  button.addEventListener('keydown', event => {
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return
    event.preventDefault()
    const next = event.key === 'Home' ? 'rounds' : event.key === 'End' ? 'throws' : statsTabs.find(candidate => candidate !== tab)
    showStats(next, true)
  })
}
const scroll = element => element.scrollIntoView({
  block: 'nearest', behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth'
})
for (const card of document.querySelectorAll('.features details')) {
  card.addEventListener('toggle', () => { if (card.open) scroll(card) })
}
$('coach-throws').addEventListener('click', () => {
  $('history').open = true
  showStats('throws', true)
  scroll($('history'))
})

async function loadLeaderboard() {
  if (player?.kind !== 'account') {
    leaderboardLoaded = true
    $('leaderboard-list').replaceChildren()
    setText('leaderboard-status', 'Sign in to see the leaderboard.')
    return
  }
  if (readingLeaderboard) return
  const accountId = player.id
  const revision = identityRevision
  readingLeaderboard = true
  setText('leaderboard-status', 'Loading scores…')
  try {
    const { entries } = await api('leaderboard')
    if (revision !== identityRevision || player?.id !== accountId) return
    leaderboardLoaded = true
    setText('leaderboard-status', entries.length ? '' : 'The first completed single-player 60 Seconds Competition round will appear here.')
    $('leaderboard-list').replaceChildren(...entries.map(entry => {
      const li = document.createElement('li')
      const name = document.createElement('span')
      name.textContent = '#' + entry.rank + ' · ' + entry.player
      const score = document.createElement('strong')
      score.textContent = entry.score + ' pts'
      li.append(name, score)
      return li
    }))
  } catch (error) {
    if (revision === identityRevision && player?.id === accountId) setText('leaderboard-status', error.message)
  } finally {
    readingLeaderboard = false
  }
}
$('leaderboard').addEventListener('toggle', () => { if ($('leaderboard').open) loadLeaderboard() })
setInterval(updateProgress, 250)
load()
