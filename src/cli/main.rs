use anyhow::Result;
use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::fs;

use uroam_lib::config;

#[derive(Parser)]
#[command(name = "ramctl")]
#[command(about = "UROAM CLI Control Tool")]
struct Args {
    #[arg(short, long, default_value = "/run/uroam/uroamd.sock")]
    socket: PathBuf,
    
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    Status,
    Optimize,
    Profile {
        #[command(subcommand)]
        subcommand: ProfileCommands,
    },
    Clean,
    Monitor,
    Zram {
        #[command(subcommand)]
        subcommand: ZramCommands,
    },
    Config {
        #[command(subcommand)]
        subcommand: ConfigCommands,
    },
}

#[derive(Subcommand)]
enum ProfileCommands {
    Get,
    Set {
        name: String,
    },
}

#[derive(Subcommand)]
enum ZramCommands {
    Status,
    Resize {
        size: String,
    },
}

#[derive(Subcommand)]
enum ConfigCommands {
    Reload,
}

fn main() {
    let args = Args::parse();
    
    let exit_code = match args.command {
        Commands::Status => match cmd_status() {
            Ok(_) => 0,
            Err(e) => { eprintln!("Error: {}", e); 1 }
        },
        Commands::Optimize => match cmd_optimize() {
            Ok(_) => 0,
            Err(e) => { eprintln!("Error: {}", e); 1 }
        },
        Commands::Profile { subcommand } => match cmd_profile(subcommand) {
            Ok(_) => 0,
            Err(e) => { eprintln!("Error: {}", e); 1 }
        },
        Commands::Clean => match cmd_clean() {
            Ok(_) => 0,
            Err(e) => { eprintln!("Error: {}", e); 1 }
        },
        Commands::Monitor => match cmd_monitor() {
            Ok(_) => 0,
            Err(e) => { eprintln!("Error: {}", e); 1 }
        },
        Commands::Zram { subcommand } => match cmd_zram(subcommand) {
            Ok(_) => 0,
            Err(e) => { eprintln!("Error: {}", e); 1 }
        },
        Commands::Config { subcommand } => match cmd_config(subcommand) {
            Ok(_) => 0,
            Err(e) => { eprintln!("Error: {}", e); 1 }
        },
    };
    
    std::process::exit(exit_code);
}

fn cmd_status() -> Result<()> {
    println!("=== UROAM Status ===");
    println!();
    
    let meminfo = fs::read_to_string("/proc/meminfo")?;
    let mut total = 0u64;
    let mut available = 0u64;
    let mut swap_total = 0u64;
    let mut swap_free = 0u64;
    
    for line in meminfo.lines() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 2 { continue; }
        match parts[0] {
            "MemTotal:" => total = parts[1].parse().unwrap_or(0),
            "MemAvailable:" => available = parts[1].parse().unwrap_or(0),
            "SwapTotal:" => swap_total = parts[1].parse().unwrap_or(0),
            "SwapFree:" => swap_free = parts[1].parse().unwrap_or(0),
            _ => {}
        }
    }
    
    let used = total.saturating_sub(available);
    let percent = if total > 0 { (used as f64 / total as f64) * 100.0 } else { 0.0 };
    println!("Memory: {} / {} KB ({:.1}%)", used / 1024, total / 1024, percent);
    
    let swap_used = swap_total.saturating_sub(swap_free);
    let swap_pct = if swap_total > 0 { (swap_used as f64 / swap_total as f64) * 100.0 } else { 0.0 };
    println!("Swap: {} / {} KB ({:.1}%)", swap_used / 1024, swap_total / 1024, swap_pct);
    println!();
    
    let swappiness = fs::read_to_string("/proc/sys/vm/swappiness")
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|_| "unknown".to_string());
    println!("vm.swappiness: {}", swappiness);
    
    let ksm = fs::read_to_string("/sys/kernel/mm/ksm/run")
        .map(|s| if s.trim() == "1" { "enabled" } else { "disabled" }.to_string())
        .unwrap_or_else(|_| "N/A".to_string());
    println!("KSM: {}", ksm);
    
    Ok(())
}

fn cmd_optimize() -> Result<()> {
    println!("Triggering immediate optimization...");
    fs::write("/proc/sys/vm/drop_caches", "1")?;
    println!("Optimization triggered");
    Ok(())
}

fn cmd_profile(subcommand: ProfileCommands) -> Result<()> {
    match subcommand {
        ProfileCommands::Get => {
            println!("Current profile: balanced");
        }
        ProfileCommands::Set { name } => {
            println!("Setting profile to: {}", name);
        }
    }
    Ok(())
}

fn cmd_clean() -> Result<()> {
    println!("Performing aggressive memory cleanup...");
    fs::write("/proc/sys/vm/drop_caches", "3")?;
    println!("Cache drop complete");
    Ok(())
}

fn cmd_monitor() -> Result<()> {
    println!("Entering monitor mode. Press Ctrl+C to exit.");
    println!();
    println!("{:12} {:12} {:12}", "Memory", "Swap", "Tasks");
    println!("{:12} {:12} {:12}", "-------", "-----", "-----");
    
    loop {
        let meminfo = fs::read_to_string("/proc/meminfo")?;
        let mut total = 0u64;
        let mut available = 0u64;
        let mut swap_total = 0u64;
        let mut swap_free = 0u64;
        
        for line in meminfo.lines() {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() < 2 { continue; }
            match parts[0] {
                "MemTotal:" => total = parts[1].parse().unwrap_or(0),
                "MemAvailable:" => available = parts[1].parse().unwrap_or(0),
                "SwapTotal:" => swap_total = parts[1].parse().unwrap_or(0),
                "SwapFree:" => swap_free = parts[1].parse().unwrap_or(0),
                _ => {}
            }
        }
        
        let used = total.saturating_sub(available);
        let swap_used = swap_total.saturating_sub(swap_free);
        
        let tasks = fs::read_to_string("/proc/loadavg")
            .map(|s| {
                let parts: Vec<&str> = s.split_whitespace().collect();
                if parts.len() >= 4 {
                    parts[3].split('/').next().unwrap_or("0").to_string()
                } else {
                    "0".to_string()
                }
            })
            .unwrap_or_else(|_| "0".to_string());
        
        println!(
            "{:>10}% {:>10}% {:>12}",
            format!("{:.1}%", used as f64 / total as f64 * 100.0),
            format!("{:.1}%", swap_used as f64 / swap_total as f64 * 100.0),
            tasks
        );
        
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
}

fn cmd_zram(subcommand: ZramCommands) -> Result<()> {
    match subcommand {
        ZramCommands::Status => {
            let zram_paths = fs::read_dir("/sys/block")?
                .filter_map(|e| e.ok())
                .filter(|e| {
                    e.path().file_name()
                        .map(|n| n.to_string_lossy().starts_with("zram"))
                        .unwrap_or(false)
                });
            
            println!("=== ZRAM Devices ===");
            for entry in zram_paths {
                let name = entry.path().file_name().unwrap().to_string_lossy().to_string();
                let size = fs::read_to_string(format!("{}/disksize", entry.path().display()))
                    .map(|s| s.trim().to_string())
                    .unwrap_or_else(|_| "0".to_string());
                let algo = fs::read_to_string(format!("{}/comp_algorithm", entry.path().display()))
                    .map(|s| format!("[{}]", s.trim()))
                    .unwrap_or_else(|_| "[none]".to_string());
                
                println!("{}: {} {}", name, size, algo);
            }
        }
        ZramCommands::Resize { size: _ } => {
            println!("Resize not yet implemented");
        }
    }
    
    Ok(())
}

fn cmd_config(subcommand: ConfigCommands) -> Result<()> {
    match subcommand {
        ConfigCommands::Reload => {
            println!("Reloading configuration...");
        }
    }
    Ok(())
}