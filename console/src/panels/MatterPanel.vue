<script setup lang="ts">
/*
 * Matter: adding the dock to Home Assistant, Apple Home or Google Home. The QR code shows while pairing
 * is open; every key becomes a button there. Leaving every controller is confirmed here and on the deck.
 */
import { computed, onBeforeUnmount, onMounted, ref } from "vue"
import { encode } from "uqr"
import { toast } from "vue-sonner"

import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Spinner } from "@/components/ui/spinner"
import { api, type Matter } from "../session/api.ts"
import { store } from "../session/store.ts"

const m = ref<Matter | null>(null)
const busy = ref<"" | "open" | "unpair">("")
let timer = 0

async function load() {
  try {
    m.value = await api.matter()
  } catch {
    /* signed out or offline */
  }
}

onMounted(() => {
  void load()
  timer = window.setInterval(load, 3000) /* see it appear once a controller added it */
})
onBeforeUnmount(() => clearInterval(timer))

/* The QR code as SVG squares, one per dark module (a 1-module quiet zone; the white card adds more). */
const qr = computed(() => {
  if (!m.value?.qr) return null
  const { size, data } = encode(m.value.qr, { border: 1, ecc: "M" })
  const cells: { x: number; y: number }[] = []
  data.forEach((row, y) => row.forEach((dark, x) => dark && cells.push({ x, y })))
  return { size, cells }
})

/* 2202-133-3609: how controllers print the 11-digit code. */
const manual = computed(() => {
  const c = m.value?.manual ?? ""
  return c.length === 11 ? `${c.slice(0, 4)}-${c.slice(4, 7)}-${c.slice(7)}` : c
})

const controllers = computed(() => {
  const n = m.value?.fabrics ?? 0
  return n === 0 ? "Not added to a controller yet" : n === 1 ? "Added to 1 controller" : `Added to ${n} controllers`
})

async function openPairing() {
  busy.value = "open"
  const r = await api.matterOpen()
  busy.value = ""
  if (r.ok) toast.success(r.message)
  else toast.error(r.message)
  await load()
}

async function unpair() {
  busy.value = "unpair"
  const r = await api.matterUnpair()
  if (r.ok) {
    toast.success(r.message)
    setTimeout(() => location.reload(), 12000)
  } else {
    busy.value = ""
    toast.error(r.message)
  }
}
</script>

<template>
  <div v-if="!m" class="flex items-center gap-2 text-sm text-muted-foreground"><Spinner class="size-4" /> Loading…</div>
  <p v-else-if="!m.running" class="text-sm text-muted-foreground">Matter is not running on this dock. The serial log says why.</p>
  <div v-else class="flex flex-col gap-5">
    <div class="flex items-center justify-between gap-3">
      <span class="text-sm">{{ controllers }}</span>
      <Badge :variant="m.window_open ? 'default' : 'secondary'" data-testid="matter-window">{{ m.window_open ? "Pairing open" : "Pairing closed" }}</Badge>
    </div>

    <template v-if="m.window_open">
      <div class="flex flex-col items-center gap-3">
        <svg
          v-if="qr"
          class="size-44 rounded-lg bg-white p-2"
          :viewBox="`0 0 ${qr.size} ${qr.size}`"
          shape-rendering="crispEdges"
          role="img"
          :aria-label="`Matter QR code, setup code ${manual}`"
          data-testid="matter-qr"
        >
          <rect v-for="c in qr.cells" :key="`${c.x},${c.y}`" :x="c.x" :y="c.y" width="1" height="1" fill="#000" />
        </svg>
        <div class="text-center">
          <div class="font-mono text-lg tracking-wider tabular-nums select-all" data-testid="matter-code">{{ manual }}</div>
          <div class="text-xs text-muted-foreground">Setup code</div>
        </div>
      </div>
      <p class="text-sm text-muted-foreground">
        Scan the code in Home Assistant (<span class="text-foreground">Add Matter device</span> in the companion app), Apple Home or
        Google Home, or type the setup code. The dock is uncertified: confirm when the app asks. Each key becomes a button.
      </p>
    </template>
    <template v-else>
      <p class="text-sm text-muted-foreground">
        Open pairing to add the dock to another controller, such as Home Assistant next to Apple Home. It stays open for
        {{ Math.round((m.window_seconds ?? 900) / 60) }} minutes.
      </p>
      <Button :disabled="busy !== ''" @click="openPairing"><Spinner v-if="busy === 'open'" class="size-4" /> Open pairing</Button>
    </template>

    <div v-if="(m.fabrics ?? 0) > 0" class="flex flex-col gap-2 border-t pt-4">
      <AlertDialog>
        <AlertDialogTrigger as-child>
          <Button variant="destructive" :disabled="busy !== ''"><Spinner v-if="busy === 'unpair'" class="size-4" /> Remove from all controllers</Button>
        </AlertDialogTrigger>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Remove from all controllers?</AlertDialogTitle>
            <AlertDialogDescription>
              The dock leaves every Matter controller it was added to and restarts. Automations that use its keys stop
              working until you add it again.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Cancel</AlertDialogCancel>
            <AlertDialogAction @click="unpair">Remove</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
      <p v-if="busy === 'unpair' && !store.viaHotspot" class="flex items-center gap-2 text-sm" role="status"><Spinner class="size-4" /> Confirm on the Stream Deck: press ✓.</p>
    </div>
  </div>
</template>
