import { fileURLToPath } from "node:url";

import tailwindcss from "@tailwindcss/vite";
import vue from "@vitejs/plugin-vue";
import { defineConfig } from "vite";

import { singleFile } from "./tools/single-file.ts";

/*
 *   npm run dev             against a real dock: ND_DEVICE=http://10.0.1.187
 *   npm run build           the single page (dist/index.html)
 *   npm run build:firmware  that page, gzipped into firmware/components/web/assets
 */
export default defineConfig(() => {
  const device = process.env.ND_DEVICE ?? "http://deck32-204c.local";
  return {
    plugins: [vue(), tailwindcss(), singleFile()],
    resolve: { alias: { "@": fileURLToPath(new URL("./src", import.meta.url)) } },
    define: {
      __VUE_OPTIONS_API__: "false",
      __VUE_PROD_DEVTOOLS__: "false",
      __VUE_PROD_HYDRATION_MISMATCH_DETAILS__: "false",
    },
    build: {
      outDir: "dist",
      emptyOutDir: true,
      target: "es2022",
      cssCodeSplit: false,
      modulePreload: false,
      assetsInlineLimit: () => true,
      reportCompressedSize: true,
    },
    server: { host: true, proxy: { "/api": { target: device, changeOrigin: true } } },
  };
});
