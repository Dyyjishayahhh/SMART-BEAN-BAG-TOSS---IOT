export function watchBoard(settings, onChange) {
  let closed = false
  let socket
  let timer
  let retryDelay = 1000

  async function connect() {
    try {
      const config = await settings()
      if (closed || !config) return
      const endpoint = new URL(`wss://${config.host}/realtime/v1/websocket`)
      endpoint.searchParams.set('apikey', config.key)
      endpoint.searchParams.set('vsn', '1.0.0')
      const channel = 'realtime:' + config.topic
      const connection = new WebSocket(endpoint)
      socket = connection

      let ready = false
      let heartbeat = 0
      let waitingFor = ''
      const send = (topic, event, payload, ref) => connection.send(JSON.stringify({ topic, event, payload, ref }))

      timer = setTimeout(() => connection.close(), 7500)
      connection.onopen = () => {
        send(channel, 'phx_join', { config: { private: true, broadcast: { ack: false, self: false } } }, 'join')
      }
      connection.onmessage = event => {
        let message
        try { message = JSON.parse(event.data) } catch { return }
        if (message.event === 'phx_reply' && message.ref === waitingFor && waitingFor) waitingFor = ''
        if (message.topic !== channel) return
        if (message.event === 'phx_error' || message.event === 'phx_close') connection.close()
        if (!ready && message.event === 'phx_reply' && message.ref === 'join') {
          if (message.payload?.status !== 'ok') {
            connection.close()
            return
          }
          ready = true
          retryDelay = 1000
          clearTimeout(timer)
          timer = setInterval(() => {
            if (waitingFor || connection.readyState !== WebSocket.OPEN) {
              connection.close()
              return
            }
            waitingFor = String(++heartbeat)
            send('phoenix', 'heartbeat', {}, waitingFor)
          }, 20000)
          onChange()
        } else if (ready && message.event === 'broadcast' && message.payload?.event === 'changed') {
          onChange()
        }
      }
      connection.onclose = retry
    } catch (error) {
      if (error.status !== 401) retry()
    }
  }

  function retry() {
    clearTimeout(timer)
    if (closed) return
    timer = setTimeout(connect, retryDelay)
    retryDelay = Math.min(retryDelay * 2, 30000)
  }

  connect()
  return () => {
    closed = true
    clearTimeout(timer)
    socket?.close()
  }
}
