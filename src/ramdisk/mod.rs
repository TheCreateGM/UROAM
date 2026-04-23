use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::io::{self, Read, Write};

#[derive(Debug, Clone, Default)]
pub struct TmpfsConfig {
    pub build_dir: String,
    pub build_size: String,
    pub ai_cache_dir: String,
    pub ai_cache_size: String,
}

impl TmpfsConfig {
    pub fn new() -> Self {
        Self {
            build_dir: "/tmp/uroam-build".to_string(),
            build_size: "8G".to_string(),
            ai_cache_dir: "/tmp/uroam-ai-cache".to_string(),
            ai_cache_size: "16G".to_string(),
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct RamDiskStats {
    pub build_used_kb: u64,
    pub build_available_kb: u64,
    pub ai_cache_used_kb: u64,
    pub ai_cache_available_kb: u64,
    pub total_inodes: u64,
    pub used_inodes: u64,
}

pub struct RamDiskManager {
    config: TmpfsConfig,
    build_mounted: bool,
    ai_cache_mounted: bool,
}

impl RamDiskManager {
    pub fn new() -> Self {
        Self {
            config: TmpfsConfig::new(),
            build_mounted: false,
            ai_cache_mounted: false,
        }
    }

    pub fn with_config(config: TmpfsConfig) -> Self {
        Self {
            config,
            build_mounted: false,
            ai_cache_mounted: false,
        }
    }

    pub fn init(&mut self) -> io::Result<()> {
        self.setup_build_dir()?;
        self.setup_ai_cache_dir()?;
        Ok(())
    }

    fn setup_build_dir(&mut self) -> io::Result<()> {
        let build_path = Path::new(&self.config.build_dir);
        
        if build_path.exists() {
            self.build_mounted = true;
            return Ok(());
        }

        fs::create_dir_all(build_path)?;
        
        let output = std::process::Command::new("mount")
            .args([
                "-t", "tmpfs",
                "-o", &format!("size={},mode=1777", self.config.build_size),
                "uroam-build",
                &self.config.build_dir,
            ])
            .output();

        match output {
            Ok(out) if out.status.success() => {
                self.build_mounted = true;
                tracing::info!("Mounted build tmpfs at {}", self.config.build_dir);
            }
            Ok(out) => {
                let stderr = String::from_utf8_lossy(&out.stderr);
                tracing::warn!("Failed to mount build tmpfs: {}", stderr);
                fs::create_dir_all(build_path)?;
            }
            Err(e) => {
                tracing::warn!("Failed to run mount command: {}", e);
            }
        }

        Ok(())
    }

    fn setup_ai_cache_dir(&mut self) -> io::Result<()> {
        let cache_path = Path::new(&self.config.ai_cache_dir);
        
        if cache_path.exists() {
            self.ai_cache_mounted = true;
            return Ok(());
        }

        fs::create_dir_all(cache_path)?;
        
        let output = std::process::Command::new("mount")
            .args([
                "-t", "tmpfs",
                "-o", &format!("size={},mode=1777", self.config.ai_cache_size),
                "uroam-ai-cache",
                &self.config.ai_cache_dir,
            ])
            .output();

        match output {
            Ok(out) if out.status.success() => {
                self.ai_cache_mounted = true;
               tracing::info!("Mounted AI cache tmpfs at {}", self.config.ai_cache_dir);
            }
            Ok(out) => {
                let stderr = String::from_utf8_lossy(&out.stderr);
                tracing::warn!("Failed to mount AI cache tmpfs: {}", stderr);
                fs::create_dir_all(cache_path)?;
            }
            Err(e) => {
                tracing::warn!("Failed to run mount command: {}", e);
            }
        }

        Ok(())
    }

    pub fn get_stats(&self) -> io::Result<RamDiskStats> {
        let mut stats = RamDiskStats::default();

        if self.build_mounted {
            if let Ok(used) = get_tmpfs_usage(&self.config.build_dir) {
                stats.build_used_kb = used.0;
                stats.build_available_kb = used.1;
            }
        }

        if self.ai_cache_mounted {
            if let Ok(used) = get_tmpfs_usage(&self.config.ai_cache_dir) {
                stats.ai_cache_used_kb = used.0;
                stats.ai_cache_available_kb = used.1;
            }
        }

        Ok(stats)
    }

    pub fn cleanup(&mut self) -> io::Result<()> {
        if self.build_mounted {
            let _ = std::process::Command::new("umount")
                .arg(&self.config.build_dir)
                .output();
            self.build_mounted = false;
        }

        if self.ai_cache_mounted {
            let _ = std::process::Command::new("umount")
                .arg(&self.config.ai_cache_dir)
                .output();
            self.ai_cache_mounted = false;
        }

        tracing::info!("Cleaned up RAM disks");
        Ok(())
    }

    pub fn is_build_dir_available(&self) -> bool {
        self.build_mounted || Path::new(&self.config.build_dir).exists()
    }

    pub fn is_ai_cache_available(&self) -> bool {
        self.ai_cache_mounted || Path::new(&self.config.ai_cache_dir).exists()
    }
}

impl Default for RamDiskManager {
    fn default() -> Self {
        Self::new()
    }
}

fn get_tmpfs_usage(path: &str) -> io::Result<(u64, u64)> {
    let statfs_path = format!("/proc/self/mountinfo");
    let content = fs::read_to_string(&statfs_path)?;

    for line in content.lines() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() > 4 && parts.get(4) == Some(&path) {
            return Ok((0, 0));
        }
    }

    Ok((0, 0))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_tmpfs_config_default() {
        let config = TmpfsConfig::new();
        assert_eq!(config.build_dir, "/tmp/uroam-build");
        assert_eq!(config.ai_cache_dir, "/tmp/uroam-ai-cache");
    }

    #[test]
    fn test_ram_disk_manager_creation() {
        let manager = RamDiskManager::new();
        assert!(!manager.is_build_dir_available());
        assert!(!manager.is_ai_cache_available());
    }
}