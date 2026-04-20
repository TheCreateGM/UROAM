package main

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"

	"github.com/spf13/cobra"
)

var rootCmd = &cobra.Command{
	Use:   "memopt",
	Short: "CLI tool for Universal RAM Optimization Layer",
	Long:  `Command line interface for managing and monitoring the memory optimization system.`,
}

var statsCmd = &cobra.Command{
	Use:   "stats",
	Short: "Show memory optimization statistics",
	Run:   showStats,
}

var enableCmd = &cobra.Command{
	Use:   "enable",
	Short: "Enable memory optimization",
	Run:   enableOptimization,
}

var disableCmd = &cobra.Command{
	Use:   "disable",
	Short: "Disable memory optimization",
	Run:   disableOptimization,
}

var tuneCmd = &cobra.Command{
	Use:   "tune [workload]",
	Short: "Tune system for specific workload type",
	Long: `Tune system for specific workload:
  inference  - AI inference (low latency)
  training   - AI training (high throughput)
  gaming     - Gaming (responsiveness)
  general    - General purpose`,
	Args: cobra.ExactArgs(1),
	Run:  tuneWorkload,
}

var daemonCmd = &cobra.Command{
	Use:   "daemon",
	Short: "Control the optimization daemon",
}

var daemonStartCmd = &cobra.Command{
	Use:   "start",
	Short: "Start the optimization daemon",
	Run:   startDaemon,
}

var daemonStopCmd = &cobra.Command{
	Use:   "stop",
	Short: "Stop the optimization daemon",
	Run:   stopDaemon,
}

var daemonStatusCmd = &cobra.Command{
	Use:   "status",
	Short: "Show daemon status",
	Run:   statusDaemon,
}

func init() {
	daemonCmd.AddCommand(daemonStartCmd, daemonStopCmd, daemonStatusCmd)
	rootCmd.AddCommand(statsCmd, enableCmd, disableCmd, tuneCmd, daemonCmd)
}

func readSysfs(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(data)), nil
}

func writeSysfs(path, value string) error {
	return os.WriteFile(path, []byte(value), 0644)
}

func showStats(cmd *cobra.Command, args []string) {
	fmt.Println("=== MemOpt System Statistics ===")

	// KSM stats
	ksmRun, _ := readSysfs("/sys/kernel/mm/ksm/run")
	fmt.Printf("KSM Running: %s\n", ksmRun)

	if ksmRun == "1" {
		shared, _ := readSysfs("/sys/kernel/mm/ksm/pages_shared")
		sharing, _ := readSysfs("/sys/kernel/mm/ksm/pages_sharing")
		fmt.Printf("KSM Pages Shared: %s\n", shared)
		fmt.Printf("KSM Pages Sharing: %s\n", sharing)
	}

	// Zswap stats
	zswapEnabled, _ := readSysfs("/sys/module/zswap/parameters/enabled")
	fmt.Printf("\nZswap Enabled: %s\n", zswapEnabled)

	if zswapEnabled == "Y" {
		maxPool, _ := readSysfs("/sys/module/zswap/parameters/max_pool_percent")
		compressor, _ := readSysfs("/sys/module/zswap/parameters/compressor")
		fmt.Printf("Zswap Max Pool: %s%%\n", maxPool)
		fmt.Printf("Zswap Compressor: %s\n", compressor)
	}

	// System VM stats
	swappiness, _ := readSysfs("/proc/sys/vm/swappiness")
	thp, _ := readSysfs("/sys/kernel/mm/transparent_hugepage/enabled")
	fmt.Printf("\nSwappiness: %s\n", swappiness)
	fmt.Printf("Transparent HugePages: %s\n", thp)
}

func enableOptimization(cmd *cobra.Command, args []string) {
	fmt.Println("Enabling memory optimizations...")

	writeSysfs("/sys/kernel/mm/ksm/run", "1")
	writeSysfs("/sys/module/zswap/parameters/enabled", "Y")
	writeSysfs("/sys/kernel/mm/transparent_hugepage/enabled", "always")
	writeSysfs("/proc/sys/vm/swappiness", "10")

	fmt.Println("Optimizations enabled successfully")
}

func disableOptimization(cmd *cobra.Command, args []string) {
	fmt.Println("Disabling memory optimizations...")

	writeSysfs("/sys/kernel/mm/ksm/run", "0")
	writeSysfs("/sys/module/zswap/parameters/enabled", "N")
	writeSysfs("/sys/kernel/mm/transparent_hugepage/enabled", "madvise")
	writeSysfs("/proc/sys/vm/swappiness", "60")

	fmt.Println("Optimizations disabled successfully")
}

func tuneWorkload(cmd *cobra.Command, args []string) {
	workload := args[0]
	fmt.Printf("Tuning system for %s workload...\n", workload)

	switch workload {
	case "inference":
		writeSysfs("/proc/sys/vm/swappiness", "5")
		writeSysfs("/sys/kernel/mm/ksm/pages_to_scan", "500")
		writeSysfs("/sys/kernel/mm/transparent_hugepage/defrag", "always")
	case "training":
		writeSysfs("/proc/sys/vm/swappiness", "20")
		writeSysfs("/sys/kernel/mm/ksm/pages_to_scan", "2000")
		writeSysfs("/sys/kernel/mm/transparent_hugepage/defrag", "defer+madvise")
	case "gaming":
		writeSysfs("/proc/sys/vm/swappiness", "1")
		writeSysfs("/sys/kernel/mm/ksm/run", "0")
		writeSysfs("/sys/kernel/mm/transparent_hugepage/defrag", "always")
	case "general":
		writeSysfs("/proc/sys/vm/swappiness", "60")
		writeSysfs("/sys/kernel/mm/ksm/pages_to_scan", "1000")
	default:
		log.Fatalf("Unknown workload type: %s", workload)
	}

	fmt.Println("Workload tuning applied successfully")
}

func startDaemon(cmd *cobra.Command, args []string) {
	execCmd := exec.Command("memopt-daemon")
	err := execCmd.Start()
	if err != nil {
		log.Fatalf("Failed to start daemon: %v", err)
	}
	fmt.Printf("Daemon started with PID: %d\n", execCmd.Process.Pid)
}

func stopDaemon(cmd *cobra.Command, args []string) {
	// Find and kill daemon process
	out, err := exec.Command("pgrep", "-f", "memopt-daemon").Output()
	if err != nil {
		fmt.Println("Daemon is not running")
		return
	}

	pid, _ := strconv.Atoi(strings.TrimSpace(string(out)))
	process, err := os.FindProcess(pid)
	if err != nil {
		log.Fatal(err)
	}
	process.Kill()
	fmt.Println("Daemon stopped")
}

func statusDaemon(cmd *cobra.Command, args []string) {
	out, err := exec.Command("pgrep", "-f", "memopt-daemon").Output()
	if err != nil {
		fmt.Println("Daemon is not running")
		return
	}
	fmt.Printf("Daemon is running with PID: %s", out)
}

func main() {
	if err := rootCmd.Execute(); err != nil {
		log.Fatal(err)
	}
}
