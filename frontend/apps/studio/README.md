# OPC Engineering Studio

Tauri 2 desktop application with a browser fallback for editing
`*.modbusproj.json` projects and monitoring OPC UA servers.

## Development

```sh
npm install
npm run dev          # browser mode with file-picker and OPC UA mock
npm run tauri dev    # native mode
npm run lint
npm run typecheck
```sh
npm test             # Vitest unit/component
npm run test:e2e     # Playwright browser mock
cargo test --manifest-path src-tauri/Cargo.toml
OPC_STUDIO_NATIVE=1 npm run test:e2e   # opt-in native sidecar (lab)
```

Schema fixtures shared with C++ live in `tests/fixtures/` at the repo root.

The project validator compiles `DOCs/schemas/modbus-project.schema.json` into the
frontend build. Native validation additionally executes the bundled `opc-map`
sidecar. The native monitor communicates with `opc-monitor` using JSONL:

- input: `connect`, `disconnect`, `browse`, and `subscribe` command objects;
- output: `status`, `browse`, `value`, and `diagnostic` event objects matching
  the TypeScript types in `src/domain.ts`.

Before packaging, place target-specific `opc-map` and `opc-monitor` binaries in
`src-tauri/binaries` as documented there. CI performs this step for Windows,
Linux, and macOS.

## Security boundary

- Monitoring is read-only; Studio never sends OPC UA Write or Call requests.
- File access is limited to paths explicitly selected by the user.
- Project writes use a temporary file and atomic replacement.
- Credentials are not persisted. The current sidecar supports the laboratory
  `None/None` profile; certificate profiles require an encryption-enabled
  open62541 build.
