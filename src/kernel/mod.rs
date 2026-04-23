use std::fs;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PressureLevel {
    #[default]
    None,
    Some,
    Full,
}

#[derive(Debug, Clone, Default)]
pub struct MemoryPressure {
    pub some_avg10: f64,
    pub some_avg60: f64,
    pub some_avg300: f64,
    pub full_avg10: f64,
    pub full_avg60: f64,
    pub full_avg300: f64,
}

pub fn read_pressure() -> std::io::Result<MemoryPressure> {
    let content = fs::read_to_string("/proc/pressure/memory")
        .unwrap_or_default();
    
    let mut pressure = MemoryPressure::default();
    
    for line in content.lines() {
        let line = line.trim();
        if line.starts_with("some") {
            let rest = &line[4..];
            let parts: Vec<&str> = rest.split_whitespace().collect();
            for part in parts {
                if part.starts_with("avg10=") {
                    pressure.some_avg10 = part.trim_start_matches("avg10=").parse().unwrap_or(0.0);
                } else if part.starts_with("avg60=") {
                    pressure.some_avg60 = part.trim_start_matches("avg60=").parse().unwrap_or(0.0);
                } else if part.starts_with("avg300=") {
                    pressure.some_avg300 = part.trim_start_matches("avg300=").parse().unwrap_or(0.0);
                }
            }
        } else if line.starts_with("full") {
            let rest = &line[4..];
            let parts: Vec<&str> = rest.split_whitespace().collect();
            for part in parts {
                if part.starts_with("avg10=") {
                    pressure.full_avg10 = part.trim_start_matches("avg10=").parse().unwrap_or(0.0);
                } else if part.starts_with("avg60=") {
                    pressure.full_avg60 = part.trim_start_matches("avg60=").parse().unwrap_or(0.0);
                } else if part.starts_with("avg300=") {
                    pressure.full_avg300 = part.trim_start_matches("avg300=").parse().unwrap_or(0.0);
                }
            }
        }
    }
    
    Ok(pressure)
}

pub fn read_memory_info() -> std::io::Result<MemoryInfo> {
    let content = fs::read_to_string("/proc/meminfo")?;
    let mut info = MemoryInfo::default();
    
    for line in content.lines() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 2 { continue; }
        
        match parts[0] {
            "MemTotal:" => info.total_kb = parts[1].parse().unwrap_or(0),
            "MemAvailable:" => info.available_kb = parts[1].parse().unwrap_or(0),
            "MemFree:" => info.free_kb = parts[1].parse().unwrap_or(0),
            "Buffers:" => info.buffers_kb = parts[1].parse().unwrap_or(0),
            "Cached:" => info.cached_kb = parts[1].parse().unwrap_or(0),
            "SwapTotal:" => info.swap_total_kb = parts[1].parse().unwrap_or(0),
            "SwapFree:" => info.swap_free_kb = parts[1].parse().unwrap_or(0),
            "SwapCached:" => info.swap_cached_kb = parts[1].parse().unwrap_or(0),
            "Dirty:" => info.dirty_kb = parts[1].parse().unwrap_or(0),
            "Writeback:" => info.writeback_kb = parts[1].parse().unwrap_or(0),
            _ => {}
        }
    }
    
    info.used_kb = info.total_kb.saturating_sub(info.available_kb);
    info.swap_used_kb = info.swap_total_kb.saturating_sub(info.swap_free_kb);
    
    Ok(info)
}

pub fn read_swap_info() -> std::io::Result<SwapInfo> {
    let content = fs::read_to_string("/proc/vmstat")?;
    let mut info = SwapInfo::default();
    
    for line in content.lines() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 2 { continue; }
        
        match parts[0] {
            "pswpin" => info.pages_swapped_in = parts[1].parse().unwrap_or(0),
            "pswpout" => info.pages_swapped_out = parts[1].parse().unwrap_or(0),
            "pgpgin" => info.pages_paged_in = parts[1].parse().unwrap_or(0),
            "pgpgout" => info.pages_paged_out = parts[1].parse().unwrap_or(0),
            "pgmajfault" => info.major_faults = parts[1].parse().unwrap_or(0),
            _ => {}
        }
    }
    
    Ok(info)
}

pub fn write_sysctl(path: &str, value: &str) -> std::io::Result<()> {
    fs::write(path, value)
}

pub fn read_sysctl(path: &str) -> std::io::Result<String> {
    fs::read_to_string(path)
        .map(|s| s.trim().to_string())
}

#[derive(Debug, Clone, Default)]
pub struct MemoryInfo {
    pub total_kb: u64,
    pub available_kb: u64,
    pub free_kb: u64,
    pub used_kb: u64,
    pub buffers_kb: u64,
    pub cached_kb: u64,
    pub swap_total_kb: u64,
    pub swap_free_kb: u64,
    pub swap_used_kb: u64,
    pub swap_cached_kb: u64,
    pub dirty_kb: u64,
    pub writeback_kb: u64,
}

#[derive(Debug, Clone, Default)]
pub struct SwapInfo {
    pub pages_swapped_in: u64,
    pub pages_swapped_out: u64,
    pub pages_paged_in: u64,
    pub pages_paged_out: u64,
    pub major_faults: u64,
}

pub struct KernelInterface;

impl KernelInterface {
    pub fn new() -> Self {
        Self
    }
    
    pub fn set_swappiness(&self, value: u32) -> std::io::Result<()> {
        write_sysctl("/proc/sys/vm/swappiness", &value.to_string())
    }
    
    pub fn get_swappiness(&self) -> std::io::Result<u32> {
        read_sysctl("/proc/sys/vm/swappiness")?
            .parse()
            .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidData, "Invalid swappiness"))
    }
    
    pub fn set_vfs_cache_pressure(&self, value: u32) -> std::io::Result<()> {
        write_sysctl("/proc/sys/vm/vfs_cache_pressure", &value.to_string())
    }
    
    pub fn get_vfs_cache_pressure(&self) -> std::io::Result<u32> {
        read_sysctl("/proc/sys/vm/vfs_cache_pressure")?
            .parse()
            .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidData, "Invalid vfs_cache_pressure"))
    }
    
    pub fn drop_caches(&self, level: u32) -> std::io::Result<()> {
        if level > 3 {
            return Err(std::io::Error::new(std::io::ErrorKind::InvalidInput, "Invalid drop_caches level"));
        }
        write_sysctl("/proc/sys/vm/drop_caches", &level.to_string())
    }
    
    pub fn set_page_cluster(&self, value: u32) -> std::io::Result<()> {
        write_sysctl("/proc/sys/vm/page-cluster", &value.to_string())
    }
    
    pub fn set_min_free_kbytes(&self, value: u32) -> std::io::Result<()> {
        write_sysctl("/proc/sys/vm/min_free_kbytes", &value.to_string())
    }
    
    pub fn enable_ksm(&self, enable: bool) -> std::io::Result<()> {
        write_sysctl("/sys/kernel/mm/ksm/run", if enable { "1" } else { "0" })
    }
    
    pub fn set_ksm_pages_to_scan(&self, pages: u32) -> std::io::Result<()> {
        write_sysctl("/sys/kernel/mm/ksm/pages_to_scan", &pages.to_string())
    }
    
    pub fn set_thp_mode(&self, mode: &str) -> std::io::Result<()> {
        write_sysctl("/sys/kernel/mm/transparent_hugepage/enabled", mode)?;
        write_sysctl("/sys/kernel/mm/transparent_hugepage/defrag", mode)
    }
}