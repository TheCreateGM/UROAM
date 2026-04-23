use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Config {
    #[serde(default)]
    pub general: GeneralConfig,
    
    #[serde(default)]
    pub zram: ZramConfig,
    
    #[serde(default)]
    pub zswap: ZswapConfig,
    
    #[serde(default)]
    pub profiles: ProfilesConfig,
    
    #[serde(default)]
    pub tmpfs: TmpfsConfig,
    
    #[serde(default)]
    pub idle: IdleConfig,
}

impl Config {
    pub fn load(path: &Path) -> Result<Self> {
        if !path.exists() {
            tracing::warn!("Config file not found: {:?}, using defaults", path);
            return Ok(Config::default());
        }
        
        let content = fs::read_to_string(path)
            .context("Failed to read config file")?;
        
        toml::from_str(&content)
            .context("Failed to parse config file")
    }
    
    pub fn validate(&self) -> Result<()> {
        if self.general.polling_interval_ms < 100 {
            anyhow::bail!("polling_interval must be at least 100ms");
        }
        
        if self.zram.size_percent > 100 {
            anyhow::bail!("zram.size_percent cannot exceed 100");
        }
        
        Ok(())
    }
    
    pub fn get_profile(&self, mode: crate::OptimizationMode) -> &WorkloadProfile {
        match mode {
            crate::OptimizationMode::Ai => &self.profiles.ai,
            crate::OptimizationMode::Gaming => &self.profiles.gaming,
            crate::OptimizationMode::Balanced => &self.profiles.balanced,
            crate::OptimizationMode::PowerSaver => &self.profiles.powersaver,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GeneralConfig {
    #[serde(default = "default_enabled")]
    pub enabled: bool,
    
    #[serde(default = "default_log_level")]
    pub log_level: String,
    
    #[serde(default)]
    pub log_file: String,
    
    #[serde(default)]
    pub socket_path: String,
    
    #[serde(default = "default_polling_interval")]
    pub polling_interval_ms: u64,
}

fn default_enabled() -> bool { true }
fn default_log_level() -> String { "info".to_string() }
fn default_polling_interval() -> u64 { 500 }

impl Default for GeneralConfig {
    fn default() -> Self {
        Self {
            enabled: default_enabled(),
            log_level: default_log_level(),
            log_file: "/var/log/uroam/uroamd.log".to_string(),
            socket_path: "/run/uroam/uroamd.sock".to_string(),
            polling_interval_ms: default_polling_interval(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ZramConfig {
    #[serde(default = "default_true")]
    pub enabled: bool,
    
    #[serde(default = "default_zram_size_percent")]
    pub size_percent: u32,
    
    #[serde(default)]
    pub num_devices: String,
    
    #[serde(default = "default_algorithm")]
    pub default_algorithm: String,
}

fn default_true() -> bool { true }
fn default_zram_size_percent() -> u32 { 50 }
fn default_algorithm() -> String { "lz4".to_string() }

impl Default for ZramConfig {
    fn default() -> Self {
        Self {
            enabled: default_true(),
            size_percent: default_zram_size_percent(),
            num_devices: "auto".to_string(),
            default_algorithm: default_algorithm(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ZswapConfig {
    #[serde(default = "default_true")]
    pub enabled: bool,
    
    #[serde(default = "default_zswap_size_percent")]
    pub max_pool_percent: u32,
}

fn default_zswap_size_percent() -> u32 { 20 }

impl Default for ZswapConfig {
    fn default() -> Self {
        Self {
            enabled: default_true(),
            max_pool_percent: default_zswap_size_percent(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProfilesConfig {
    #[serde(default)]
    pub ai: WorkloadProfile,
    
    #[serde(default)]
    pub gaming: WorkloadProfile,
    
    #[serde(default)]
    pub balanced: WorkloadProfile,
    
    #[serde(default)]
    pub powersaver: WorkloadProfile,
}

impl Default for ProfilesConfig {
    fn default() -> Self {
        Self {
            ai: WorkloadProfile::ai(),
            gaming: WorkloadProfile::gaming(),
            balanced: WorkloadProfile::balanced(),
            powersaver: WorkloadProfile::powersaver(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WorkloadProfile {
    #[serde(default = "default_swappiness")]
    pub swappiness: u32,
    
    #[serde(default)]
    pub zram_algorithm: String,
    
    #[serde(default = "default_zram_size_percent")]
    pub zram_size_percent: u32,
    
    #[serde(default = "default_true")]
    pub ksm_enabled: bool,
    
    #[serde(default = "default_ksm_pages")]
    pub ksm_pages_to_scan: u32,
    
    #[serde(default)]
    pub thp_mode: String,
    
    #[serde(default = "default_cache_pressure")]
    pub cache_pressure: u32,
    
    #[serde(default)]
    pub background_cgroup_limit: String,
}

fn default_swappiness() -> u32 { 60 }
fn default_ksm_pages() -> u32 { 1000 }
fn default_cache_pressure() -> u32 { 100 }

impl Default for WorkloadProfile {
    fn default() -> Self {
        Self {
            swappiness: default_swappiness(),
            zram_algorithm: default_algorithm(),
            zram_size_percent: default_zram_size_percent(),
            ksm_enabled: default_true(),
            ksm_pages_to_scan: default_ksm_pages(),
            thp_mode: "madvise".to_string(),
            cache_pressure: default_cache_pressure(),
            background_cgroup_limit: "4G".to_string(),
        }
    }
}

impl WorkloadProfile {
    pub fn ai() -> Self {
        Self {
            swappiness: 5,
            zram_algorithm: "zstd".to_string(),
            zram_size_percent: 50,
            ksm_enabled: true,
            ksm_pages_to_scan: 5000,
            thp_mode: "always".to_string(),
            cache_pressure: 100,
            background_cgroup_limit: "2G".to_string(),
        }
    }
    
    pub fn gaming() -> Self {
        Self {
            swappiness: 1,
            zram_algorithm: "lz4".to_string(),
            zram_size_percent: 30,
            ksm_enabled: false,
            ksm_pages_to_scan: 0,
            thp_mode: "always".to_string(),
            cache_pressure: 50,
            background_cgroup_limit: "4G".to_string(),
        }
    }
    
    pub fn balanced() -> Self {
        Self {
            swappiness: 60,
            zram_algorithm: "lz4".to_string(),
            zram_size_percent: 25,
            ksm_enabled: true,
            ksm_pages_to_scan: 1000,
            thp_mode: "madvise".to_string(),
            cache_pressure: 100,
            background_cgroup_limit: "4G".to_string(),
        }
    }
    
    pub fn powersaver() -> Self {
        Self {
            swappiness: 90,
            zram_algorithm: "zstd".to_string(),
            zram_size_percent: 60,
            ksm_enabled: true,
            ksm_pages_to_scan: 3000,
            thp_mode: "always".to_string(),
            cache_pressure: 25,
            background_cgroup_limit: "1G".to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TmpfsConfig {
    #[serde(default)]
    pub build_dir: String,
    
    #[serde(default)]
    pub build_size: String,
    
    #[serde(default)]
    pub ai_cache_dir: String,
    
    #[serde(default)]
    pub ai_cache_size: String,
}

impl Default for TmpfsConfig {
    fn default() -> Self {
        Self {
            build_dir: "/tmp/uroam-build".to_string(),
            build_size: "8G".to_string(),
            ai_cache_dir: "/tmp/uroam-ai-cache".to_string(),
            ai_cache_size: "16G".to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IdleConfig {
    #[serde(default = "default_idle_threshold")]
    pub idle_threshold_secs: u64,
    
    #[serde(default)]
    pub optimization_level: String,
    
    #[serde(default = "default_false")]
    pub preload_enabled: bool,
}

fn default_idle_threshold() -> u64 { 300 }
fn default_false() -> bool { false }

impl Default for IdleConfig {
    fn default() -> Self {
        Self {
            idle_threshold_secs: default_idle_threshold(),
            optimization_level: "normal".to_string(),
            preload_enabled: default_false(),
        }
    }
}