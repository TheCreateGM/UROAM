use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CompressionStats {
    pub zram_total_kb: u64,
    pub zram_used_kb: u64,
    pub zram_compressed_kb: u64,
    pub zram_ratio: f32,
    pub zswap_enabled: bool,
    pub zswap_pool_kb: u64,
}

pub struct ZramManager {
    num_devices: u32,
    default_algorithm: String,
}

impl ZramManager {
    pub fn new() -> Self {
        Self {
            num_devices: Self::detect_devices(),
            default_algorithm: "lz4".to_string(),
        }
    }
    
    fn detect_devices() -> u32 {
        if let Ok(entries) = std::fs::read_dir("/sys/block") {
            entries
                .filter_map(|e| e.ok())
                .filter(|e| {
                    e.path().file_name()
                        .map(|n| n.to_string_lossy().starts_with("zram"))
                        .unwrap_or(false)
                })
                .count() as u32
        } else {
            0
        }
    }
    
    pub fn get_devices(&self) -> Vec<ZramDevice> {
        (0..self.num_devices)
            .map(ZramDevice::new)
            .collect()
    }
    
    pub fn set_algorithm(&self, device: u32, algorithm: &str) -> std::io::Result<()> {
        let path = format!("/sys/block/zram{}/comp_algorithm", device);
        std::fs::write(path, algorithm)
    }
    
    pub fn set_size(&self, device: u32, size_kb: u64) -> std::io::Result<()> {
        let path = format!("/sys/block/zram{}/disksize", device);
        let size_bytes = size_kb * 1024;
        std::fs::write(path, size_bytes.to_string())
    }
    
    pub fn get_stats(&self, device: u32) -> std::io::Result<CompressionStats> {
        let dev = ZramDevice::new(device);
        Ok(CompressionStats {
            zram_total_kb: dev.size_kb(),
            zram_used_kb: dev.pages_used() * 4,
            zram_compressed_kb: dev.compr_size_kb(),
            zram_ratio: dev.compression_ratio(),
            zswap_enabled: false,
            zswap_pool_kb: 0,
        })
    }
}

pub struct ZramDevice {
    id: u32,
}

impl ZramDevice {
    pub fn new(id: u32) -> Self {
        Self { id }
    }
    
    fn path(&self, file: &str) -> String {
        format!("/sys/block/zram{}/{}", self.id, file)
    }
    
    pub fn size_kb(&self) -> u64 {
        std::fs::read_to_string(self.path("disksize"))
            .ok()
            .and_then(|s| s.trim().parse::<u64>().ok())
            .map(|b| b / 1024)
            .unwrap_or(0)
    }
    
    pub fn pages_used(&self) -> u64 {
        std::fs::read_to_string(self.path("mm_stat"))
            .ok()
            .and_then(|s| {
                let parts: Vec<&str> = s.trim().split_whitespace().collect();
                if parts.len() >= 4 {
                    parts[3].parse::<u64>().ok()
                } else {
                    None
                }
            })
            .unwrap_or(0)
    }
    
    pub fn compr_size_kb(&self) -> u64 {
        std::fs::read_to_string(self.path("mm_stat"))
            .ok()
            .and_then(|s| {
                let parts: Vec<&str> = s.trim().split_whitespace().collect();
                if !parts.is_empty() {
                    parts[0].parse::<u64>().ok()
                } else {
                    None
                }
            })
            .map(|b| b / 1024)
            .unwrap_or(0)
    }
    
    pub fn compression_ratio(&self) -> f32 {
        let used = self.pages_used() * 4;
        let compr = self.compr_size_kb();
        if compr > 0 {
            used as f32 / compr as f32
        } else {
            1.0
        }
    }
    
    pub fn reset(&self) -> std::io::Result<()> {
        std::fs::write(self.path("reset"), "1")
    }
}

pub struct ZswapManager {
    enabled: bool,
    max_pool_percent: u32,
}

impl ZswapManager {
    pub fn new() -> Self {
        Self {
            enabled: Self::detect_enabled(),
            max_pool_percent: 20,
        }
    }
    
    fn detect_enabled() -> bool {
        std::fs::read_to_string("/sys/module/zswap/parameters/enabled")
            .map(|s| s.trim() == "Y")
            .unwrap_or(false)
    }
    
    pub fn enable(&self) -> std::io::Result<()> {
        std::fs::write("/sys/module/zswap/parameters/enabled", "Y")
    }
    
    pub fn disable(&self) -> std::io::Result<()> {
        std::fs::write("/sys/module/zswap/parameters/enabled", "N")
    }
    
    pub fn set_pool_percent(&self, percent: u32) -> std::io::Result<()> {
        std::fs::write("/sys/module/zswap/parameters/max_pool_percent", percent.to_string())
    }
    
    pub fn set_compressor(&self, algo: &str) -> std::io::Result<()> {
        std::fs::write("/sys/module/zswap/parameters/compressor", algo)
    }
    
    pub fn get_pool_kb(&self) -> u64 {
        std::fs::read_to_string("/sys/module/zswap/parameters/max_pool_percent")
            .ok()
            .and_then(|s| s.trim().parse::<u64>().ok())
            .unwrap_or(0)
    }
}