<script setup lang="ts">
/*
 * The stage: the Stream Deck on the dock's USB port, drawn as the hardware, with what is on its keys
 * right now (the Stream Deck app's layout, or the dock's own screens when no app is connected).
 *
 * Each key is an <img> addressed by its version (/api/key?n=3&v=1207): a key reloads only when what is
 * on it changed, and an unchanged key is never fetched again. Keys arrive in the deck's own orientation
 * and the model's transform turns them upright. A pressed key sinks at once, the way the real one does.
 */
import { useElementSize } from "@vueuse/core"
import { Usb } from "@lucide/vue"
import { computed, ref } from "vue"

import { store } from "../session/store.ts"

const stage = ref<HTMLElement | null>(null)
const { width, height } = useElementSize(stage)

const deck = computed(() => store.deck)
const cols = computed(() => deck.value?.cols ?? 5)
const rows = computed(() => deck.value?.rows ?? 3)

/* The largest key that fits: keys plus the gaps and the bezel, within the stage, at most 132 px. */
const keyPx = computed(() => {
  const gapRatio = 0.26
  const bezelRatio = 0.55
  const unitsW = cols.value + (cols.value - 1) * gapRatio + 2 * bezelRatio
  const unitsH = rows.value + (rows.value - 1) * gapRatio + 2 * bezelRatio + 0.5
  const fit = Math.min((width.value - 48) / unitsW, (height.value - 96) / unitsH)
  return Math.max(28, Math.min(132, Math.floor(fit)))
})

const keys = computed(() => {
  const d = deck.value
  if (!d?.attached || !d.keys) return []
  return d.keys.map((k, i) => ({
    i,
    src: k.v && !k.color ? `/api/key?n=${i}&v=${k.v}` : null,
    color: k.color ?? null,
    pressed: d.pressed?.[i] === 1,
  }))
})

const caption = computed(() => {
  const d = deck.value
  if (!d?.attached) return ""
  return d.source === "app" ? "Showing what the Stream Deck app draws" : "Showing the dock's own screens"
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
        class="deck"
        :style="{
          '--key': `${keyPx}px`,
          gridTemplateColumns: `repeat(${cols}, var(--key))`,
        }"
        :aria-label="`${deck.model}, ${cols} by ${rows} keys`"
        role="img"
      >
        <div
          v-for="k in keys"
          :key="k.i"
          class="key"
          :data-pressed="k.pressed"
          :style="k.color ? { background: k.color } : undefined"
        >
          <img
            v-if="k.src"
            :src="k.src"
            :style="{ transform: deck.transform }"
            alt=""
            draggable="false"
            decoding="async"
          />
        </div>
      </div>
      <div class="mt-5 text-center">
        <div class="text-sm font-medium tracking-tight text-white/90">{{ deck.model }}</div>
        <div class="text-xs text-white/55">{{ caption }}</div>
      </div>
    </template>
    <div v-else-if="deck" class="flex max-w-xs flex-col items-center gap-3 text-center text-white/70">
      <Usb class="size-8 text-white/50" />
      <div class="text-[15px] font-medium text-white/90">No Stream Deck plugged in</div>
      <div class="text-sm">Plug a Stream Deck MK.2 or Mini straight into the dock's USB-A port.</div>
    </div>
  </section>
</template>

<style scoped>
.stage {
  /* Black in both themes: the deck is the subject, like a photo on a dark table. */
  background: radial-gradient(120% 90% at 50% 40%, #1c1d21 0%, #0b0b0d 70%);
}

.deck {
  display: grid;
  gap: calc(var(--key) * 0.26);
  padding: calc(var(--key) * 0.55);
  padding-bottom: calc(var(--key) * 0.8);
  border-radius: calc(var(--key) * 0.42);
  background: linear-gradient(180deg, #2a2b30 0%, #1a1b1f 100%);
  box-shadow:
    0 1px 0 rgba(255, 255, 255, 0.08) inset,
    0 -2px 0 rgba(0, 0, 0, 0.5) inset,
    0 30px 60px -20px rgba(0, 0, 0, 0.8),
    0 12px 24px -12px rgba(0, 0, 0, 0.6);
}

.key {
  position: relative;
  width: var(--key);
  height: var(--key);
  border-radius: calc(var(--key) * 0.16);
  overflow: hidden;
  background: #050506;
  box-shadow:
    0 0 0 1px rgba(255, 255, 255, 0.06),
    0 2px 0 rgba(0, 0, 0, 0.6),
    0 3px 6px rgba(0, 0, 0, 0.35);
  /* The press is instant; the release settles, critically damped. */
  transition: transform 220ms cubic-bezier(0.2, 0.9, 0.3, 1), box-shadow 220ms ease-out;
}
.key[data-pressed="true"] {
  transform: scale(0.94) translateY(1px);
  box-shadow:
    0 0 0 1px rgba(255, 255, 255, 0.14),
    0 0 0 3px rgba(56, 189, 248, 0.35);
  transition-duration: 0ms;
}
.key img {
  width: 100%;
  height: 100%;
  display: block;
  image-rendering: auto;
  user-select: none;
}

@media (prefers-reduced-motion: reduce) {
  .key,
  .key[data-pressed="true"] {
    transition: box-shadow 120ms ease;
    transform: none;
  }
}
@media (prefers-contrast: more) {
  .key {
    box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.4);
  }
}
</style>
