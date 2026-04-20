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
	WorkloadType WorkloadType
}

type WorkloadType int

const (
	WorkloadGeneral WorkloadType = iota
	WorkloadAIInference
	WorkloadAITraining
	WorkloadGaming
	WorkloadBackground
)

func classifyProcess(p *process.Process) WorkloadType {
	name, err := p.Name()
	if err != nil {
		return WorkloadGeneral
	}

	switch name {
	case "python", "python3":
		cmdline, err := p.Cmdline()
		if err == nil {
			if containsAny(cmdline, "torch", "tensorflow", "llama", "transformers", "inference") {
				return WorkloadAIInference
			}
			if containsAny(cmdline, "train", "fit", "epoch") {
				return WorkloadAITraining
			}
		}
	case "steam", "steamwebhelper", "proton":
		return WorkloadGaming
	}

	return WorkloadGeneral
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

		processInfos = append(processInfos, &ProcessInfo{
			PID:          p.Pid,
			Name:         name,
			RSS:          memInfo.RSS,
			WorkloadType: classifyProcess(p),
		})
	}

	return &SystemState{
		Memory:    vmem,
		Processes: processInfos,
		PSI:       psi,
	}, nil
}

func writeSysfs(path, value string) error {
	f, err := os.OpenFile(path, os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	defer f.Close()

	_, err = f.WriteString(value)
	return err
}

func applyOptimizations(state *SystemState) {
	// Apply swappiness based on PSI (Pressure Stall Information)
	if state.PSI.Full10 > 10.0 || state.Memory.UsedPercent > 85 {
		writeSysfs("/proc/sys/vm/swappiness", "5")
		writeSysfs("/sys/kernel/mm/ksm/run", "1")
		writeSysfs("/sys/kernel/mm/ksm/pages_to_scan", "3000")
	} else if state.PSI.Some10 > 5.0 || state.Memory.UsedPercent > 70 {
		writeSysfs("/proc/sys/vm/swappiness", "20")
		writeSysfs("/sys/kernel/mm/ksm/run", "1")
	} else {
		writeSysfs("/proc/sys/vm/swappiness", "60")
		writeSysfs("/sys/kernel/mm/ksm/run", "0")
	}

	// Tune zswap based on workload
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
		writeSysfs("/sys/module/zswap/parameters/max_pool_percent", "20")
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
