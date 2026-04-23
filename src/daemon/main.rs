use anyhow::Result;
use clap::Parser;

use uroam_lib::config;
use uroam_lib::classification;
use uroam_lib::kernel;
use uroam_lib::optimization;

#[derive(Parser)]
#[clap(name = "uroamd")]
#[clap(about = "UROAM - Unified RAM Optimization and Management Daemon")]
struct Args {
    #[clap(short, long, default_value = "/etc/uroam/uroam.conf")]
    config: std::path::PathBuf,
    
    #[clap(long)]
    daemon: bool,
}

fn main() {
    let args = Args::parse();
    
    let config = match config::Config::load(&args.config) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Failed to load config: {}", e);
            std::process::exit(1);
        }
    };
    
    if let Err(e) = run_daemon(&config) {
        eprintln!("Daemon error: {}", e);
        std::process::exit(1);
    }
}

fn run_daemon(config: &config::Config) -> Result<()> {
    eprintln!("Starting UROAM daemon v{}", env!("CARGO_PKG_VERSION"));
    
    let mut classifier = classification::Classifier::new();
    classifier.detect_system_info()?;
    
    eprintln!(
        "System: {} RAM, {} CPUs, {} NUMA nodes",
        classifier.system_info.total_ram_kb / 1024 / 1024,
        classifier.system_info.cpu_cores,
        classifier.system_info.numa_nodes
    );
    
    optimization::run_optimization_loop(config, &mut classifier)?;

    Ok(())
}