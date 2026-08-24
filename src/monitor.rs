use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};
use sysinfo::System;
use sysinfo::ProcessesToUpdate;
use crate::config::{resolve_delay, AppConfig};
use crate::utils::{obs_start, obs_stop, obs_scene_switch};

#[derive(Clone, PartialEq, Debug)]
pub enum IconColor {
    Red,
    Green,
    Orange,
}

pub struct RuntimeState {
    pub recording: bool,
    pub monitoring: bool,
    pub automation_enabled: bool,
    pub active_game: String,
    pub icon_color: IconColor,
    pub tooltip: String,
    pub ui_needs_update: bool,
}

impl Default for RuntimeState {
    fn default() -> Self {
        Self {
            recording: false,
            monitoring: true,
            automation_enabled: true,
            active_game: "None".to_string(),
            icon_color: IconColor::Red,
            tooltip: "Idle".to_string(),
            ui_needs_update: true,
        }
    }
}

pub fn is_obs_running(sys: &System) -> bool {
    sys.processes().values().any(|p| {
        p.name().to_string_lossy().to_lowercase().contains("obs")
    })
}

pub fn get_running_games(sys: &System, configured_games: &[String]) -> Vec<String> {
    let mut found = Vec::new();
    for process in sys.processes().values() {
        let name = process.name();
        if configured_games.iter().any(|g| g.to_lowercase() == name.to_string_lossy().to_lowercase()) {
            if !found.contains(&name.to_string_lossy().to_string()) {
                found.push(name.to_string_lossy().to_string());
            }
        }
    }
    found
}

fn switch_game_scene(config: &AppConfig, game: &str) {
    if let Some(game_settings) = config.games.get(game) {
        if let Some(scene) = &game_settings.scene {
            if !scene.trim().is_empty() {
                obs_scene_switch(scene);
            }
        }
    }
}

pub fn spawn_monitor_thread(
    config: Arc<Mutex<AppConfig>>,
    state: Arc<Mutex<RuntimeState>>,
) {
    thread::spawn(move || {
        let mut sys = System::new_all();
        let mut start_delay_timer: Option<Instant> = None;
        let mut stop_delay_timer: Option<Instant> = None;

        loop {
            let (interval, configured_games, cfg_clone) = {
                let cfg = config.lock().unwrap();
                let games = cfg.games.keys().cloned().collect::<Vec<String>>();
                (cfg.check_interval, games, cfg.clone())
            };

            {
                let s = state.lock().unwrap();
                if !s.monitoring {
                    break;
                }
                if !s.automation_enabled {
                    drop(s);
                    thread::sleep(Duration::from_secs(1));
                    continue;
                }
            }

            sys.refresh_processes(ProcessesToUpdate::All, true);
            let running_games = get_running_games(&sys, &configured_games);
            let obs_active = is_obs_running(&sys);

            let mut s = state.lock().unwrap();

            if s.recording && !obs_active {
                s.recording = false;
                s.active_game = "None".to_string();
                s.icon_color = IconColor::Red;
                s.tooltip = "OBS stopped".to_string();
                s.ui_needs_update = true;
            }

            if !running_games.is_empty() && !s.recording {
                let game = &running_games[0];
                let delay = resolve_delay(&cfg_clone, game, "start_delay");

                match start_delay_timer {
                    None => start_delay_timer = Some(Instant::now()),
                    Some(timer) => {
                        if timer.elapsed().as_secs() >= delay && obs_active {
                            switch_game_scene(&cfg_clone, game);

                            obs_start();
                            s.recording = true;
                            s.active_game = game.clone();
                            start_delay_timer = None;
                            stop_delay_timer = None;

                            s.icon_color = IconColor::Green;
                            s.tooltip = format!("Recording {}", s.active_game);
                            s.ui_needs_update = true;
                        }
                    }
                }
            }

            else if running_games.is_empty() && s.recording {
                let delay = resolve_delay(&cfg_clone, &s.active_game, "stop_delay");

                match stop_delay_timer {
                    None => stop_delay_timer = Some(Instant::now()),
                    Some(timer) => {
                        if timer.elapsed().as_secs() >= delay {
                            obs_stop();
                            s.recording = false;
                            s.active_game = "None".to_string();
                            stop_delay_timer = None;

                            s.icon_color = IconColor::Red;
                            s.tooltip = "Idle".to_string();
                            s.ui_needs_update = true;
                        }
                    }
                }
            } else {
                if !running_games.is_empty() {
                    stop_delay_timer = None;
                } else {
                    start_delay_timer = None;
                }
            }

            drop(s);
            thread::sleep(Duration::from_secs(interval));
        }
    });
}