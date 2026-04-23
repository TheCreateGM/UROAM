pub mod config;
pub mod compression;
pub mod classification;
pub mod optimization;
pub mod kernel;
pub mod gaming;
pub mod idle;
pub mod ramdisk;

pub use config::{Config, ProfilesConfig, WorkloadProfile};
pub use classification::{WorkloadType, ProcessInfo, Classifier, Priority, SystemInfo};
pub use compression::{ZramManager, ZramDevice, ZswapManager, CompressionStats};
pub use kernel::{KernelInterface, MemoryPressure, read_pressure, read_memory_info, read_swap_info};
pub use gaming::GamingOptimizer;
pub use idle::{IdleOptimizer, IdleConfig, OptimizationLevel};
pub use ramdisk::{RamDiskManager, RamDiskStats, TmpfsConfig};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OptimizationMode {
    Ai,
    Gaming,
    Balanced,
    PowerSaver,
}

impl Default for OptimizationMode {
    fn default() -> Self {
        Self::Balanced
    }
}

#[derive(Debug, Clone)]
pub struct UroamState {
    pub enabled: bool,
    pub mode: OptimizationMode,
    pub active_workload: WorkloadType,
    pub compression: CompressionStats,
}

impl Default for UroamState {
    fn default() -> Self {
        Self {
            enabled: false,
            mode: OptimizationMode::Balanced,
            active_workload: WorkloadType::Idle,
            compression: CompressionStats::default(),
        }
    }
}