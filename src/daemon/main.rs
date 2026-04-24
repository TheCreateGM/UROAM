use anyhow::Result;
use clap::Parser;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;

use uroam_lib::config;
use uroam_lib::classification;
use uroam_lib::kernel;
use uroam_lib::optimization;
use uroam_lib::ipc::IpcServer;

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
    
    let classifier = classification::Classifier::new();
    let classifier_mutex = Arc::new(Mutex::new(classifier));
    {
        let mut classifier_lock = classifier_mutex.lock().unwrap();
        classifier_lock.detect_system_info()?;
        
        eprintln!(
            "System: {} RAM, {} CPUs, {} NUMA nodes",
            classifier_lock.system_info.total_ram_kb / 1024 / 1024,
            classifier_lock.system_info.cpu_cores,
            classifier_lock.system_info.numa_nodes
        );
    }
    
    // Start IPC server
    let ipc_socket = &config.general.socket_path;
    let ipc_server = IpcServer::new(ipc_socket);
    ipc_server.start()?;
    eprintln!("IPC server started on {}", ipc_socket);
    
    // Run optimization loop in a separate thread so IPC server can handle requests
    let config_clone = config.clone();
    let classifier_mutex_clone = classifier_mutex.clone();
    
    let optimization_handle = thread::spawn(move || {
        let mut classifier_lock = classifier_mutex_clone.lock().unwrap();
        optimization::run_optimization_loop(&config_clone, &mut *classifier_lock)
    });
    
    // Wait for optimization thread to complete (it runs indefinitely)
    let _ = optimization_handle.join();
    
    Ok(())
}