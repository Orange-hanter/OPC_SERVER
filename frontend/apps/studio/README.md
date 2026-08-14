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
npm test
npm run build
```

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
- Credentials are not persisted. Certificate profiles (`Sign` /
  `SignAndEncrypt` + `Basic256Sha256`) are forwarded to `opc-monitor`; if
  cert/key paths are empty, the sidecar generates a self-signed client cert.
