use std::os::unix::net::UnixListener;
use std::os::unix::io::AsRawFd;
use std::io::{Read, Write};
use std::thread;
use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum IpcCommand {
    Status,
    Optimize,
    ProfileGet,
    ProfileSet(String),
    Clean,
    MonitorStart,
    MonitorStop,
    ZramStatus,
    ZramResize(String),
    ConfigReload,
    Shutdown,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum IpcResponse {
    Ok(String),
    Error(String),
    Status{
        memory_total_kb: u64,
        memory_used_kb: u64,
        memory_available_kb: u64,
        swap_total_kb: u64,
        swap_used_kb: u64,
        swappiness: u32,
        zram_active: bool,
        profile: String,
        workload: String,
    },
    MonitorUpdate{
        timestamp_ms: u64,
        memory_percent: f32,
        swap_percent: f32,
        active_tasks: u32,
    },
}

pub struct IpcServer {
    socket_path: String,
    running: Arc<AtomicBool>,
}

impl IpcServer {
    pub fn new(socket_path: &str) -> Self {
        Self {
            socket_path: socket_path.to_string(),
            running: Arc::new(AtomicBool::new(false)),
        }
    }
    
    pub fn start(&self) -> std::io::Result<()> {
        let _ = std::fs::remove_file(&self.socket_path);
        
        let listener = UnixListener::bind(&self.socket_path)?;
        listener.set_nonblocking(false)?;
        
        self.running.store(true, Ordering::SeqCst);
        tracing::info!("IPC server listening on {}", self.socket_path);
        
        let running = self.running.clone();
        thread::spawn(move || {
            for stream in listener.incoming() {
                if !running.load(Ordering::SeqCst) {
                    break;
                }
                
                match stream {
                    Ok(mut stream) => {
                        let _ = handle_client(&mut stream);
                    }
                    Err(e) => {
                        tracing::error!("IPC accept error: {}", e);
                    }
                }
            }
            tracing::info!("IPC server stopped");
        });
        
        Ok(())
    }
    
    pub fn stop(&self) {
        self.running.store(false, Ordering::SeqCst);
        let _ = std::fs::remove_file(&self.socket_path);
    }
    
    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }
}

fn handle_client(stream: &mut std::os::unix::net::UnixStream) -> std::io::Result<()> {
    let mut buffer = [0u8; 4096];
    let n = stream.read(&mut buffer)?;
    
    if n == 0 {
        return Ok(());
    }
    
    let cmd_str = String::from_utf8_lossy(&buffer[..n]);
    let response = match serde_json::from_str::<IpcCommand>(&cmd_str) {
        Ok(cmd) => match cmd {
            IpcCommand::Status => {
                match get_status() {
                    Ok(status) => IpcResponse::Status{
                        memory_total_kb: status.0,
                        memory_used_kb: status.1,
                        memory_available_kb: status.2,
                        swap_total_kb: status.3,
                        swap_used_kb: status.4,
                        swappiness: status.5,
                        zram_active: status.6,
                        profile: status.7,
                        workload: status.8,
                    },
                    Err(e) => IpcResponse::Error(format!("Failed to get status: {}", e)),
                }
            }
            IpcCommand::Optimize => {
                match trigger_optimization() {
                    Ok(_) => IpcResponse::Ok("Optimization triggered".to_string()),
                    Err(e) => IpcResponse::Error(format!("Optimization failed: {}", e)),
                }
            }
            IpcCommand::ProfileGet => {
                match get_current_profile() {
                    Ok(profile) => IpcResponse::Ok(profile),
                    Err(e) => IpcResponse::Error(format!("Failed to get profile: {}", e)),
                }
            }
            IpcCommand::ProfileSet(name) => {
                match set_profile(&name) {
                    Ok(_) => IpcResponse::Ok(format!("Profile set to {}", name)),
                    Err(e) => IpcResponse::Error(format!("Failed to set profile: {}", e)),
                }
            }
            IpcCommand::Clean => {
                match clean_memory() {
                    Ok(_) => IpcResponse::Ok("Memory cleaned".to_string()),
                    Err(e) => IpcResponse::Error(format!("Clean failed: {}", e)),
                }
            }
            IpcCommand::ConfigReload => {
                match reload_config() {
                    Ok(_) => IpcResponse::Ok("Config reloaded".to_string()),
                    Err(e) => IpcResponse::Error(format!("Reload failed: {}", e)),
                }
            }
            IpcCommand::Shutdown => {
                tracing::info!("Shutdown requested via IPC");
                std::process::exit(0);
            }
            _ => IpcResponse::Error("Command not implemented".to_string()),
        },
        Err(e) => IpcResponse::Error(format!("Invalid command: {}", e)),
    };
    
    let response_str = serde_json::to_string(&response).unwrap_or_default();
    stream.write_all(response_str.as_bytes())?;
    stream.write_all(b"\n")?;
    
    Ok(())
}

fn get_status() -> std::io::Result<(u64, u64, u64, u64, u64, u32, bool, String, String)> {
    let content = std::fs::read_to_string("/proc/meminfo")?;
    let mut total_kb = 0u64;
    let mut available_kb = 0u64;
    let mut swap_total_kb = 0u64;
    let mut swap_free_kb = 0u64;
    
    for line in content.lines() {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 2 {
            continue;
        }
        match parts[0] {
            "MemTotal:" => total_kb = parts[1].parse().unwrap_or(0),
            "MemAvailable:" => available_kb = parts[1].parse().unwrap_or(0),
            "SwapTotal:" => swap_total_kb = parts[1].parse().unwrap_or(0),
            "SwapFree:" => swap_free_kb = parts[1].parse().unwrap_or(0),
            _ => {}
        }
    }
    
    let used_kb = total_kb.saturating_sub(available_kb);
    let swappiness = std::fs::read_to_string("/proc/sys/vm/swappiness")
        .ok()
        .and_then(|s| s.trim().parse().ok())
        .unwrap_or(0);
    
    let zram_active = std::path::Path::new("/sys/block/zram0").exists();
    
    Ok((total_kb, used_kb, available_kb, swap_total_kb, 
         swap_total_kb.saturating_sub(swap_free_kb), swappiness,
         zram_active, "balanced".to_string(), "idle".to_string()))
}

fn trigger_optimization() -> std::io::Result<()> {
    std::fs::write("/proc/sys/vm/drop_caches", "1")?;
    tracing::info!("Optimization triggered via IPC");
    Ok(())
}

fn get_current_profile() -> std::io::Result<String> {
    Ok("balanced".to_string())
}

fn set_profile(_name: &str) -> std::io::Result<()> {
    tracing::info!("Profile change requested");
    Ok(())
}

fn clean_memory() -> std::io::Result<()> {
    std::fs::write("/proc/sys/vm/drop_caches", "3")?;
    tracing::info!("Memory cleanup triggered via IPC");
    Ok(())
}

fn reload_config() -> std::io::Result<()> {
    tracing::info!("Config reload requested");
    Ok(())
}

pub struct IpcClient {
    socket_path: String,
}

impl IpcClient {
    pub fn new(socket_path: &str) -> Self {
        Self {
            socket_path: socket_path.to_string(),
        }
    }
    
    pub fn send_command(&self, cmd: &IpcCommand) -> std::io::Result<IpcResponse> {
        let mut stream = std::os::unix::net::UnixStream::connect(&self.socket_path)?;
        
        let cmd_str = serde_json::to_string(cmd).unwrap_or_default();
        stream.write_all(cmd_str.as_bytes())?;
        stream.write_all(b"\n")?;
        
        let mut response_str = String::new();
        stream.read_to_string(&mut response_str)?;
        
        serde_json::from_str(&response_str)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
    }
}
