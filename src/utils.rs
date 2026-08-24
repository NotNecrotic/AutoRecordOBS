use std::fs;
use std::path::PathBuf;
use std::process::Command;
use tray_icon::Icon;
use winreg::enums::*;
use winreg::RegKey;
use std::os::windows::process::CommandExt;

pub const APP_NAME: &str = "AutoRecordOBS";
const OBS_CMD_BYTES: &[u8] = include_bytes!("../bin/obs-cmd.exe");
pub const OBS_CMD_NAME: &str = "obs-cmd.exe";
const TRAY_IDLE: &[u8] = include_bytes!("../assets/idle.png");
const TRAY_PAUSED: &[u8] = include_bytes!("../assets/pause.png");
const TRAY_RECORDING: &[u8] = include_bytes!("../assets/record.png");
pub const CREATE_NO_WINDOW: u32 = 0x08000000;

pub fn set_startup(enable: bool) {
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    let path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    if let Ok(key) = hkcu.open_subkey_with_flags(path, KEY_WRITE) {
        if enable {
            if let Ok(exe_path) = std::env::current_exe() {
                if let Some(path_str) = exe_path.to_str() {
                    let _ = key.set_value(APP_NAME, &path_str);
                }
            }
        } else {
            let _ = key.delete_value(APP_NAME);
        }
    }
}

pub fn obs_cmd_path() -> PathBuf {
    let mut path = std::env::current_exe().unwrap_or_else(|_| PathBuf::from("."));
    path.pop(); 
    path.push(OBS_CMD_NAME);

    if !path.exists() {
        if let Err(e) = fs::write(&path, OBS_CMD_BYTES) {
            eprintln!("Failed to extract embedded obs-cmd.exe: {}", e);
        }
    }

    path
}

pub fn run_obs(args: &[&str]) {
    let cmd_path = obs_cmd_path();
    let _ = Command::new(cmd_path)
        .args(args)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .creation_flags(CREATE_NO_WINDOW)
        .spawn();
}

pub fn obs_start() {
    run_obs(&["recording", "start"]);
}

pub fn obs_stop() {
    run_obs(&["recording", "stop"]);
}

pub fn obs_scene_switch(scene: &str) {
    run_obs(&["scene", "switch", scene]);
}

pub fn open_config_in_editor() {
    let path = crate::config::get_config_path();
    let _ = Command::new("cmd")
        .args(["/C", "start", "", path.to_str().unwrap_or("")])
        .spawn();
}

pub fn tray_icon(data: &[u8]) -> Icon {
    let image = image::load_from_memory(data)
        .expect("Failed to load tray icon")
        .into_rgba8();

    let (width, height) = image.dimensions();

    Icon::from_rgba(
        image.into_raw(),
        width,
        height,
    )
    .expect("Failed to create tray icon")
}

pub fn tray_idle_icon() -> Icon {
    tray_icon(TRAY_IDLE)
}

pub fn tray_paused_icon() -> Icon {
    tray_icon(TRAY_PAUSED)
}

pub fn tray_recording_icon() -> Icon {
    tray_icon(TRAY_RECORDING)
}