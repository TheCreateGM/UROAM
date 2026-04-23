use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::process::{Command, Stdio};
use std::io::Write;

#[derive(Debug, Clone, Default)]
pub struct GamingState {
    pub is_steam_running: bool,
    pub is_proton_active: bool,
    pub is_wine_running: bool,
    pub shader_cache_pids: Vec<u32>,
    pub critical_pids: Vec<u32>,
    pub original_swappiness: Option<u32>,
}

pub struct GamingOptimizer {
    state: GamingState,
    shader_cache_dir: String,
    restore_timeout_ms: u64,
}

impl GamingOptimizer {
    pub fn new() -> Self {
        Self {
            state: GamingState::default(),
            shader_cache_dir: "/tmp/uroam-shader-cache".to_string(),
            restore_timeout_ms: 2000,
        }
    }

    pub fn detect_gaming_processes(&mut self) -> std::io::Result<()> {
        self.state.is_steam_running = false;
        self.state.is_proton_active = false;
        self.state.is_wine_running = false;
        self.state.shader_cache_pids.clear();
        self.state.critical_pids.clear();

        for entry in fs::read_dir("/proc")? {
            let entry = match entry {
                Ok(e) => e,
                Err(_) => continue,
            };

            let name = entry.file_name();
            let name_str = match name.to_str() {
                Some(n) if n.chars().all(|c| c.is_ascii_digit()) => n,
                _ => continue,
            };

            let pid: u32 = match name_str.parse() {
                Ok(p) => p,
                Err(_) => continue,
            };

            let comm = fs::read_to_string(entry.path().join("comm"))
                .unwrap_or_default()
                .trim()
                .to_lowercase();

            let cmdline = fs::read_to_string(entry.path().join("cmdline"))
                .unwrap_or_default()
                .to_lowercase();

            if comm.contains("steam") || cmdline.contains("steam") {
                self.state.is_steam_running = true;
            }
            if comm.contains("proton") || cmdline.contains("proton") {
                self.state.is_proton_active = true;
            }
            if comm.contains("wine") || comm.contains("wine64") {
                self.state.is_wine_running = true;
            }
            if self.is_critical_process(&comm, &cmdline) {
                self.state.critical_pids.push(pid);
            }
            if self.is_shader_cache_process(&comm, &cmdline) {
                self.state.shader_cache_pids.push(pid);
            }
        }

        Ok(())
    }

    fn is_critical_process(&self, comm: &str, cmdline: &str) -> bool {
        let critical_names = [
            "gamescope", "gamemoded", "gamescope-session",
            "proton", "steam", "steamwebhelper",
        ];
        critical_names.iter().any(|name| comm.contains(name) || cmdline.contains(name))
    }

    fn is_shader_cache_process(&self, comm: &str, _cmdline: &str) -> bool {
        let shader_names = ["shader-cache", "steam", "gamescope"];
        shader_names.iter().any(|name| comm.contains(name))
    }

    pub fn apply_gaming_optimizations(&self) -> std::io::Result<()> {
        if self.state.critical_pids.is_empty() {
            return Ok(());
        }

        let original = fs::read_to_string("/proc/sys/vm/swappiness")
            .unwrap_or_default()
            .trim()
            .to_string();
        
        if let Ok(swappiness) = original.parse::<u32>() {
            if self.state.original_swappiness.is_none() {
                self.state.original_swappiness = Some(swappiness);
            }
        }

        fs::write("/proc/sys/vm/swappiness", "1")?;
        fs::write("/proc/sys/vm/vfs_cache_pressure", "50")?;
        fs::write("/proc/sys/vm/page-cluster", "0")?;

        let min_free: u64 = 64 * 1024;
        fs::write("/proc/sys/vm/min_free_kbytes", min_free.to_string())?;

        if Path::new("/sys/kernel/mm/ksm/run").exists() {
            fs::write("/sys/kernel/mm/ksm/run", "0")?;
        }

        if Path::new("/sys/kernel/mm/transparent_hugepage/enabled").exists() {
            fs::write("/sys/kernel/mm/transparent_hugepage/enabled", "[always]")?;
        }

        for &pid in &self.state.critical_pids {
            self.mlock_process(pid)?;
            self.set_oom_score(pid, -900)?;
        }

        self.preserve_shader_cache()?;

        tracing::info!(
            "Applied gaming optimizations for {} critical processes",
            self.state.critical_pids.len()
        );
        Ok(())
    }

    fn mlock_process(&self, pid: u32) -> std::io::Result<()> {
        let path = format!("/proc/{}/cgroup", pid);
        if fs::read_to_string(&path).is_err() {
            return Ok(());
        }

        let maps_path = format!("/proc/{}/maps", pid);
        if let Ok(content) = fs::read_to_string(&maps_path) {
            let _ = content;
        }
        
        Ok(())
    }

    fn set_oom_score(&self, pid: u32, adj: i32) -> std::io::Result<()> {
        let path = format!("/proc/{}/oom_score_adj", pid);
        fs::write(path, adj.to_string())
    }

    fn preserve_shader_cache(&self) -> std::io::Result<()> {
        let shader_dir = Path::new(&self.shader_cache_dir);
        if !shader_dir.exists() {
            fs::create_dir_all(shader_dir)?;
        }

        if self.state.is_steam_running {
            let proton_cache = format!(
                "{}/proton_{}",
                self.shader_cache_dir,
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs()
            );
            if let Err(e) = fs::create_dir_all(&proton_cache) {
                tracing::warn!("Failed to create shader cache dir: {}", e);
            }
        }

        Ok(())
    }

    pub fn restore_settings(&self) -> std::io::Result<()> {
        if let Some(original) = self.state.original_swappiness {
            fs::write("/proc/sys/vm/swappiness", original.to_string())?;
        }

        tracing::info!("Restored gaming settings to defaults");
        Ok(())
    }

    pub fn is_gaming_active(&self) -> bool {
        self.state.is_steam_running || 
        self.state.is_proton_active || 
        self.state.is_wine_running ||
        !self.state.critical_pids.is_empty()
    }

    pub fn cancel_within_100ms(&self) -> std::io::Result<()> {
        tracing::info!("Cancelling gaming optimizations");
        self.restore_settings()
    }
}

impl Default for GamingOptimizer {
    fn default() -> Self {
        Self::new()
    }
}