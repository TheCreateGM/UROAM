use std::fs;
use std::path::Path;
use std::process::Stdio;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OptimizationLevel {
    Minimal,
    Normal,
    Aggressive,
}

impl Default for OptimizationLevel {
    fn default() -> Self {
        Self::Normal
    }
}

#[derive(Debug, Clone)]
pub struct IdleConfig {
    pub idle_threshold_secs: u64,
    pub optimization_level: OptimizationLevel,
    pub preload_enabled: bool,
}

impl Default for IdleConfig {
    fn default() -> Self {
        Self {
            idle_threshold_secs: 300,
            optimization_level: OptimizationLevel::Normal,
            preload_enabled: false,
        }
    }
}

pub struct IdleOptimizer {
    config: IdleConfig,
    idle_start: Option<Instant>,
    is_optimizing: Arc<AtomicBool>,
    cancel_pending: Arc<AtomicBool>,
}

impl IdleOptimizer {
    pub fn new() -> Self {
        Self {
            config: IdleConfig::default(),
            idle_start: None,
            is_optimizing: Arc::new(AtomicBool::new(false)),
            cancel_pending: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn with_config(config: IdleConfig) -> Self {
        Self {
            config,
            idle_start: None,
            is_optimizing: Arc::new(AtomicBool::new(false)),
            cancel_pending: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn check_idle(&mut self, memory_usage_percent: f32, active_processes: u32) -> bool {
        let is_idle = memory_usage_percent < 40.0 && active_processes < 10;

        if is_idle {
            if self.idle_start.is_none() {
                self.idle_start = Some(Instant::now());
            }

            let idle_duration = self.idle_start
                .map(|start| start.elapsed())
                .unwrap_or(Duration::ZERO);

            idle_duration.as_secs() >= self.config.idle_threshold_secs
        } else {
            self.idle_start = None;
            false
        }
    }

    pub fn apply_idle_optimizations(&mut self) -> std::io::Result<()> {
        if self.is_optimizing.load(Ordering::SeqCst) {
            return Ok(());
        }

        self.is_optimizing.store(true, Ordering::SeqCst);
        tracing::info!("Applying idle optimizations (level: {:?})", self.config.optimization_level);

        match self.config.optimization_level {
            OptimizationLevel::Minimal => self.apply_minimal_optimizations(),
            OptimizationLevel::Normal => self.apply_normal_optimizations(),
            OptimizationLevel::Aggressive => self.apply_aggressive_optimizations(),
        }
    }

    fn apply_minimal_optimizations(&self) -> std::io::Result<()> {
        if Path::new("/sys/kernel/mm/ksm/run").exists() {
            fs::write("/sys/kernel/mm/ksm/run", "1")?;
        }

        if Path::new("/sys/kernel/mm/transparent_hugepage/enabled").exists() {
            fs::write("/sys/kernel/mm/transparent_hugepage/enabled", "[madvise]")?;
        }

        Ok(())
    }

    fn apply_normal_optimizations(&self) -> std::io::Result<()> {
        self.apply_minimal_optimizations()?;

        fs::write("/proc/sys/vm/swappiness", "90")?;
        fs::write("/proc/sys/vm/vfs_cache_pressure", "25")?;

        if Path::new("/sys/kernel/mm/ksm/pages_to_scan").exists() {
            fs::write("/sys/kernel/mm/ksm/pages_to_scan", "5000")?;
        }

        if self.config.preload_enabled {
            self.run_preload()?;
        }

        Ok(())
    }

    fn apply_aggressive_optimizations(&self) -> std::io::Result<()> {
        self.apply_normal_optimizations()?;

        let swap_used = self.get_swap_used_kb();
        if swap_used > 512 * 1024 {
            self.drop_caches(3)?;
        }

        fs::write("/proc/sys/vm/page-cluster", "3")?;

        Ok(())
    }

    fn get_swap_used_kb(&self) -> u64 {
        fs::read_to_string("/proc/meminfo")
            .ok()
            .and_then(|content| {
                let mut swap_total = 0u64;
                let mut swap_free = 0u64;
                for line in content.lines() {
                    let parts: Vec<&str> = line.split_whitespace().collect();
                    if parts.len() < 2 {
                        continue;
                    }
                    match parts[0] {
                        "SwapTotal:" => swap_total = parts[1].parse().unwrap_or(0),
                        "SwapFree:" => swap_free = parts[1].parse().unwrap_or(0),
                        _ => {}
                    }
                }
                Some(swap_total.saturating_sub(swap_free))
            })
            .unwrap_or(0)
    }

    fn drop_caches(&self, level: u32) -> std::io::Result<()> {
        if level > 3 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "Invalid drop_caches level",
            ));
        }
        tracing::info!("Dropping caches (level {})", level);
        fs::write("/proc/sys/vm/drop_caches", level.to_string())
    }

    fn run_preload(&self) -> std::io::Result<()> {
        if which("preload").is_some() {
            tracing::info!("Running preload");
            let _ = std::process::Command::new("preload")
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn();
        }
        Ok(())
    }

    pub fn cancel_within_100ms(&mut self) -> std::io::Result<()> {
        if !self.cancel_pending.load(Ordering::SeqCst) {
            self.cancel_pending.store(true, Ordering::SeqCst);
        }

        self.is_optimizing.store(false, Ordering::SeqCst);
        self.idle_start = None;

        tracing::info!("Cancelled idle optimizations");
        Ok(())
    }

    pub fn reset(&mut self) {
        self.is_optimizing.store(false, Ordering::SeqCst);
        self.cancel_pending.store(false, Ordering::SeqCst);
        self.idle_start = None;
    }
}

impl Default for IdleOptimizer {
    fn default() -> Self {
        Self::new()
    }
}

fn which(program: &str) -> Option<String> {
    std::env::var("PATH")
        .ok()
        .and_then(|paths| {
            paths.split(':').find_map(|dir| {
                let path = Path::new(dir).join(program);
                if path.exists() {
                    Some(path.to_string_lossy().to_string())
                } else {
                    None
                }
            })
        })
}