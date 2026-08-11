use chrono::{SecondsFormat, TimeZone, Utc};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::{
    fs,
    io::Write,
    path::{Path, PathBuf},
};
use tauri::{AppHandle, Emitter, State};
use tauri_plugin_shell::{
    process::{CommandChild, CommandEvent},
    ShellExt,
};
use tempfile::NamedTempFile;
use tokio::sync::Mutex;

const MAX_PROJECT_BYTES: u64 = 16 * 1024 * 1024;

#[derive(Default)]
struct MonitorState {
    child: Mutex<Option<CommandChild>>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct ProjectFile {
    path: String,
    content: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct ValidationIssue {
    severity: &'static str,
    path: String,
    message: String,
    source: &'static str,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct ConnectionProfile {
    endpoint_url: String,
    security_policy: String,
    security_mode: String,
    username: Option<String>,
    password: Option<String>,
}

fn validate_project_path(path: &Path) -> Result<(), String> {
    let name = path
        .file_name()
        .and_then(|value| value.to_str())
        .ok_or_else(|| "Project path must have a UTF-8 file name".to_owned())?;
    if !(name.ends_with(".modbusproj.json") || name.ends_with(".json")) {
        return Err("Project file must end in .modbusproj.json or .json".to_owned());
    }
    Ok(())
}

#[tauri::command]
fn read_project(path: String) -> Result<ProjectFile, String> {
    let path = PathBuf::from(path);
    validate_project_path(&path)?;
    let canonical = path.canonicalize().map_err(|error| error.to_string())?;
    let metadata = fs::metadata(&canonical).map_err(|error| error.to_string())?;
    if !metadata.is_file() {
        return Err("Selected project is not a regular file".to_owned());
    }
    if metadata.len() > MAX_PROJECT_BYTES {
        return Err("Project exceeds the 16 MiB safety limit".to_owned());
    }
    let content = fs::read_to_string(&canonical).map_err(|error| error.to_string())?;
    Ok(ProjectFile {
        path: canonical.to_string_lossy().into_owned(),
        content,
    })
}

#[tauri::command]
fn write_project(path: String, content: String) -> Result<String, String> {
    if content.len() as u64 > MAX_PROJECT_BYTES {
        return Err("Project exceeds the 16 MiB safety limit".to_owned());
    }
    serde_json::from_str::<Value>(&content)
        .map_err(|error| format!("Refusing to save invalid JSON: {error}"))?;
    let path = PathBuf::from(path);
    validate_project_path(&path)?;
    if path.exists() {
        let metadata = fs::symlink_metadata(&path).map_err(|error| error.to_string())?;
        if metadata.file_type().is_symlink() || !metadata.is_file() {
            return Err("Refusing to overwrite a symlink or non-regular file".to_owned());
        }
    }
    let parent = path
        .parent()
        .filter(|value| !value.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let canonical_parent = parent.canonicalize().map_err(|error| error.to_string())?;
    let destination =
        canonical_parent.join(path.file_name().ok_or("Destination has no file name")?);
    let mut temporary =
        NamedTempFile::new_in(&canonical_parent).map_err(|error| error.to_string())?;
    temporary
        .write_all(content.as_bytes())
        .and_then(|_| temporary.as_file().sync_all())
        .map_err(|error| error.to_string())?;
    temporary
        .persist(&destination)
        .map_err(|error| error.error.to_string())?;
    Ok(destination.to_string_lossy().into_owned())
}

fn parse_validation_line(line: &str) -> Option<ValidationIssue> {
    let (severity, remainder) = line
        .strip_prefix("error: ")
        .map(|rest| ("error", rest))
        .or_else(|| line.strip_prefix("warning: ").map(|rest| ("warning", rest)))?;
    let (path, message) = remainder.split_once(": ").unwrap_or(("/", remainder));
    Some(ValidationIssue {
        severity,
        path: path.to_owned(),
        message: message.to_owned(),
        source: "opc-map",
    })
}

#[tauri::command]
async fn validate_project(app: AppHandle, content: String) -> Result<Vec<ValidationIssue>, String> {
    if content.len() as u64 > MAX_PROJECT_BYTES {
        return Err("Project exceeds the 16 MiB safety limit".to_owned());
    }
    let mut temporary = tempfile::Builder::new()
        .suffix(".modbusproj.json")
        .tempfile()
        .map_err(|error| error.to_string())?;
    temporary
        .write_all(content.as_bytes())
        .map_err(|error| error.to_string())?;
    let path = temporary.path().to_string_lossy().into_owned();
    let output = app
        .shell()
        .sidecar("opc-map")
        .map_err(|error| error.to_string())?
        .args(["validate", &path])
        .output()
        .await
        .map_err(|error| error.to_string())?;
    let issues = String::from_utf8_lossy(&output.stderr)
        .lines()
        .filter_map(parse_validation_line)
        .collect::<Vec<_>>();
    if !output.status.success() && issues.is_empty() {
        return Err(format!(
            "opc-map validation failed: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        ));
    }
    Ok(issues)
}

fn timestamp(value: Option<i64>) -> String {
    value
        .and_then(|millis| Utc.timestamp_millis_opt(millis).single())
        .map(|date| date.to_rfc3339_opts(SecondsFormat::Millis, true))
        .unwrap_or_default()
}

fn translate_monitor_event(raw: Value) -> Value {
    match raw.get("event").and_then(Value::as_str) {
        Some("connection") => {
            let status = match raw.get("state").and_then(Value::as_str) {
                Some("connected") => "connected",
                Some("reconnecting") => "connecting",
                _ => "disconnected",
            };
            json!({ "type": "status", "status": status, "message": raw.get("statusName").and_then(Value::as_str) })
        }
        Some("browseResult") => {
            let nodes = raw
                .get("children")
                .cloned()
                .unwrap_or_else(|| Value::Array(Vec::new()));
            json!({ "type": "browse", "nodes": nodes })
        }
        Some("dataChange") => {
            let node_id = raw
                .get("nodeId")
                .and_then(Value::as_str)
                .unwrap_or_default();
            let browse_name = node_id.rsplit(['/', ';', '=']).next().unwrap_or(node_id);
            let status_name = raw
                .get("statusName")
                .and_then(Value::as_str)
                .unwrap_or("Good");
            let quality = if status_name.starts_with("Bad") {
                "Bad"
            } else if status_name.starts_with("Uncertain") {
                "Uncertain"
            } else {
                "Good"
            };
            json!({
                "type": "value",
                "value": {
                    "nodeId": node_id,
                    "browseName": browse_name,
                    "value": raw.get("value").cloned().unwrap_or(Value::Null),
                    "quality": quality,
                    "sourceTimestamp": timestamp(raw.get("sourceTimestamp").and_then(Value::as_i64)),
                    "serverTimestamp": timestamp(raw.get("serverTimestamp").and_then(Value::as_i64))
                }
            })
        }
        Some("error") => json!({
            "type": "diagnostic", "level": "error",
            "message": raw.get("message").and_then(Value::as_str).unwrap_or("OPC UA error"),
            "timestamp": Utc::now().to_rfc3339_opts(SecondsFormat::Millis, true)
        }),
        _ => json!({
            "type": "diagnostic", "level": "warning", "message": "Unknown opc-monitor event",
            "timestamp": Utc::now().to_rfc3339_opts(SecondsFormat::Millis, true)
        }),
    }
}

fn emit_diagnostic(app: &AppHandle, level: &str, message: impl Into<String>) {
    let _ = app.emit(
        "opc-monitor://event",
        json!({
            "type": "diagnostic", "level": level, "message": message.into(),
            "timestamp": Utc::now().to_rfc3339_opts(SecondsFormat::Millis, true)
        }),
    );
}

async fn ensure_monitor(app: &AppHandle, state: &MonitorState) -> Result<(), String> {
    let mut guard = state.child.lock().await;
    if guard.is_some() {
        return Ok(());
    }
    let (mut receiver, child) = app
        .shell()
        .sidecar("opc-monitor")
        .map_err(|error| error.to_string())?
        .spawn()
        .map_err(|error| error.to_string())?;
    let app_handle = app.clone();
    tauri::async_runtime::spawn(async move {
        let mut pending = String::new();
        while let Some(event) = receiver.recv().await {
            match event {
                CommandEvent::Stdout(bytes) => {
                    pending.push_str(&String::from_utf8_lossy(&bytes));
                    while let Some(index) = pending.find('\n') {
                        let line = pending[..index].trim().to_owned();
                        pending.drain(..=index);
                        if line.is_empty() {
                            continue;
                        }
                        match serde_json::from_str::<Value>(&line) {
                            Ok(raw) => {
                                let _ = app_handle
                                    .emit("opc-monitor://event", translate_monitor_event(raw));
                            }
                            Err(error) => emit_diagnostic(
                                &app_handle,
                                "error",
                                format!("Invalid sidecar output: {error}"),
                            ),
                        }
                    }
                }
                CommandEvent::Stderr(bytes) => {
                    let message = String::from_utf8_lossy(&bytes).trim().to_owned();
                    if !message.is_empty() {
                        emit_diagnostic(&app_handle, "warning", message);
                    }
                }
                CommandEvent::Error(message) => emit_diagnostic(&app_handle, "error", message),
                CommandEvent::Terminated(payload) => {
                    let _ = app_handle.emit(
                        "opc-monitor://event",
                        json!({
                            "type": "status", "status": "disconnected",
                            "message": format!("opc-monitor exited: {:?}", payload.code)
                        }),
                    );
                }
                _ => {}
            }
        }
    });
    *guard = Some(child);
    Ok(())
}

async fn send_monitor_command(
    app: &AppHandle,
    state: &MonitorState,
    command: Value,
) -> Result<(), String> {
    ensure_monitor(app, state).await?;
    let mut line = serde_json::to_vec(&command).map_err(|error| error.to_string())?;
    line.push(b'\n');
    let mut guard = state.child.lock().await;
    guard
        .as_mut()
        .ok_or_else(|| "opc-monitor is not running".to_owned())?
        .write(&line)
        .map_err(|error| error.to_string())
}

#[tauri::command]
async fn monitor_connect(
    app: AppHandle,
    state: State<'_, MonitorState>,
    profile: ConnectionProfile,
) -> Result<(), String> {
    if profile.endpoint_url.len() > 2048 || !profile.endpoint_url.starts_with("opc.tcp://") {
        return Err("Endpoint must be an opc.tcp URL".to_owned());
    }
    if profile.security_policy != "None" || profile.security_mode != "None" {
        return Err(
            "Encrypted OPC UA profiles require a certificate-enabled open62541 build".to_owned(),
        );
    }
    if profile
        .username
        .as_deref()
        .is_some_and(|value| !value.is_empty())
        || profile
            .password
            .as_deref()
            .is_some_and(|value| !value.is_empty())
    {
        return Err("Username authentication is not enabled in opc-monitor v1".to_owned());
    }
    send_monitor_command(
        &app,
        &state,
        json!({ "command": "connect", "endpoint": profile.endpoint_url }),
    )
    .await
}

#[tauri::command]
async fn monitor_disconnect(app: AppHandle, state: State<'_, MonitorState>) -> Result<(), String> {
    send_monitor_command(&app, &state, json!({ "command": "disconnect" })).await
}

#[tauri::command]
async fn monitor_browse(
    app: AppHandle,
    state: State<'_, MonitorState>,
    node_id: Option<String>,
) -> Result<(), String> {
    send_monitor_command(
        &app,
        &state,
        json!({
            "command": "browse", "nodeId": node_id.unwrap_or_else(|| "ns=0;i=85".to_owned())
        }),
    )
    .await
}

#[tauri::command]
async fn monitor_subscribe(
    app: AppHandle,
    state: State<'_, MonitorState>,
    node_ids: Vec<String>,
) -> Result<(), String> {
    if node_ids.len() > 10_000 || node_ids.iter().any(|node| node.len() > 4096) {
        return Err("A subscription is limited to 10,000 safe node ids".to_owned());
    }
    for (index, node_id) in node_ids.into_iter().enumerate() {
        send_monitor_command(
            &app,
            &state,
            json!({
                "command": "subscribe", "subscriptionId": format!("studio-{index}-{node_id}"),
                "nodeId": node_id, "samplingIntervalMs": 250
            }),
        )
        .await?;
    }
    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(MonitorState::default())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![
            read_project,
            write_project,
            validate_project,
            monitor_connect,
            monitor_disconnect,
            monitor_browse,
            monitor_subscribe
        ])
        .run(tauri::generate_context!())
        .expect("failed to run OPC Engineering Studio");
}
