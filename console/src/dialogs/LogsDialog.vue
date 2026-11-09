<script setup lang="ts">
/*
 * The dock's log: what happened, newest first, by category, kept in the dock's flash through restarts
 * and power loss. Grouped by day; entries from before the clock was set show the time since that start.
 * "Protocol" is the live trace of frames between the Stream Deck app and the dock.
 */
import { computed, onBeforeUnmount, ref, watch } from "vue"
import { Download, RefreshCw, Search, X } from "@lucide/vue"

import { Button } from "@/components/ui/button"
import { Dialog, DialogClose, DialogContent, DialogDescription, DialogTitle } from "@/components/ui/dialog"
import { cn } from "@/lib/utils"
import { api, type LogCategory, type LogPage, type LogRecord } from "../session/api.ts"

const open = defineModel<boolean>("open", { required: true })

type Filter = "all" | LogCategory | "protocol"
const FILTERS: { id: Filter; title: string }[] = [
  { id: "all", title: "All" },
  { id: "connection", title: "Connections" },
  { id: "deck", title: "Stream Deck" },
  { id: "network", title: "Network" },
  { id: "security", title: "Users & Security" },
  { id: "update", title: "Updates" },
  { id: "matter", title: "Matter" },
  { id: "system", title: "System" },
]
const TAGS: Record<LogCategory, string> = {
  connection: "App",
  deck: "Deck",
  network: "Network",
  security: "Sign-in",
  update: "Update",
  matter: "Matter",
  system: "System",
}

const filter = ref<Filter>("all")
const query = ref("")
const page = ref<LogPage | null>(null)
const records = ref<LogRecord[]>([])
const loading = ref(false)
const more = ref(true)
const trace = ref("")
let timer = 0

async function load(fresh = true) {
  loading.value = true
  try {
    const p = await api.log(fresh ? 0 : (records.value.at(-1)?.s ?? 0), 300)
    page.value = p
    if (fresh) {
      /* keep what was paged in before, newer entries in front */
      const newest = records.value[0]?.s ?? 0
      const fresher = p.records.filter((r) => r.s > newest)
      records.value = records.value.length && fresher.length < p.records.length ? [...fresher, ...records.value] : p.records
    } else {
      records.value = [...records.value, ...p.records]
    }
    more.value = p.records.length === 300
  } catch {
    /* offline or signed out: the lights say so */
  } finally {
    loading.value = false
  }
}

async function loadTrace() {
  try {
    trace.value = await api.trace()
  } catch {
    /* as above */
  }
}

function tick() {
  if (filter.value === "protocol") void loadTrace()
  else void load(true)
}

watch(
  open,
  (o) => {
    clearInterval(timer)
    if (!o) return
    records.value = []
    void load(true)
    timer = window.setInterval(tick, 4000)
  },
  { immediate: true },
)
watch(filter, (f) => f === "protocol" && loadTrace())
onBeforeUnmount(() => clearInterval(timer))

/* When an entry happened: its own time, or worked out from this start's clock, or none (before the clock was set). */
function whenOf(r: LogRecord): number | null {
  if (r.t) return r.t * 1000
  const p = page.value
  if (p && r.b === p.boot && p.now) return (p.now - (p.uptime - r.u)) * 1000
  return null
}

const shown = computed(() => {
  const q = query.value.trim().toLowerCase()
  return records.value.filter(
    (r) => (filter.value === "all" || r.c === filter.value) && (!q || r.m.toLowerCase().includes(q) || TAGS[r.c].toLowerCase().includes(q)),
  )
})

function dayTitle(ms: number) {
  const d = new Date(ms)
  const today = new Date()
  const yesterday = new Date(today.getTime() - 86400000)
  const same = (a: Date, b: Date) => a.toDateString() === b.toDateString()
  if (same(d, today)) return "Today"
  if (same(d, yesterday)) return "Yesterday"
  return d.toLocaleDateString(undefined, { weekday: "short", month: "short", day: "numeric" })
}

const groups = computed(() => {
  const out: { title: string; items: LogRecord[] }[] = []
  for (const r of shown.value) {
    const w = whenOf(r)
    const title = w === null ? `Before the clock was set (start ${r.b})` : dayTitle(w)
    const last = out.at(-1)
    if (last && last.title === title) last.items.push(r)
    else out.push({ title, items: [r] })
  }
  return out
})

function uptime(s: number) {
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  return `+${h}:${String(m).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`
}
function timeOf(r: LogRecord) {
  const w = whenOf(r)
  if (w === null) return uptime(r.u)
  const ago = (Date.now() - w) / 1000
  if (ago < 60) return "just now"
  if (ago < 3600) return `${Math.floor(ago / 60)} min ago`
  if (ago < 12 * 3600) return `${Math.floor(ago / 3600)} h ${Math.floor((ago % 3600) / 60)} min ago`
  return new Date(w).toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit", second: "2-digit" })
}
function fullTime(r: LogRecord) {
  const w = whenOf(r)
  return w === null ? `${uptime(r.u)} after start ${r.b}` : new Date(w).toLocaleString()
}

function download() {
  const lines = shown.value.map((r) => {
    const w = whenOf(r)
    return `${w === null ? `start ${r.b} ${uptime(r.u)}` : new Date(w).toISOString()}\t${r.l}\t${r.c}\t${r.m}`
  })
  const blob = new Blob([lines.join("\n") + "\n"], { type: "text/plain" })
  const a = document.createElement("a")
  a.href = URL.createObjectURL(blob)
  a.download = "networkdeck-log.txt"
  a.click()
  URL.revokeObjectURL(a.href)
}

const DOT = { info: "bg-muted-foreground/50", warn: "bg-status-attention", error: "bg-status-error" } as const
const navClass = (id: Filter) =>
  cn(
    "flex shrink-0 items-center rounded-md px-2.5 py-1.5 text-left text-sm whitespace-nowrap transition-colors outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
    filter.value === id ? "bg-muted font-medium text-foreground" : "text-muted-foreground hover:bg-muted/60 hover:text-foreground",
  )
</script>

<template>
  <Dialog v-model:open="open">
    <DialogContent
      :show-close-button="false"
      data-testid="logs-dialog"
      class="flex h-[min(720px,calc(100dvh-2rem))] w-[calc(100vw-2rem)] max-w-none flex-col gap-0 overflow-hidden bg-background p-0 sm:max-w-[960px] max-sm:top-0 max-sm:left-0 max-sm:h-dvh max-sm:w-screen max-sm:translate-x-0 max-sm:translate-y-0 max-sm:rounded-none"
    >
      <header class="flex h-12 shrink-0 items-center justify-between gap-3 border-b bg-card px-4">
        <DialogTitle class="text-sm font-semibold">Logs</DialogTitle>
        <DialogDescription class="sr-only">What the dock logged, by category.</DialogDescription>
        <div class="flex items-center gap-1">
          <Button type="button" variant="ghost" size="icon-sm" aria-label="Download the log" :disabled="filter === 'protocol'" @click="download"><Download /></Button>
          <Button type="button" variant="ghost" size="icon-sm" aria-label="Refresh" @click="tick"><RefreshCw :class="loading ? 'animate-spin' : ''" /></Button>
          <DialogClose as-child>
            <Button type="button" variant="ghost" size="icon-sm" aria-label="Close the log"><X /></Button>
          </DialogClose>
        </div>
      </header>

      <div class="flex min-h-0 flex-1 flex-col sm:flex-row">
        <nav aria-label="Log categories" class="flex shrink-0 gap-1 overflow-x-auto border-b p-2 sm:w-52 sm:flex-col sm:overflow-y-auto sm:border-r sm:border-b-0 sm:p-3">
          <button v-for="f in FILTERS" :key="f.id" type="button" :data-filter="f.id" :aria-current="filter === f.id ? 'page' : undefined" :class="navClass(f.id)" @click="filter = f.id">
            {{ f.title }}
          </button>
          <div class="mx-2 my-1 hidden border-t sm:block" role="separator" />
          <button type="button" data-filter="protocol" :aria-current="filter === 'protocol' ? 'page' : undefined" :class="navClass('protocol')" @click="filter = 'protocol'">
            Protocol
          </button>
          <p v-if="page" class="mt-3 hidden px-2.5 text-xs leading-relaxed text-muted-foreground sm:block">
            {{ page.kept.toLocaleString() }} of {{ page.capacity.toLocaleString() }} records kept, in the dock's flash: they survive restarts and power loss.
          </p>
        </nav>

        <div v-if="filter === 'protocol'" class="flex min-h-0 flex-1 flex-col gap-2 p-4">
          <p class="text-sm text-muted-foreground">The last frames between the Stream Deck app and the dock, newest last. Live; not kept.</p>
          <pre class="min-h-0 flex-1 overflow-auto rounded-md border bg-muted/40 p-2 font-mono text-[11px] leading-snug whitespace-pre-wrap" data-testid="log-trace">{{ trace || "Nothing yet." }}</pre>
        </div>

        <div v-else class="flex min-h-0 min-w-0 flex-1 flex-col">
          <div class="p-3">
            <label class="flex items-center gap-2 rounded-md border bg-background px-2.5 py-1.5 text-sm focus-within:ring-3 focus-within:ring-ring/50">
              <Search class="size-4 text-muted-foreground" />
              <input v-model="query" type="search" placeholder="Find in the log" class="min-w-0 flex-1 bg-transparent outline-none placeholder:text-muted-foreground" aria-label="Find in the log" />
            </label>
          </div>
          <div class="min-h-0 flex-1 overflow-y-auto px-3 pb-3" data-testid="log-list">
            <p v-if="!shown.length && !loading" class="px-2 py-8 text-center text-sm text-muted-foreground">Nothing logged{{ query ? " that matches" : " here yet" }}.</p>
            <section v-for="g in groups" :key="g.title" class="mb-2">
              <h3 class="sticky top-0 z-10 bg-background px-2 py-1.5 text-[11px] font-semibold tracking-wide text-muted-foreground uppercase">{{ g.title }}</h3>
              <ul>
                <li v-for="r in g.items" :key="r.s" class="flex items-center gap-3 rounded-md px-2 py-1.5 text-sm hover:bg-muted/50" :title="fullTime(r)" :data-level="r.l">
                  <span :class="cn('size-1.5 shrink-0 rounded-full', DOT[r.l])" aria-hidden="true" />
                  <span class="w-16 shrink-0 rounded bg-muted px-1.5 py-0.5 text-center text-[11px] text-muted-foreground">{{ TAGS[r.c] }}</span>
                  <span class="min-w-0 flex-1 truncate" :class="r.l === 'error' ? 'text-status-error-text' : ''">{{ r.m }}</span>
                  <span class="shrink-0 text-xs tabular-nums text-muted-foreground">{{ timeOf(r) }}</span>
                </li>
              </ul>
            </section>
            <div v-if="more && records.length" class="flex justify-center py-2">
              <Button variant="outline" size="sm" :disabled="loading" @click="load(false)">Older entries</Button>
            </div>
          </div>
        </div>
      </div>
    </DialogContent>
  </Dialog>
</template>
