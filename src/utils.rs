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

pub fn icon_circle(r: u8, g: u8, b: u8) -> Icon {
    let width = 64;
    let height = 64;
    let mut rgba = Vec::with_capacity((width * height * 4) as usize);
    let center = 32_i32;
    let radius = 24_i32;

    for y in 0..height {
        for x in 0..width {
            let dx = x as i32 - center;
            let dy = y as i32 - center;
            
            if dx * dx + dy * dy <= radius * radius {
                rgba.extend_from_slice(&[r, g, b, 255]);
            } else {
                rgba.extend_from_slice(&[255, 255, 255, 255]);
            }
        }
    }

    Icon::from_rgba(rgba, width, height).expect("Failed to create icon")
}