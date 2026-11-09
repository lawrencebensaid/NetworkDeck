<script setup lang="ts">
/* Changing the web interface's password. Other browsers are signed out. */
import { computed, ref } from "vue"
import { toast } from "vue-sonner"

import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { api } from "../session/api.ts"

const current = ref("")
const next = ref("")
const repeat = ref("")
const busy = ref(false)
const ok = computed(() => current.value && next.value.length >= 8 && next.value === repeat.value)

async function change() {
  busy.value = true
  const r = await api.changePassword(current.value, next.value)
  busy.value = false
  if (r.ok) {
    current.value = next.value = repeat.value = ""
    toast.success(r.message)
  } else {
    toast.error(r.message)
  }
}
</script>

<template>
  <form class="flex flex-col gap-3" @submit.prevent="change">
    <div class="flex flex-col gap-1.5"><Label for="cur">Current password</Label><Input id="cur" v-model="current" type="password" autocomplete="current-password" /></div>
    <div class="flex flex-col gap-1.5"><Label for="new">New password</Label><Input id="new" v-model="next" type="password" autocomplete="new-password" /><p class="text-xs text-muted-foreground">At least 8 characters.</p></div>
    <div class="flex flex-col gap-1.5"><Label for="rep">Repeat it</Label><Input id="rep" v-model="repeat" type="password" autocomplete="new-password" /></div>
    <Button type="submit" :disabled="!ok || busy" class="mt-1">Change password</Button>
    <p class="mt-2 text-xs text-muted-foreground">Forgot it? A factory reset on the deck (hold the top-left and top-right keys for 10 seconds) clears it, along with the network settings.</p>
  </form>
</template>
