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
`src-tauri/binaries` as documented there.
# React + TypeScript + Vite

This template provides a minimal setup to get React working in Vite with HMR and some Oxlint rules.

Currently, two official plugins are available:

- [@vitejs/plugin-react](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react) uses [Oxc](https://oxc.rs)
- [@vitejs/plugin-react-swc](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react-swc) uses [SWC](https://swc.rs/)

## React Compiler

The React Compiler is not enabled on this template because of its impact on dev & build performances. To add it, see [this documentation](https://react.dev/learn/react-compiler/installation).

## Expanding the Oxlint configuration

If you are developing a production application, we recommend enabling type-aware lint rules by installing `oxlint-tsgolint` and editing `.oxlintrc.json`:

```json
{
  "$schema": "./node_modules/oxlint/configuration_schema.json",
  "plugins": ["react", "typescript", "oxc"],
  "options": {
    "typeAware": true
  },
  "rules": {
    "react/rules-of-hooks": "error",
    "react/only-export-components": ["warn", { "allowConstantExport": true }]
  }
}
```

See the [Oxlint rules documentation](https://oxc.rs/docs/guide/usage/linter/rules) for the full list of rules and categories.
