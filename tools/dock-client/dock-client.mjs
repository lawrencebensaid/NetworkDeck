// Known-good host for testing Deck32: the same TCP client Bitfocus Companion uses.
//
//   node dock-client.mjs --discover        list docks announced on mDNS
//   node dock-client.mjs <ip>              read dock info, draw keys, log presses
//   node dock-client.mjs <ip> --bench      also time full-deck redraws (--frames N, default 20)
import { StreamDeckTcpConnectionManager, StreamDeckTcpDiscoveryService } from '@elgato-stream-deck/tcp'

const args = process.argv.slice(2)
const ip = args.find((a, i) => !a.startsWith('--') && args[i - 1] !== '--frames')
const bench = args.includes('--bench')
const framesArg = args.indexOf('--frames')
const benchFrames = framesArg >= 0 ? Number(args[framesArg + 1]) : 20

if (args.includes('--discover')) {
	const discovery = new StreamDeckTcpDiscoveryService()
	discovery.on('up', (s) => console.log('up', s.name, s.address, s.port, s.modelName, s.serialNumber))
	discovery.on('down', (s) => console.log('down', s.name, s.address))
	setTimeout(() => process.exit(0), 8000)
} else if (!ip) {
	console.log('usage: node dock-client.mjs --discover | <ip> [--bench]')
	process.exit(1)
} else {
	run(ip)
}

function hueColor(i, n) {
	const h = (i * 255) / n
	const r = h < 85 ? 255 - h * 3 : h < 170 ? 0 : (h - 170) * 3
	const g = h < 85 ? h * 3 : h < 170 ? 255 - (h - 85) * 3 : 0
	const b = h < 85 ? 0 : h < 170 ? (h - 85) * 3 : 255 - (h - 170) * 3
	return [Math.round(r), Math.round(g), Math.round(b)]
}

async function benchmark(deck) {
	const keys = deck.CONTROLS.filter((c) => c.type === 'button' && c.feedbackType === 'lcd')
	const { width, height } = keys[0].pixelSize
	const frames = benchFrames
	const t0 = performance.now()
	for (let f = 0; f < frames; f++) {
		await Promise.all(
			keys.map((k) => {
				const buf = Buffer.alloc(width * height * 3)
				const [r, g, b] = hueColor((k.index + f) % keys.length, keys.length)
				for (let p = 0; p < width * height; p++) {
					// a gradient so the JPEG isn't trivially small
					buf[p * 3] = (r + p) & 0xff
					buf[p * 3 + 1] = g
					buf[p * 3 + 2] = (b + (p >> 4)) & 0xff
				}
				return deck.fillKeyBuffer(k.index, buf)
			}),
		)
	}
	// Writes resolve once they're in the socket. A feature read queues behind them on the dock, so its
	// reply means every image reached the deck.
	await deck.getFirmwareVersion()
	const ms = performance.now() - t0
	console.log(`bench: ${frames} full redraws of ${keys.length} keys in ${ms.toFixed(0)} ms → ${((frames * 1000) / ms).toFixed(1)} fps`)
}

function run(address) {
	const manager = new StreamDeckTcpConnectionManager()
	manager.on('error', (e) => console.log('error:', e))
	manager.on('disconnected', (d) => console.log('disconnected', d.remoteAddress, d.remotePort))
	manager.on('connected', async (deck) => {
		const where = `${deck.remoteAddress}:${deck.remotePort}`
		try {
			if (deck.CONTROLS.length === 0) {
				console.log(`dock connected at ${where}: ${deck.PRODUCT_NAME}`)
				console.log('  serial  ', await deck.getSerialNumber())
				console.log('  firmware', await deck.getFirmwareVersion())
				console.log('  mac     ', await deck.getMacAddress())
				console.log('  child   ', await deck.getChildDeviceInfo())
				deck.tcpEvents.on('childChange', (info) => console.log('child changed:', info))
				return
			}

			console.log(`deck connected at ${where}: ${deck.PRODUCT_NAME}`)
			console.log('  serial  ', await deck.getSerialNumber())
			console.log('  firmware', await deck.getFirmwareVersion())
			await deck.setBrightness(70)
			const buttons = deck.CONTROLS.filter((c) => c.type === 'button')
			await Promise.all(buttons.map((b) => deck.fillKeyColor(b.index, ...hueColor(b.index, buttons.length))))
			console.log(`  drew ${buttons.length} keys`)

			let pressedAt = 0
			deck.on('down', (c) => {
				pressedAt = performance.now()
				console.log('down', c.index)
				deck.fillKeyColor(c.index, 255, 255, 255).catch((e) => console.log('fill failed', e))
			})
			deck.on('up', (c) => {
				console.log('up', c.index, `(held ${(performance.now() - pressedAt).toFixed(0)} ms)`)
				deck.fillKeyColor(c.index, ...hueColor(c.index, buttons.length)).catch((e) => console.log('fill failed', e))
			})
			if (bench) await benchmark(deck)
		} catch (e) {
			console.log(`failed on ${where}:`, e)
		}
	})
	manager.connectTo(address)
}
