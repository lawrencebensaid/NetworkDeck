<script setup lang="ts">
/* The Matter logo as an icon: its shape as a mask, in the current text colour, sized like the Lucide icons. */
import logo from "../assets/matter-logo.png"
</script>

<template>
  <span
    class="inline-block size-[18px] shrink-0 bg-current"
    :style="{ maskImage: `url(${logo})`, WebkitMaskImage: `url(${logo})`, maskSize: 'contain', WebkitMaskSize: 'contain', maskRepeat: 'no-repeat', WebkitMaskRepeat: 'no-repeat', maskPosition: 'center' }"
    aria-hidden="true"
  />
</template>
