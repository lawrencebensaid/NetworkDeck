<script setup lang="ts">
/*
 * System: the dock's hardware options. The status LED (on or off at once, and its data pin) and the USB
 * port a Stream Deck plugs into: its data lines are the chip's own (shown, not settable), and on a dev
 * board with a switched 5 V supply, the pin that turns it on. Pins apply at the next start; the restart
 * is here too.
 */
import { computed, ref, watch } from "vue"
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
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Switch } from "@/components/ui/switch"
import { api } from "../session/api.ts"
import { store } from "../session/store.ts"

const y = computed(() => store.system)
const ledPin = ref("")
const usbPin = ref("")
const pinned = ref(false) /* pins saved this time: they apply after a restart */
const pin = (n: number) => (n < 0 ? "" : String(n))
watch(
  y,
  (v) => {
    if (!v || pinned.value) return
    ledPin.value = pin(v.led_gpio)
    usbPin.value = pin(v.usb_power_gpio)
  },
  { immediate: true },
)
const dirty = computed(() => !!y.value && (ledPin.value !== pin(y.value.led_gpio) || usbPin.value !== pin(y.value.usb_power_gpio)))

async function refresh() {
  try {
    store.system = await api.system()
  } catch {
    /* offline */
  }
}

async function setLed(on: boolean) {
  const r = await api.setSystem({ led: on ? "1" : "0" })
  if (!r.ok) toast.error(r.message)
  void refresh()
}

async function savePins() {
  const r = await api.setSystem({ led_gpio: ledPin.value.trim() || "none", usb_power_gpio: usbPin.value.trim() || "none" })
  if (r.ok) {
    pinned.value = true
    toast.success("Saved: the pins apply when the dock restarts")
  } else {
    toast.error(r.message)
  }
  await refresh()
  pinned.value = false
}

const restarting = ref(false)
async function restart() {
  restarting.value = true
  const r = await api.restart()
  if (r.ok) {
    toast.success("Restarting…")
    setTimeout(() => location.reload(), 9000)
  } else {
    restarting.value = false
    toast.error(r.message)
  }
}
</script>

<template>
  <div v-if="!y" class="text-sm text-muted-foreground">Asking the dock…</div>
  <div v-else class="flex flex-col gap-6">
    <form class="flex flex-col gap-3" @submit.prevent="savePins">
      <div class="flex items-center justify-between gap-4 rounded-lg border p-3">
        <div>
          <Label for="led-on">Status LED</Label>
          <p class="text-xs text-muted-foreground">Blue in setup or without a network, green when ready, purple while the Stream Deck app is connected.</p>
        </div>
        <Switch id="led-on" :model-value="y.led" data-testid="led-toggle" @update:model-value="setLed(Boolean($event))" />
      </div>
      <div class="grid grid-cols-[1fr_8rem] items-center gap-x-4 gap-y-1">
        <Label for="led-pin">LED data pin (WS2812)</Label>
        <Input id="led-pin" v-model="ledPin" inputmode="numeric" :placeholder="y.default_led_gpio >= 0 ? String(y.default_led_gpio) : 'none'" />
        <p class="col-span-2 text-xs text-muted-foreground">{{ y.default_led_gpio >= 0 ? `GPIO ${y.default_led_gpio} on ${y.chip} dev boards.` : `This board has no addressable LED; set its pin if you add one.` }}</p>
      </div>

      <h3 class="mt-3 border-t pt-5 text-sm font-medium">USB port</h3>
      <template v-if="y.usb_host">
        <p class="-mt-2 text-xs text-muted-foreground">Where the Stream Deck plugs in. For a USB-A socket added to a dev board:</p>
        <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1 text-sm">
          <dt class="text-muted-foreground">Data (D− / D+)</dt>
          <dd>{{ y.usb_data_pins }} <span class="text-muted-foreground">(fixed by the {{ y.chip }})</span></dd>
          <dt class="text-muted-foreground">5 V and ground</dt>
          <dd>the board's 5 V and GND pins</dd>
        </dl>
        <div class="grid grid-cols-[1fr_8rem] items-center gap-x-4 gap-y-1">
          <Label for="usb-pin">5 V switch pin (high = on)</Label>
          <Input id="usb-pin" v-model="usbPin" inputmode="numeric" placeholder="none" />
          <p class="col-span-2 text-xs text-muted-foreground">Only for a board that switches the port's 5 V with a GPIO; empty when the 5 V is always on.</p>
        </div>
      </template>
      <p v-else class="-mt-2 text-xs text-muted-foreground">The {{ y.chip }} has no USB host controller, so no Stream Deck can be plugged into it. Use an ESP32-P4 or -S3 for that.</p>
      <p class="text-xs text-muted-foreground">Pins apply when the dock restarts.</p>
      <div><Button type="submit" variant="outline" size="sm" :disabled="!dirty">Save pins</Button></div>
    </form>

    <section class="flex flex-col gap-2 border-t pt-5">
      <h3 class="text-sm font-medium">Restart</h3>
      <p class="text-xs text-muted-foreground">The Stream Deck app reconnects by itself after about 10 seconds.</p>
      <AlertDialog>
        <AlertDialogTrigger as-child>
          <div><Button variant="outline" size="sm" :disabled="restarting">Restart the dock</Button></div>
        </AlertDialogTrigger>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Restart the dock?</AlertDialogTitle>
            <AlertDialogDescription>The deck goes dark for a few seconds{{ y.usb_host && !store.viaHotspot ? "; confirm on the Stream Deck too" : "" }}.</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Cancel</AlertDialogCancel>
            <AlertDialogAction @click="restart">Restart</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </section>
  </div>
</template>
