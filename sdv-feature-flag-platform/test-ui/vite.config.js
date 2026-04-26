import dns from "node:dns";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Windows: prefer IPv4 when resolving "localhost" from the dev server / tooling.
dns.setDefaultResultOrder("ipv4first");

export default defineConfig({
  plugins: [react()],
  server: {
    host: true,
    port: 5174,
    strictPort: true,
  },
});

