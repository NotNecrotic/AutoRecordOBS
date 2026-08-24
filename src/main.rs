// Hide Windows console window in release builds
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod monitor;
mod utils;

use std::sync::{Arc, Mutex};
use std::time::Duration;
use tao::event_loop::{ControlFlow, EventLoopBuilder};
use tray_icon::menu::{CheckMenuItem, Menu, MenuEvent, MenuItem, PredefinedMenuItem};
use tray_icon::{TrayIconBuilder, TrayIconEvent};

use crate::config::{load_config, save_config};
use crate::monitor::{spawn_monitor_thread, IconColor, RuntimeState};
use crate::utils::{icon_circle, obs_stop, open_config_in_editor, set_startup};

fn main() {
    let initial_config = load_config();
    set_startup(initial_config.start_with_windows);

    let config = Arc::new(Mutex::new(initial_config.clone()));
    let state = Arc::new(Mutex::new(RuntimeState::default()));

    spawn_monitor_thread(Arc::clone(&config), Arc::clone(&state));

    let event_loop = EventLoopBuilder::new().build();

    let item_game = MenuItem::new("Idle", false, None);
    let item_rec = MenuItem::new("⏹ Not Recording", false, None);
    let item_startup = CheckMenuItem::new("Start with Windows", true, initial_config.start_with_windows, None);
    let item_edit = MenuItem::new("⚙ Edit Config", true, None);
    let item_reload = MenuItem::new("🔄 Reload Config", true, None);
    let item_pause = CheckMenuItem::new("⏸ Pause Automation", true, false, None);
    let item_exit = MenuItem::new("❌ Exit", true, None);

    let menu = Menu::new();
    let _ = menu.append_items(&[
        &item_game,
        &item_rec,
        &PredefinedMenuItem::separator(),
        &item_startup,
        &PredefinedMenuItem::separator(),
        &item_edit,
        &item_reload,
        &item_pause,
        &PredefinedMenuItem::separator(),
        &item_exit,
    ]);

    let mut tray_icon = Some(
        TrayIconBuilder::new()
            .with_menu(Box::new(menu))
            .with_tooltip("Idle")
            .with_icon(icon_circle(255, 0, 0))
            .build()
            .expect("Failed to build tray icon"),
    );

    let menu_channel = MenuEvent::receiver();
    let _tray_channel = TrayIconEvent::receiver();

    event_loop.run(move |_event, _, control_flow| {
        *control_flow = ControlFlow::WaitUntil(std::time::Instant::now() + Duration::from_millis(200));

        if let Ok(event) = menu_channel.try_recv() {
            if event.id == item_startup.id() {
                let mut cfg = config.lock().unwrap();
                cfg.start_with_windows = item_startup.is_checked();
                save_config(&cfg);
                set_startup(cfg.start_with_windows);
            } else if event.id == item_edit.id() {
                open_config_in_editor();
            } else if event.id == item_reload.id() {
                let new_cfg = load_config();
                set_startup(new_cfg.start_with_windows);
                item_startup.set_checked(new_cfg.start_with_windows);
                *config.lock().unwrap() = new_cfg;
            } else if event.id == item_pause.id() {
                let mut runtime = state.lock().unwrap();
                runtime.automation_enabled = !item_pause.is_checked();
                runtime.ui_needs_update = true;
                if !runtime.automation_enabled {
                    runtime.icon_color = IconColor::Orange;
                    runtime.tooltip = "Paused".to_string();
                } else {
                    runtime.icon_color = if runtime.recording { IconColor::Green } else { IconColor::Red };
                    runtime.tooltip = if runtime.recording { format!("Recording {}", runtime.active_game) } else { "Idle".to_string() };
                }
            } else if event.id == item_exit.id() {
                let mut runtime = state.lock().unwrap();
                runtime.monitoring = false;
                if runtime.recording {
                    obs_stop();
                }
                tray_icon.take();
                *control_flow = ControlFlow::Exit;
            }
        }

        let mut runtime = state.lock().unwrap();
        if runtime.ui_needs_update {
            if let Some(tray) = tray_icon.as_mut() {
                let _ = tray.set_tooltip(Some(&runtime.tooltip));
                
                match runtime.icon_color {
                    IconColor::Red => { let _ = tray.set_icon(Some(icon_circle(255, 0, 0))); },
                    IconColor::Green => { let _ = tray.set_icon(Some(icon_circle(0, 255, 0))); },
                    IconColor::Orange => { let _ = tray.set_icon(Some(icon_circle(255, 165, 0))); },
                }

                if runtime.recording {
                    item_game.set_text(format!("🎮 {}", runtime.active_game));
                    item_rec.set_text("⏺ Recording");
                } else {
                    item_game.set_text("Idle");
                    item_rec.set_text("⏹ Not Recording");
                }
            }
            runtime.ui_needs_update = false;
        }
    });
}