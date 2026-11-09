<script setup lang="ts">
/*
 * API keys (Connectors → REST API): for scripts and tools that use the REST API. A key is made with a name, confirmed on the
 * deck, and shown once; the dock keeps only its hash. Deleting a key stops it at once.
 */
import { onMounted, ref } from "vue"
import { Check, Copy, KeyRound, Trash2 } from "@lucide/vue"
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
import { Spinner } from "@/components/ui/spinner"
import { api, type ApiKeyInfo } from "../session/api.ts"
import { store } from "../session/store.ts"

const emit = defineEmits<{ change: [] }>()
const keys = ref<ApiKeyInfo[] | null>(null)
const name = ref("")
const busy = ref(false)
const made = ref<{ name: string; key: string } | null>(null)
const copied = ref(false)

async function load() {
  try {
    keys.value = await api.keys()
    emit("change")
  } catch {
    /* signed out or offline */
  }
}
onMounted(load)

async function create() {
  busy.value = true
  const r = await api.keyCreate(name.value.trim())
  busy.value = false
  if (r.ok && r.key) {
    made.value = { name: r.name ?? name.value, key: r.key }
    name.value = ""
    copied.value = false
    void load()
  } else {
    toast.error(r.message)
  }
}

async function copy() {
  if (!made.value) return
  try {
    await navigator.clipboard.writeText(made.value.key)
    copied.value = true
  } catch {
    toast.error("Couldn't copy: select the key and copy it")
  }
}

async function remove(k: ApiKeyInfo) {
  const r = await api.keyDelete(k.id)
  if (r.ok) toast.success(`${k.name} deleted`)
  else toast.error(r.message)
  void load()
}

const when = (t: number) => (t ? new Date(t * 1000).toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" }) : "")
</script>

<template>
  <div class="flex flex-col gap-6">
    <section v-if="made" class="flex flex-col gap-2 rounded-lg border border-primary/40 bg-primary/5 p-4" data-testid="key-made">
      <p class="text-sm font-medium">{{ made.name }}: copy the key now. The dock doesn't show it again.</p>
      <div class="flex gap-2">
        <Input :model-value="made.key" readonly class="font-mono text-xs" aria-label="The new API key" @focus="($event.target as HTMLInputElement).select()" />
        <Button variant="outline" size="icon" :aria-label="copied ? 'Copied' : 'Copy the key'" @click="copy">
          <Check v-if="copied" />
          <Copy v-else />
        </Button>
      </div>
      <p class="text-xs text-muted-foreground">Send it as <code>Authorization: Bearer {{ made.key.slice(0, 8) }}…</code> with each request.</p>
      <div><Button variant="ghost" size="sm" @click="made = null">Done</Button></div>
    </section>

    <section class="flex flex-col gap-2">
      <p v-if="keys === null" class="flex items-center gap-2 text-sm text-muted-foreground"><Spinner class="size-4" /> Loading…</p>
      <p v-else-if="!keys.length" class="text-sm text-muted-foreground">No API keys yet.</p>
      <ul v-else class="divide-y rounded-lg border" data-testid="key-list">
        <li v-for="k in keys" :key="k.id" class="flex items-center gap-3 px-3 py-2.5">
          <KeyRound class="size-4 shrink-0 text-muted-foreground" />
          <div class="min-w-0 flex-1">
            <div class="truncate text-sm font-medium">{{ k.name }}</div>
            <div class="text-xs text-muted-foreground">
              <template v-if="k.created">Made {{ when(k.created) }}</template>
              <template v-if="k.created && k.last_used"> · </template>
              <template v-if="k.last_used">used {{ when(k.last_used) }}</template>
              <template v-else-if="!k.created">Not used since the dock started</template>
            </div>
          </div>
          <AlertDialog>
            <AlertDialogTrigger as-child>
              <Button variant="ghost" size="icon-sm" :aria-label="`Delete ${k.name}`"><Trash2 /></Button>
            </AlertDialogTrigger>
            <AlertDialogContent>
              <AlertDialogHeader>
                <AlertDialogTitle>Delete {{ k.name }}?</AlertDialogTitle>
                <AlertDialogDescription>Whatever uses this key stops working at once.</AlertDialogDescription>
              </AlertDialogHeader>
              <AlertDialogFooter>
                <AlertDialogCancel>Cancel</AlertDialogCancel>
                <AlertDialogAction @click="remove(k)">Delete</AlertDialogAction>
              </AlertDialogFooter>
            </AlertDialogContent>
          </AlertDialog>
        </li>
      </ul>
    </section>

    <form class="flex flex-col gap-2 border-t pt-5" @submit.prevent="create">
      <Label for="key-name">Make a key</Label>
      <div class="flex gap-2">
        <Input id="key-name" v-model="name" placeholder="What it is for" maxlength="32" autocomplete="off" />
        <Button type="submit" :disabled="!name.trim() || busy">
          <Spinner v-if="busy" class="size-4" />
          Make
        </Button>
      </div>
      <p v-if="busy && !store.viaHotspot" class="text-sm" role="status">Confirm on the Stream Deck: press ✓.</p>
      <p class="text-xs text-muted-foreground">At most 8 keys. A key can do what you can here; changes are still confirmed on the deck.</p>
    </form>
  </div>
</template>
