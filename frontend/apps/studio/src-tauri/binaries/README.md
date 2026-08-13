# Bundled command-line tools

Tauri expects target-specific sidecars in this directory at packaging time:

- `opc-map-$TARGET_TRIPLE` (`.exe` on Windows)
- `opc-monitor-$TARGET_TRIPLE` (`.exe` on Windows)

`opc-map` must support `validate <project>`. `opc-monitor` reads command objects and
writes monitor events as newline-delimited JSON on standard input/output. The
release pipeline should copy the compiled tools here before `tauri build`.
