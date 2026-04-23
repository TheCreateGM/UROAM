use crate::config::Config;
use crate::classification::{Classifier, WorkloadType};
use crate::kernel::KernelInterface;
use crate::OptimizationMode;

pub struct OptimizationEngine {
    config: Config,
    classifier: Classifier,
    kernel: KernelInterface,
}

impl OptimizationEngine {
    pub fn new(config: Config) -> Self {
        Self {
            config,
            classifier: Classifier::new(),
            kernel: KernelInterface::new(),
        }
    }
    
    pub fn run_optimization_pass(&mut self) -> std::io::Result<()> {
        self.classifier.detect_system_info()?;
        self.classifier.scan_processes()?;
        
        let workload = self.classifier.get_current_workload();
        let is_idle = self.classifier.is_idle();
        
        if is_idle {
            self.apply_idle_optimizations()?;
        } else {
            match workload {
                WorkloadType::AiInference | WorkloadType::AiTraining => {
                    self.apply_ai_optimizations()?;
                }
                WorkloadType::Gaming => {
                    self.apply_gaming_optimizations()?;
                }
                WorkloadType::Compilation => {
                    self.apply_compilation_optimizations()?;
                }
                _ => {
                    self.apply_balanced_optimizations()?;
                }
            }
        }
        
        Ok(())
    }
    
    fn apply_ai_optimizations(&self) -> std::io::Result<()> {
        let profile = self.config.get_profile(OptimizationMode::Ai);
        
        self.kernel.set_swappiness(profile.swappiness)?;
        self.kernel.set_vfs_cache_pressure(profile.cache_pressure)?;
        
        if profile.ksm_enabled {
            self.kernel.enable_ksm(true)?;
            self.kernel.set_ksm_pages_to_scan(profile.ksm_pages_to_scan)?;
        }
        
        self.kernel.set_thp_mode(&profile.thp_mode)?;
        
        for pid in self.classifier.get_high_priority_pids() {
            let _ = set_oom_score_adj(pid, -900);
        }
        
        for pid in self.classifier.get_low_priority_pids() {
            let _ = set_oom_score_adj(pid, 300);
        }
        
        tracing::info!("Applied AI optimization profile");
        Ok(())
    }
    
    fn apply_gaming_optimizations(&self) -> std::io::Result<()> {
        self.kernel.set_swappiness(1)?;
        self.kernel.set_vfs_cache_pressure(50)?;
        self.kernel.set_page_cluster(0)?;
        self.kernel.set_min_free_kbytes(65536)?;
        
        self.kernel.enable_ksm(false)?;
        self.kernel.set_thp_mode("always")?;
        
        for pid in self.classifier.get_high_priority_pids() {
            let _ = set_oom_score_adj(pid, -900);
        }
        
        for pid in self.classifier.get_low_priority_pids() {
            let _ = set_oom_score_adj(pid, 500);
        }
        
        tracing::info!("Applied gaming optimization profile");
        Ok(())
    }
    
    fn apply_compilation_optimizations(&self) -> std::io::Result<()> {
        self.kernel.set_swappiness(20)?;
        self.kernel.set_vfs_cache_pressure(50)?;
        self.kernel.enable_ksm(true)?;
        self.kernel.set_thp_mode("always")?;
        
        tracing::info!("Applied compilation optimization profile");
        Ok(())
    }
    
    fn apply_balanced_optimizations(&self) -> std::io::Result<()> {
        let profile = self.config.get_profile(OptimizationMode::Balanced);
        
        self.kernel.set_swappiness(profile.swappiness)?;
        self.kernel.set_vfs_cache_pressure(profile.cache_pressure)?;
        
        if profile.ksm_enabled {
            self.kernel.enable_ksm(true)?;
            self.kernel.set_ksm_pages_to_scan(profile.ksm_pages_to_scan)?;
        } else {
            self.kernel.enable_ksm(false)?;
        }
        
        self.kernel.set_thp_mode(&profile.thp_mode)?;
        
        tracing::info!("Applied balanced optimization profile");
        Ok(())
    }
    
    fn apply_idle_optimizations(&self) -> std::io::Result<()> {
        self.kernel.set_swappiness(90)?;
        self.kernel.set_vfs_cache_pressure(25)?;
        
        self.kernel.enable_ksm(true)?;
        self.kernel.set_ksm_pages_to_scan(5000)?;
        
        if self.classifier.system_info.swap_used_kb > 512 * 1024 {
            self.kernel.drop_caches(3)?;
        }
        
        self.kernel.set_thp_mode("always")?;
        
        tracing::info!("Applied idle optimization profile");
        Ok(())
    }
}

fn set_oom_score_adj(pid: u32, adj: i32) -> std::io::Result<()> {
    let path = format!("/proc/{}/oom_score_adj", pid);
    std::fs::write(path, adj.to_string())
}

pub fn run_optimization_loop(
    config: &Config,
    classifier: &mut Classifier,
) -> std::io::Result<()> {
    let mut engine = OptimizationEngine::new(config.clone());
    
    loop {
        engine.run_optimization_pass()?;
        
        std::thread::sleep(std::time::Duration::from_millis(config.general.polling_interval_ms));
    }
}