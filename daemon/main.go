package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/shirou/gopsutil/v3/mem"
	"github.com/shirou/gopsutil/v3/process"
	"github.com/spf13/cobra"
)

var (
	rootCmd = &cobra.Command{
		Use:   "memopt-daemon",
		Short: "Memory optimization daemon for AI workloads",
		Run:   runDaemon,
	}

	interval   time.Duration
	aggressive bool
)

func init() {
	rootCmd.PersistentFlags().DurationVarP(&interval, "interval", "i", 5*time.Second, "Monitoring interval")
	rootCmd.PersistentFlags().BoolVarP(&aggressive, "aggressive", "a", false, "Enable aggressive optimization")
}

type PSIStats struct {
	Some10  float64
	Some60  float64
	Some300 float64
	Full10  float64
	Full60  float64
	Full300 float64
}

type SystemState struct {
	Memory    *mem.VirtualMemoryStat
	Processes []*ProcessInfo
	SwapUsed  uint64
	PSI       PSIStats
}

type ProcessInfo struct {
	PID          int32
	Name         string
	RSS          uint64
	Swap         uint64
	WorkloadType WorkloadType
	Priority     int // P0=0, P1=1, P2=2
}

type WorkloadType int

const (
	WorkloadGeneral WorkloadType = iota
	WorkloadAIInference
	WorkloadAITraining
	WorkloadGaming
	WorkloadBackground
)

const (
	PriorityP0 = 0 // Highest - AI inference, interactive
	PriorityP1 = 1 // Gaming, interactive apps
	PriorityP2 = 2 // Default - background
)

var isIdle bool

func classifyProcess(p *process.Process) (WorkloadType, int) {
	name, err := p.Name()
	if err != nil {
		return WorkloadGeneral, PriorityP2
	}

	switch name {
	case "python", "python3", "python3.10", "python3.11", "python3.12":
		cmdline, err := p.Cmdline()
		if err == nil {
			if containsAny(cmdline, "torch", "tensorflow", "llama", "transformers", "inference", "ollama", "llamacpp", "llama.cpp", "llm") {
				return WorkloadAIInference, PriorityP0
			}
			if containsAny(cmdline, "train", "fit", "epoch", "fine-tune", "finetune", "training") {
				return WorkloadAITraining, PriorityP1
			}
		}
	case "steam", "steamwebhelper", "proton", "wine", "wine64", "winedevice.exe", "wine64-preloader", "gamescope":
		return WorkloadGaming, PriorityP1
	case "gnome-shell", "kwin", "Xorg", "X11", "sway", "hyprland", "weston", "compton", "picom":
		return WorkloadBackground, PriorityP0
	case "blender", "gimp", "kdenlive", "obs", "ffmpeg", "handbrake":
		return WorkloadGeneral, PriorityP1
	case "systemd", "systemd-journal", "dbus-daemon", "NetworkManager", "polkitd", "udevd":
		return WorkloadBackground, PriorityP1
	}

	// Check for game processes by name patterns
	if containsAny(name, "exe", "x86_64", "linux64", "game") && !containsAny(name, "steam") {
		return WorkloadGaming, PriorityP1
	}

	return WorkloadGeneral, PriorityP2
}

func containsAny(s string, substrs ...string) bool {
	for _, substr := range substrs {
		if len(substr) > len(s) {
			continue
		}
		for i := 0; i <= len(s)-len(substr); i++ {
			if s[i:i+len(substr)] == substr {
				return true
			}
		}
	}
	return false
}

func readPSI() (PSIStats, error) {
	var psi PSIStats
	data, err := os.ReadFile("/proc/pressure/memory")
	if err != nil {
		return psi, err
	}

	lines := strings.Split(string(data), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "some") {
			var ignore string
			fmt.Sscanf(line, "some avg10=%f avg60=%f avg300=%f %s",
				&psi.Some10, &psi.Some60, &psi.Some300, &ignore)
		} else if strings.HasPrefix(line, "full") {
			var ignore string
			fmt.Sscanf(line, "full avg10=%f avg60=%f avg300=%f %s",
				&psi.Full10, &psi.Full60, &psi.Full300, &ignore)
		}
	}
	return psi, nil
}

func collectSystemState() (*SystemState, error) {
	vmem, err := mem.VirtualMemory()
	if err != nil {
		return nil, err
	}

	swap, err := mem.SwapMemory()
	if err != nil {
		swap = &mem.SwapMemoryStat{}
	}

	procs, err := process.Processes()
	if err != nil {
		return nil, err
	}

	psi, _ := readPSI()

	var processInfos []*ProcessInfo
	for _, p := range procs {
		memInfo, err := p.MemoryInfo()
		if err != nil {
			continue
		}

		name, err := p.Name()
		if err != nil {
			continue
		}

		wlType, priority := classifyProcess(p)

		processInfos = append(processInfos, &ProcessInfo{
			PID:          p.Pid,
			Name:         name,
			RSS:          memInfo.RSS,
			Swap:         memInfo.Swap,
			WorkloadType: wlType,
			Priority:     priority,
		})
	}

	return &SystemState{
		Memory:    vmem,
		Processes: processInfos,
		SwapUsed:  swap.Used,
		PSI:       psi,
	}, nil
}

func writeSysfs(path, value string) error {
	f, err := os.OpenFile(path, os.O_WRONLY, 0644)
	if err != nil {
		log.Printf("Warning: Failed to open sysfs path %s: %v", path, err)
		return err
	}
	_, err = f.WriteString(value)
	if err != nil {
		log.Printf("Warning: Failed to write to sysfs path %s: %v", path, err)
	}
	f.Close()
	return err
}

func detectIdle(state *SystemState) bool {
	// Check for interactive processes (priority 0 and 1)
	interactiveCount := 0

	for _, p := range state.Processes {
		if p.Priority <= PriorityP1 {
			interactiveCount++
		}
	}

	// System is idle if:
	// 1. No interactive processes running
	// 2. Memory usage below threshold
	// 3. Low pressure stall information
	return interactiveCount == 0 &&
		state.Memory.UsedPercent < 40 &&
		state.PSI.Full10 < 1.0 &&
		state.PSI.Some10 < 5.0
}

func applySmartSwapPrioritization(state *SystemState) {
	var lowPriSwap, highPriSwap uint64
	highPriTotal := uint64(0)
	lowPriTotal := uint64(0)

	for _, p := range state.Processes {
		if p.Priority >= PriorityP2 {
			lowPriSwap += p.Swap
			lowPriTotal += p.RSS + p.Swap
		} else {
			highPriSwap += p.Swap
			highPriTotal += p.RSS + p.Swap
		}
	}

	// Calculate swap ratios
	highPriSwapRatio := float64(0)
	lowPriSwapRatio := float64(0)

	if highPriTotal > 0 {
		highPriSwapRatio = float64(highPriSwap) / float64(highPriTotal)
	}
	if lowPriTotal > 0 {
		lowPriSwapRatio = float64(lowPriSwap) / float64(lowPriTotal)
	}

	// If high priority processes are being swapped too much, reduce swappiness
	if highPriSwapRatio > 0.15 {
		writeSysfs("/proc/sys/vm/swappiness", "5")
		writeSysfs("/proc/sys/vm/page-cluster", "0")
	} else if lowPriSwapRatio < 0.3 && lowPriSwap > 1024*1024*500 {
		// Low priority processes have low swap usage, encourage more swapping
		writeSysfs("/proc/sys/vm/swappiness", "80")
		writeSysfs("/proc/sys/vm/page-cluster", "3")
	} else if state.Memory.UsedPercent > 80 {
		writeSysfs("/proc/sys/vm/swappiness", "20")
		writeSysfs("/proc/sys/vm/page-cluster", "1")
	} else {
		writeSysfs("/proc/sys/vm/swappiness", "40")
		writeSysfs("/proc/sys/vm/page-cluster", "2")
	}

	// Protect high priority processes from OOM killer
	protectHighPriorityProcesses(state)
}

func protectHighPriorityProcesses(state *SystemState) {
	for _, p := range state.Processes {
		if p.Priority <= PriorityP1 {
			// Adjust OOM score adj for important processes
			path := fmt.Sprintf("/proc/%d/oom_score_adj", p.PID)
			if p.Priority == PriorityP0 {
				writeSysfs(path, "-900") // Almost never kill
			} else if p.Priority == PriorityP1 {
				writeSysfs(path, "-500") // Very hard to kill
			}
		} else if p.Priority >= PriorityP2 {
			// Background processes can be killed more easily
			path := fmt.Sprintf("/proc/%d/oom_score_adj", p.PID)
			writeSysfs(path, "300")
		}
	}
}

func applyIdleOptimization(state *SystemState) {
	if isIdle {
		// Aggressive optimization during idle
		writeSysfs("/proc/sys/vm/swappiness", "90")
		writeSysfs("/proc/sys/vm/vfs_cache_pressure", "50")
		writeSysfs("/sys/kernel/mm/ksm/run", "1")
		writeSysfs("/sys/kernel/mm/ksm/pages_to_scan", "5000")
		writeSysfs("/sys/kernel/mm/ksm/sleep_millisecs", "5")

		// Enable zRAM compression aggressively
		writeSysfs("/sys/module/zswap/parameters/max_pool_percent", "40")
		writeSysfs("/sys/module/zswap/parameters/enabled", "Y")

		// Use zstd for better compression during idle
		writeSysfs("/sys/module/zswap/parameters/compressor", "zstd")

		// Drop caches if we have too much swap usage
		state.SwapUsed = 0
		for _, p := range state.Processes {
			if p.Priority == PriorityP2 && p.Swap > 0 {
				state.SwapUsed += p.Swap
			}
		}
		if state.SwapUsed > 1024*1024*500 {
			writeSysfs("/proc/sys/vm/drop_caches", "3")
		}

		// Enable THP defrag during idle
		writeSysfs("/sys/kernel/mm/transparent_hugepage/defrag", "always")
	} else {
		writeSysfs("/proc/sys/vm/vfs_cache_pressure", "100")
		// Restore zswap settings to default
		writeSysfs("/sys/module/zswap/parameters/compressor", "lz4")
	}
}

func applyOptimizations(state *SystemState) {
	wasIdle := isIdle
	isIdle = detectIdle(state)

	if isIdle && !wasIdle {
		log.Println("Entering idle mode - aggressive memory optimization")
	} else if !isIdle && wasIdle {
		log.Println("Exiting idle mode - restoring normal operation")
	}

	if state.PSI.Full10 > 10.0 || state.Memory.UsedPercent > 85 {
		writeSysfs("/proc/sys/vm/swappiness", "5")
		writeSysfs("/sys/kernel/mm/ksm/run", "1")
		writeSysfs("/sys/kernel/mm/ksm/pages_to_scan", "3000")
	} else if state.PSI.Some10 > 5.0 || state.Memory.UsedPercent > 70 {
		writeSysfs("/proc/sys/vm/swappiness", "20")
		writeSysfs("/sys/kernel/mm/ksm/run", "1")
	} else {
		if !isIdle {
			writeSysfs("/proc/sys/vm/swappiness", "60")
		}
		writeSysfs("/sys/kernel/mm/ksm/run", "0")
	}

	var hasAI, hasGaming bool
	for _, p := range state.Processes {
		switch p.WorkloadType {
		case WorkloadAIInference, WorkloadAITraining:
			hasAI = true
		case WorkloadGaming:
			hasGaming = true
		}
	}

	if hasAI || hasGaming {
		writeSysfs("/sys/module/zswap/parameters/max_pool_percent", "30")
	} else {
		if !isIdle {
			writeSysfs("/sys/module/zswap/parameters/max_pool_percent", "20")
		}
	}

	if hasGaming {
		applyGamingOptimizations(state)
	}

	if hasAI {
		applyAIOptimizations(state)
	}

	applyCacheOptimizations(state)
	applySmartSwapPrioritization(state)
	applyIdleOptimization(state)
}

func applyGamingOptimizations(state *SystemState) {
	// Gaming specific optimizations
	writeSysfs("/proc/sys/vm/swappiness", "10")
	writeSysfs("/proc/sys/vm/dirty_ratio", "5")
	writeSysfs("/proc/sys/vm/dirty_background_ratio", "2")
	writeSysfs("/proc/sys/vm/vfs_cache_pressure", "50")
	writeSysfs("/sys/kernel/mm/transparent_hugepage/enabled", "always")
	writeSysfs("/sys/kernel/mm/transparent_hugepage/defrag", "always")

	// Disable KSM during gaming - causes stutters
	writeSysfs("/sys/kernel/mm/ksm/run", "0")

	// Optimize page cluster for low latency
	writeSysfs("/proc/sys/vm/page-cluster", "0")

	// Increase min free kbytes for responsiveness
	writeSysfs("/proc/sys/vm/min_free_kbytes", "65536")
}

func applyAIOptimizations(state *SystemState) {
	// AI workload optimizations
	writeSysfs("/sys/kernel/mm/transparent_hugepage/enabled", "always")
	writeSysfs("/sys/kernel/mm/transparent_hugepage/defrag", "defer+madvise")

	// Enable KSM for deduplication of model weights
	writeSysfs("/sys/kernel/mm/ksm/run", "1")
	writeSysfs("/sys/kernel/mm/ksm/pages_to_scan", "3000")
	writeSysfs("/sys/kernel/mm/ksm/sleep_millisecs", "10")

	// Adjust page cluster for better throughput
	writeSysfs("/proc/sys/vm/page-cluster", "3")

	// Allow more dirty pages for batch processing
	writeSysfs("/proc/sys/vm/dirty_ratio", "30")
	writeSysfs("/proc/sys/vm/dirty_background_ratio", "10")

	// Optimize cache pressure
	writeSysfs("/proc/sys/vm/vfs_cache_pressure", "100")
}

func applyCacheOptimizations(state *SystemState) {
	// Dynamic cache management based on system pressure
	if state.Memory.UsedPercent > 90 {
		// Critical memory pressure - drop all non-essential caches
		writeSysfs("/proc/sys/vm/drop_caches", "3")
	} else if state.Memory.UsedPercent > 80 && state.PSI.Full10 > 5.0 {
		// High pressure - drop dentries and inodes
		writeSysfs("/proc/sys/vm/drop_caches", "2")
	} else if state.Memory.UsedPercent < 50 && isIdle {
		// Low usage - allow more caching
		writeSysfs("/proc/sys/vm/vfs_cache_pressure", "25")
	}
}

func runDaemon(cmd *cobra.Command, args []string) {
	log.Println("Starting memopt daemon...")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	// Initial optimizations
	writeSysfs("/sys/kernel/mm/transparent_hugepage/enabled", "always")
	writeSysfs("/sys/kernel/mm/ksm/sleep_millisecs", "10")

	log.Println("Daemon started successfully")

	for {
		select {
		case <-sigChan:
			log.Println("Received shutdown signal")
			return
		case <-ticker.C:
			state, err := collectSystemState()
			if err != nil {
				log.Printf("Failed to collect state: %v", err)
				continue
			}

			applyOptimizations(state)

			log.Printf("Memory usage: %.1f%% | PSI full10: %.1f%% | Processes: %d",
				state.Memory.UsedPercent, state.PSI.Full10, len(state.Processes))
		case <-ctx.Done():
			return
		}
	}
}

func main() {
	if err := rootCmd.Execute(); err != nil {
		log.Fatal(err)
	}
}
