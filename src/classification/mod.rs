use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize, Default)]
pub enum WorkloadType {
    #[default]
    General,
    AiInference,
    AiTraining,
    Gaming,
    Compilation,
    Rendering,
    Interactive,
    Background,
    Idle,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProcessInfo {
    pub pid: u32,
    pub name: String,
    pub cmdline: Vec<String>,
    pub rss_kb: u64,
    pub swap_kb: u64,
    pub workload_type: WorkloadType,
    pub priority: Priority,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize, Default)]
pub enum Priority {
    #[default]
    P0,
    P1,
    P2,
    P3,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct SystemInfo {
    pub total_ram_kb: u64,
    pub available_ram_kb: u64,
    pub used_ram_kb: u64,
    pub memory_percent: f32,
    pub swap_total_kb: u64,
    pub swap_free_kb: u64,
    pub swap_used_kb: u64,
    pub swap_percent: f32,
    pub cpu_cores: u32,
    pub numa_nodes: u32,
    pub thp_enabled: bool,
    pub ksm_enabled: bool,
    pub zram_devices: u32,
    pub zswap_enabled: bool,
}

pub struct Classifier {
    pub system_info: SystemInfo,
    pub processes: Vec<ProcessInfo>,
    pub workload_counts: HashMap<WorkloadType, u32>,
    current_workload: WorkloadType,
    
    ai_processes: Vec<&'static str>,
    gaming_processes: Vec<&'static str>,
    compilation_processes: Vec<&'static str>,
    interactive_processes: Vec<&'static str>,
}

impl Classifier {
    pub fn new() -> Self {
        Self {
            system_info: SystemInfo::default(),
            processes: Vec::new(),
            workload_counts: HashMap::new(),
            current_workload: WorkloadType::Idle,
            
            ai_processes: vec![
                "llama.cpp", "llama-server", "ollama", "ollama-server",
                "python", "python3", "torch", "tensorflow", "transformers", "onnxruntime",
            ],
            gaming_processes: vec![
                "steam", "steamwebhelper", "steam-runtime",
                "proton", "wine", "wine64", "gamescope", "gamemoded", "lutris",
            ],
            compilation_processes: vec![
                "gcc", "g++", "clang", "clang++", "rustc", "cargo",
                "make", "ninja", "cmake", "meson",
            ],
            interactive_processes: vec![
                "gnome-shell", "kwin", "Xorg", "X11", "sway",
                "hyprland", "weston", "kitty", "alacritty", "foot",
                "firefox", "chromium", "vscode", "code",
            ],
        }
    }
    
    pub fn detect_system_info(&mut self) -> std::io::Result<()> {
        let meminfo = fs::read_to_string("/proc/meminfo")?;
        
        for line in meminfo.lines() {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() < 2 { continue; }
            match parts[0] {
                "MemTotal:" => {
                    self.system_info.total_ram_kb = parts[1].parse().unwrap_or(0);
                }
                "MemAvailable:" => {
                    self.system_info.available_ram_kb = parts[1].parse().unwrap_or(0);
                }
                "SwapTotal:" => {
                    self.system_info.swap_total_kb = parts[1].parse().unwrap_or(0);
                }
                "SwapFree:" => {
                    self.system_info.swap_free_kb = parts[1].parse().unwrap_or(0);
                }
                _ => {}
            }
        }
        
        self.system_info.used_ram_kb = self.system_info.total_ram_kb.saturating_sub(self.system_info.available_ram_kb);
        
        if self.system_info.total_ram_kb > 0 {
            self.system_info.memory_percent = 
                (self.system_info.used_ram_kb as f32 / self.system_info.total_ram_kb as f32) * 100.0;
        }
        
        self.system_info.swap_used_kb = self.system_info.swap_total_kb.saturating_sub(self.system_info.swap_free_kb);
        
        if self.system_info.swap_total_kb > 0 {
            self.system_info.swap_percent = 
                (self.system_info.swap_used_kb as f32 / self.system_info.swap_total_kb as f32) * 100.0;
        }
        
        let cpuinfo = fs::read_to_string("/proc/cpuinfo")
            .unwrap_or_default();
        let cores = cpuinfo.lines()
            .filter(|l| l.starts_with("processor"))
            .count() as u32;
        self.system_info.cpu_cores = cores.max(1);
        
        self.system_info.numa_nodes = 1;
        if fs::read_dir("/sys/bus/node/devices").is_ok() {
            if let Ok(entries) = fs::read_dir("/sys/bus/node/devices") {
                self.system_info.numa_nodes = entries.filter_map(|e| e.ok()).count() as u32;
            }
        }
        
        self.system_info.thp_enabled = std::path::Path::new("/sys/kernel/mm/transparent_hugepage/enabled").exists();
        self.system_info.ksm_enabled = std::path::Path::new("/sys/kernel/mm/ksm/run").exists();
        
        if let Ok(entries) = fs::read_dir("/sys/block") {
            self.system_info.zram_devices = entries
                .filter_map(|e| e.ok())
                .filter(|e| {
                    e.path().file_name()
                        .map(|n| n.to_string_lossy().starts_with("zram"))
                        .unwrap_or(false)
                })
                .count() as u32;
        } else {
            // If we can't read /sys/block, assume no zram devices
            self.system_info.zram_devices = 0;
        }
        
        Ok(())
    }
    
    pub fn scan_processes(&mut self) -> std::io::Result<()> {
        self.processes.clear();
        self.workload_counts.clear();
        
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
            
            let path = entry.path();
            
            let comm = fs::read_to_string(path.join("comm"))
                .unwrap_or_default()
                .trim()
                .to_string();
            
            let cmdline = fs::read_to_string(path.join("cmdline"))
                .unwrap_or_default()
                .replace("\0", " ")
                .trim()
                .to_string();
            
            let smaps = fs::read_to_string(path.join("smaps"))
                .unwrap_or_default();
            
            let mut rss_kb = 0u64;
            let mut swap_kb = 0u64;
            
            for line in smaps.lines() {
                if line.starts_with("Rss:") {
                    rss_kb = line.split_whitespace()
                        .nth(1)
                        .and_then(|s| s.parse().ok())
                        .unwrap_or(0);
                } else if line.starts_with("Swap:") {
                    swap_kb = line.split_whitespace()
                        .nth(1)
                        .and_then(|s| s.parse().ok())
                        .unwrap_or(0);
                }
            }
            
            let workload = self.classify_process(&comm, &cmdline);
            let priority = self.determine_priority(&workload);
            
            self.processes.push(ProcessInfo {
                pid,
                name: comm,
                cmdline: cmdline.split(' ').map(String::from).collect(),
                rss_kb,
                swap_kb,
                workload_type: workload,
                priority,
            });
            
            *self.workload_counts.entry(workload).or_insert(0) += 1;
        }
        
        Ok(())
    }
    
    pub fn classify_process(&self, name: &str, cmdline: &str) -> WorkloadType {
        let name_lower = name.to_lowercase();
        let cmdline_lower = cmdline.to_lowercase();
        
        for ai_proc in &self.ai_processes {
            if name_lower.contains(ai_proc) || cmdline_lower.contains(ai_proc) {
                if cmdline_lower.contains("train") || cmdline_lower.contains("fit") {
                    return WorkloadType::AiTraining;
                }
                return WorkloadType::AiInference;
            }
        }
        
        for game_proc in &self.gaming_processes {
            if name_lower.contains(game_proc) {
                return WorkloadType::Gaming;
            }
        }
        
        for comp_proc in &self.compilation_processes {
            if name_lower.contains(comp_proc) {
                return WorkloadType::Compilation;
            }
        }
        
        for inter_proc in &self.interactive_processes {
            if name_lower.contains(inter_proc) {
                return WorkloadType::Interactive;
            }
        }
        
        if name_lower.starts_with("kworker") || name_lower.contains("ksoftirqd") ||
           name_lower.contains("rcu_") || name_lower == "migration" {
            return WorkloadType::Background;
        }
        
        WorkloadType::General
    }
    
    fn determine_priority(&self, workload: &WorkloadType) -> Priority {
        match workload {
            WorkloadType::AiInference | WorkloadType::Interactive => Priority::P0,
            WorkloadType::Gaming | WorkloadType::Compilation => Priority::P1,
            WorkloadType::Background => Priority::P2,
            _ => Priority::P2,
        }
    }
    
    pub fn get_current_workload(&self) -> WorkloadType {
        let mut max_count = 0;
        let mut dominant = WorkloadType::General;
        
        for (workload, count) in &self.workload_counts {
            if *count > max_count {
                max_count = *count;
                dominant = *workload;
            }
        }
        
        if max_count == 0 {
            WorkloadType::Idle
        } else {
            dominant
        }
    }
    
    pub fn is_idle(&self) -> bool {
        self.system_info.memory_percent < 40.0 &&
        self.workload_counts.get(&WorkloadType::Gaming).copied().unwrap_or(0) == 0 &&
        self.workload_counts.get(&WorkloadType::AiInference).copied().unwrap_or(0) == 0 &&
        self.workload_counts.get(&WorkloadType::Interactive).copied().unwrap_or(0) == 0
    }
    
    pub fn get_high_priority_pids(&self) -> Vec<u32> {
        self.processes
            .iter()
            .filter(|p| matches!(p.priority, Priority::P0 | Priority::P1))
            .map(|p| p.pid)
            .collect()
    }
    
    pub fn get_low_priority_pids(&self) -> Vec<u32> {
        self.processes
            .iter()
            .filter(|p| matches!(p.priority, Priority::P2 | Priority::P3))
            .map(|p| p.pid)
            .collect()
    }
}