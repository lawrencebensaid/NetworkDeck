<script setup lang="ts">
/*
 * The console's root: whichever screen the session calls for - choosing the first password, signing in,
 * waiting for a dock that does not answer, or the console itself.
 */
import { onMounted } from "vue"

import { Toaster } from "@/components/ui/sonner"
import { TooltipProvider } from "@/components/ui/tooltip"
import { checkAuth, store } from "./session/store.ts"
import AuthView from "./views/AuthView.vue"
import ConsoleView from "./views/ConsoleView.vue"
import WaitingView from "./views/WaitingView.vue"

onMounted(checkAuth)
</script>

<template>
  <TooltipProvider :delay-duration="350" :skip-delay-duration="250">
    <Transition
      mode="out-in"
      enter-active-class="transition-opacity duration-200"
      leave-active-class="transition-opacity duration-150"
      enter-from-class="opacity-0"
      leave-to-class="opacity-0"
    >
      <ConsoleView v-if="store.phase === 'signed-in'" key="console" />
      <AuthView v-else-if="store.phase === 'setup' || store.phase === 'signed-out'" key="auth" :setup="store.phase === 'setup'" />
      <WaitingView v-else key="waiting" :unreachable="store.phase === 'unreachable'" />
    </Transition>
    <Toaster position="bottom-right" />
  </TooltipProvider>
</template>
