import { createHmac, randomBytes, randomUUID, scrypt, timingSafeEqual } from 'node:crypto'
import { promisify } from 'node:util'

const derive = promisify(scrypt)
const scryptOptions = { N: 32768, r: 8, p: 3, maxmem: 64 * 1024 * 1024 }
const guestCookie = 'ishoot_guest'
const accountCookie = 'ishoot_account'
const guestSeconds = 7 * 86400
const accountSeconds = 12 * 3600
const now = () => Math.floor(Date.now() / 1000)
const fakeSalt = Buffer.alloc(16)
const usernamePattern = /^[A-Za-z0-9][A-Za-z0-9_-]{2,23}$/

export function validCredentials(username, password) {
  return typeof username === 'string' && usernamePattern.test(username) && username.toLowerCase() !== 'guest'
    && typeof password === 'string' && password.length >= 8 && Buffer.byteLength(password) <= 128
}

export async function hashPassword(password) {
  const salt = randomBytes(16)
  const hash = await derive(password, salt, 32, scryptOptions)
  return salt.toString('hex') + ':' + hash.toString('hex')
}

export async function verifyPassword(password, stored) {
  const [salt, hash] = typeof stored === 'string' && /^[a-f0-9]{32}:[a-f0-9]{64}$/.test(stored)
    ? stored.split(':').map(part => Buffer.from(part, 'hex')) : [fakeSalt, null]
  const candidate = await derive(password, salt, 32, scryptOptions)
  return Boolean(hash && timingSafeEqual(candidate, hash))
}

export function identityService(secret, secure) {
  const mac = (purpose, value) => createHmac('sha256', secret).update(purpose + '.' + value).digest('base64url')
  function issue(purpose, data) {
    const value = Buffer.from(JSON.stringify(data)).toString('base64url')
    return value + '.' + mac(purpose, value)
  }
  function decode(purpose, token) {
    if (!token || token.length > 800) return null
    const [value, signature, extra] = token.split('.')
    if (!value || !signature || extra !== undefined) return null
    const expected = Buffer.from(mac(purpose, value))
    const provided = Buffer.from(signature)
    if (expected.length !== provided.length || !timingSafeEqual(expected, provided)) return null
    try {
      const data = JSON.parse(Buffer.from(value, 'base64url'))
      return data?.expires > now() ? data : null
    } catch {
      return null
    }
  }
  const header = (name, value, seconds) => name + '=' + value + '; Path=/; HttpOnly; SameSite=Strict; Max-Age='
    + seconds + (secure ? '; Secure' : '')

  function read(request) {
    const cookies = new Map((request.headers.get('cookie') ?? '').split(';').map(pair => {
      const [name, ...value] = pair.trim().split('=')
      return [name, value.join('=')]
    }))
    const existing = decode('guest', cookies.get(guestCookie))
    const guest = existing ?? { id: randomUUID(), expires: now() + guestSeconds }
    const account = decode('account', cookies.get(accountCookie))
    return {
      guest,
      player: account?.browser === guest.id
        ? { id: 'a:' + account.id, name: account.name, kind: 'account' }
        : { id: 'g:' + guest.id, name: 'Guest', kind: 'guest' },
      headers: existing ? {} : { 'set-cookie': header(guestCookie, issue('guest', guest), guestSeconds) }
    }
  }

  return {
    read,
    signIn(visitor, account) {
      const player = { id: 'a:' + account.id, name: account.username, kind: 'account' }
      const value = issue('account', { id: account.id, name: account.username,
        browser: visitor.guest.id, expires: now() + accountSeconds })
      return { player, cookie: header(accountCookie, value, accountSeconds) }
    },
    signOut() {
      return header(accountCookie, '', 0)
    }
  }
}
