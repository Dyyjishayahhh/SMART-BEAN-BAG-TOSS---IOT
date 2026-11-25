import { createServer } from 'node:http'
import { readFile } from 'node:fs/promises'
import { Readable } from 'node:stream'
import { createMemoryStore } from './store.js'
import { createGateway, handleGame } from './game.js'
import { createDemoBoard } from './demo.js'

const demo = process.argv.includes('--demo') || (!process.env.SUPABASE_URL && !process.env.SUPABASE_SECRET_KEY)
const port = Number(process.env.PORT || 4173)
const address = `http://localhost:${port}`
const store = demo ? createMemoryStore() : null
const board = demo ? createDemoBoard(store) : null
const gateway = demo ? createGateway({
  store, demoToss: board.toss, accessCode: 'local-demo-only', deviceToken: 'local-demo-only', secure: false
}) : handleGame
const types = {
  '/': ['index.html', 'text/html; charset=utf-8'],
  '/app.js': ['app.js', 'text/javascript; charset=utf-8'],
  '/live.js': ['live.js', 'text/javascript; charset=utf-8'],
  '/sound.js': ['sound.js', 'text/javascript; charset=utf-8'],
  '/styles.css': ['styles.css', 'text/css; charset=utf-8'],
  '/background.mp3': ['background.mp3', 'audio/mpeg'],
  '/sounds/hit-1.mp3': ['sounds/hit-1.mp3', 'audio/mpeg'],
  '/sounds/hit-2.mp3': ['sounds/hit-2.mp3', 'audio/mpeg'],
  '/sounds/hit-3.mp3': ['sounds/hit-3.mp3', 'audio/mpeg'],
  '/sounds/miss.mp3': ['sounds/miss.mp3', 'audio/mpeg'],
  '/sounds/miss-2.mp3': ['sounds/miss-2.mp3', 'audio/mpeg'],
  '/sounds/miss-3.mp3': ['sounds/miss-3.mp3', 'audio/mpeg'],
  '/sounds/miss-4.mp3': ['sounds/miss-4.mp3', 'audio/mpeg'],
  '/cabinet.png': ['cabinet.png', 'image/png']
}
const server = createServer(async (req, res) => {
  try {
    const host = req.headers.host
    if (host !== `localhost:${port}` && host !== `127.0.0.1:${port}`) {
      res.writeHead(403).end()
      return
    }
    const url = new URL(req.url, `http://${host}`)
    if (url.pathname === '/api/game') {
      const hasBody = req.method !== 'GET' && req.method !== 'HEAD'
      const request = new Request(url, { method: req.method, headers: req.headers,
        ...(hasBody ? { body: Readable.toWeb(req), duplex: 'half' } : {}) })
      const response = await gateway(request)
      const headers = Object.fromEntries(response.headers)
      if (response.headers.has('set-cookie')) headers['set-cookie'] = response.headers.getSetCookie()
      res.writeHead(response.status, headers)
      res.end(Buffer.from(await response.arrayBuffer()))
      return
    }
    const file = Object.hasOwn(types, url.pathname) && types[url.pathname]
    if (!file || (req.method !== 'GET' && req.method !== 'HEAD')) {
      res.writeHead(404).end()
      return
    }
    const bytes = await readFile(new URL(`../public/${file[0]}`, import.meta.url))
    res.writeHead(200, { 'content-type': file[1], 'x-content-type-options': 'nosniff', 'cache-control': 'no-store' })
    res.end(req.method === 'HEAD' ? undefined : bytes)
  } catch (error) {
    console.error(error.name)
    res.writeHead(500).end('Unable to load the game.')
  }
})
server.listen(port, '127.0.0.1', () => console.log(`${demo ? 'I-Shoot Mo demo' : 'I-Shoot Mo'} at ${address}`))
