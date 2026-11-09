<script setup lang="ts">
/*
 * The stage: the Stream Deck on the dock's USB port, drawn as the hardware (sizes and layout from
 * session/models.ts, in the deck's own finish), with what is on its keys right now (the Stream Deck
 * app's layout, or the dock's own screens when no app is connected).
 *
 * Each key is an <img> addressed by its version (/api/key?n=3&v=1207): a key reloads only when what is
 * on it changed, and an unchanged key is never fetched again. Keys arrive in the deck's own orientation
 * and the model's transform turns them upright. A pressed key sinks at once, the way the real one does.
 */
import { useElementSize } from "@vueuse/core"
import { Usb } from "@lucide/vue"
import { computed, ref } from "vue"

import logo from "../assets/stream-deck-logo.png"
import { finishOf, hardware } from "../session/models.ts"
import { store } from "../session/store.ts"

const LOGO_RATIO = 560 / 110 /* the logo image, width over height */

const stage = ref<HTMLElement | null>(null)
const { width, height } = useElementSize(stage)

const deck = computed(() => store.deck)
const hw = computed(() => hardware(deck.value?.pid, deck.value?.cols, deck.value?.rows, deck.value?.model))
const finish = computed(() => finishOf(hw.value, deck.value?.product))

/* Pixels per millimetre: the body as large as fits, with room for the caption, at most 4.6 (an MK.2 ~540 px wide). */
const scale = computed(() => {
  const h = hw.value
  const fit = Math.min((width.value - 64) / h.w, (height.value - 120) / h.h)
  return Math.max(1, Math.min(4.6, fit))
})
const mm = (v: number) => `${(v * scale.value).toFixed(2)}px`
const box = (x: number, y: number, w: number, h: number, r?: number) => ({
  left: mm(x),
  top: mm(y),
  width: mm(w),
  height: mm(h),
  ...(r !== undefined ? { borderRadius: mm(r) } : {}),
})

const keys = computed(() => {
  const d = deck.value
  const h = hw.value
  const gw = h.cols * h.key + Math.max(0, h.cols - 1) * h.gap
  const left = (h.w - gw) / 2
  return Array.from({ length: h.cols * h.rows }, (_, i) => {
    const k = d?.live ? d.keys?.[i] : undefined
    return {
      i,
      style: box(left + (i % h.cols) * (h.key + h.gap), h.top + Math.floor(i / h.cols) * (h.key + h.gap), h.key, h.key, h.key * 0.17),
      src: k?.v && !k.color ? `/api/key?n=${i}&v=${k.v}` : null,
      color: k?.color ?? null,
      pressed: d?.pressed?.[i] === 1,
    }
  })
})

const logoStyle = computed(() => {
  const h = hw.value
  const lh = h.logoW / LOGO_RATIO
  return { ...box((h.w - h.logoW) / 2, h.logoY - lh / 2, h.logoW, lh), maskImage: `url(${logo})`, WebkitMaskImage: `url(${logo})` }
})

const name = computed(() => deck.value?.model ?? hw.value.name)
const caption = computed(() => {
  const d = deck.value
  if (!d?.attached) return ""
  if (d.live === false) return "The dock passes it through to the Stream Deck app. Its keys aren't shown here yet."
  if (d.source !== "app") return "Showing the dock's own screens"
  const ip = store.status?.host_ip
  return ip ? `Showing what the Stream Deck app on ${ip} draws` : "Showing what the Stream Deck app draws"
})
</script>

<template>
  <section
    ref="stage"
    class="stage relative flex min-h-0 min-w-0 flex-1 flex-col items-center justify-center overflow-hidden"
    aria-label="Stream Deck"
    data-testid="stage"
  >
    <template v-if="deck?.attached">
      <div
        class="body"
        :data-finish="finish"
        :style="{ width: mm(hw.w), height: mm(hw.h), borderRadius: mm(hw.radius), '--mm': `${scale}px` }"
        :aria-label="hw.cols ? `${name}, ${hw.cols} by ${hw.rows} keys` : name"
        role="img"
        data-testid="deck-body"
      >
        <span v-if="hw.logoW" class="logo" :style="logoStyle" aria-hidden="true" />

        <div v-for="(p, i) in hw.parts" :key="`p${i}`" :class="['part', p.kind]" :style="box(p.x, p.y, p.w, p.h, p.kind === 'dial' || p.kind === 'touch' ? p.w / 2 : p.radius)" />

        <div v-for="k in keys" :key="k.i" class="key" :data-pressed="k.pressed" :style="k.style">
          <div class="lcd" :style="k.color ? { background: k.color } : undefined">
            <img v-if="k.src" :src="k.src" :style="{ transform: deck.transform }" alt="" draggable="false" decoding="async" />
          </div>
        </div>
      </div>
      <div class="mt-6 text-center">
        <div class="text-sm font-medium tracking-tight text-foreground/90">{{ name }}</div>
        <div class="text-xs text-muted-foreground" data-testid="stage-caption">{{ caption }}</div>
      </div>
    </template>
    <div v-else-if="deck" class="flex max-w-xs flex-col items-center gap-3 text-center text-muted-foreground">
      <Usb class="size-8 opacity-70" />
      <div class="text-[15px] font-medium text-foreground/90">No Stream Deck plugged in</div>
      <div class="text-sm">Plug a Stream Deck straight into the dock's USB-A port.</div>
    </div>
  </section>
</template>

<style scoped>
/* The stage follows the theme: a soft light table, or a dark one. */
.stage {
  background: radial-gradient(120% 90% at 50% 40%, #f7f7f8 0%, #e4e4e7 75%);
}
:root.dark .stage {
  background: radial-gradient(120% 90% at 50% 40%, #1c1d21 0%, #0b0b0d 70%);
}

/* The body: a flat slab with a soft edge, lit from above. */
.body {
  position: relative;
  flex-shrink: 0;
}
.body[data-finish="black"] {
  background: linear-gradient(180deg, #2f3034 0%, #242528 55%, #1d1e21 100%);
  box-shadow:
    0 0 0 1px rgba(0, 0, 0, 0.55),
    inset 0 1px 0 rgba(255, 255, 255, 0.07),
    0 calc(var(--mm) * 4) calc(var(--mm) * 10) calc(var(--mm) * -3) rgba(0, 0, 0, 0.45);
  --logo: #f2f2f2;
}
.body[data-finish="white"] {
  background: linear-gradient(180deg, #fbfbfb 0%, #f1f1f2 60%, #e9e9eb 100%);
  box-shadow:
    0 0 0 1px rgba(0, 0, 0, 0.08),
    inset 0 1px 0 #fff,
    0 calc(var(--mm) * 4) calc(var(--mm) * 10) calc(var(--mm) * -3) rgba(0, 0, 0, 0.22);
  --logo: #111113;
}

.logo {
  position: absolute;
  background: var(--logo);
  mask-size: 100% 100%;
  -webkit-mask-size: 100% 100%;
  mask-repeat: no-repeat;
  -webkit-mask-repeat: no-repeat;
}

/* A key: the cap's rim (chrome on the black decks, black on the white ones) around the LCD. */
.key {
  position: absolute;
  padding: calc(var(--mm) * 0.75);
  transition: transform 220ms cubic-bezier(0.2, 0.9, 0.3, 1);
}
.body[data-finish="black"] .key {
  background: linear-gradient(160deg, #e9eaee 0%, #8d8f96 28%, #2b2c30 52%, #9a9ca3 78%, #f1f2f5 100%);
  box-shadow:
    0 0 0 calc(var(--mm) * 0.35) #0c0c0e,
    0 calc(var(--mm) * 0.5) calc(var(--mm) * 1.2) rgba(0, 0, 0, 0.6);
}
.body[data-finish="white"] .key {
  background: linear-gradient(180deg, #2a2a2d 0%, #050506 60%, #19191b 100%);
  box-shadow:
    0 0 0 calc(var(--mm) * 0.2) rgba(0, 0, 0, 0.35),
    0 calc(var(--mm) * 0.5) calc(var(--mm) * 1.2) rgba(0, 0, 0, 0.28);
}
.lcd {
  position: relative;
  width: 100%;
  height: 100%;
  overflow: hidden;
  border-radius: calc(var(--mm) * 2);
  background: #060607;
  box-shadow: inset 0 0 0 calc(var(--mm) * 0.25) rgba(0, 0, 0, 0.9);
}
/* The cap's glass: a faint highlight across the top. */
.lcd::after {
  content: "";
  position: absolute;
  inset: 0;
  background: linear-gradient(170deg, rgba(255, 255, 255, 0.1) 0%, rgba(255, 255, 255, 0) 38%);
  pointer-events: none;
}
.lcd img {
  width: 100%;
  height: 100%;
  display: block;
  user-select: none;
}
.key[data-pressed="true"] {
  transform: scale(0.95) translateY(1px);
  transition-duration: 0ms;
}

.part {
  position: absolute;
}
.part.screen {
  background: #060607;
  box-shadow: inset 0 0 0 calc(var(--mm) * 0.3) rgba(255, 255, 255, 0.05), 0 0 0 calc(var(--mm) * 0.3) #0a0a0b;
}
.part.tray {
  background: linear-gradient(180deg, #121214 0%, #1d1e21 45%, #26272a 100%);
  box-shadow: inset 0 calc(var(--mm) * 1.5) calc(var(--mm) * 3) rgba(0, 0, 0, 0.7);
}
.part.dial {
  background: radial-gradient(circle at 50% 50%, #3b3c41 0%, #2a2b2f 35%, #141416 62%, #3a3b40 64%, #18181a 100%);
  box-shadow:
    0 calc(var(--mm) * 0.8) calc(var(--mm) * 1.6) rgba(0, 0, 0, 0.7),
    inset 0 calc(var(--mm) * 0.3) 0 rgba(255, 255, 255, 0.12);
}
.part.touch {
  background: #141416;
  box-shadow: inset 0 0 0 calc(var(--mm) * 0.3) rgba(255, 255, 255, 0.08);
}
.part.pedal {
  background: linear-gradient(180deg, #34353a 0%, #202124 100%);
  box-shadow: 0 1px 0 rgba(255, 255, 255, 0.06) inset, 0 4px 10px rgba(0, 0, 0, 0.3);
}

@media (prefers-reduced-motion: reduce) {
  .key,
  .key[data-pressed="true"] {
    transition: none;
    transform: none;
  }
}
</style>