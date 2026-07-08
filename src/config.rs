use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

pub const CONFIG_FILE: &str = "config.json";

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct GameSettings {
    pub start_delay: Value,
    pub stop_delay: Value,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct AppConfig {
    pub check_interval: u64,
    pub start_delay: u64,
    pub stop_delay: u64,
    pub start_with_windows: bool,
    pub games: HashMap<String, GameSettings>,
}

impl Default for AppConfig {
    fn default() -> Self {
        let mut games = HashMap::new();
        games.insert(
            "VRChat.exe".to_string(),
            GameSettings {
                start_delay: Value::String("default".to_string()),
                stop_delay: Value::String("default".to_string()),
            },
        );

        Self {
            check_interval: 2,
            start_delay: 2,
            stop_delay: 2,
            start_with_windows: false,
            games,
        }
    }
}

pub fn get_config_path() -> PathBuf {
    let mut path = std::env::current_exe().unwrap_or_else(|_| PathBuf::from("."));
    path.pop();
    path.join(CONFIG_FILE)
}

pub fn load_config() -> AppConfig {
    let path = get_config_path();
    if !path.exists() {
        let default_cfg = AppConfig::default();
        save_config(&default_cfg);
        return default_cfg;
    }

    match fs::read_to_string(&path) {
        Ok(data) => serde_json::from_str(&data).unwrap_or_else(|_| AppConfig::default()),
        Err(_) => AppConfig::default(),
    }
}

pub fn save_config(config: &AppConfig) {
    let path = get_config_path();
    if let Ok(data) = serde_json::to_string_pretty(config) {
        let _ = fs::write(path, data);
    }
}

pub fn resolve_delay(config: &AppConfig, game: &str, key: &str) -> u64 {
    let fallback = match key {
        "start_delay" => config.start_delay,
        "stop_delay" => config.stop_delay,
        _ => 0,
    };

    if let Some(game_settings) = config.games.get(game) {
        let val = match key {
            "start_delay" => &game_settings.start_delay,
            "stop_delay" => &game_settings.stop_delay,
            _ => return fallback,
        };

        if let Some(num) = val.as_u64() {
            return num;
        }
        if let Some(s) = val.as_str() {
            if s != "default" {
                if let Ok(parsed) = s.parse::<u64>() {
                    return parsed;
                }
            }
        }
    }

    fallback
}