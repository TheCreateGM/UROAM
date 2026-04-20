#!/usr/bin/env python3
"""
Universal RAM Optimization Layer - CLI Control Tool

This is the command-line interface for monitoring and controlling the uroam daemon.
"""

import argparse
import json
import requests
import sys
import time
import socket
import os
from typing import Optional, Dict, Any

# Default API endpoint
DEFAULT_API_URL = "http://localhost:8080"

# Timeout for API requests
API_TIMEOUT = 10  # seconds


class APIClient:
    """Client for interacting with the uroam daemon API"""

    def __init__(self, base_url: str = DEFAULT_API_URL, timeout: int = API_TIMEOUT):
        self.base_url = base_url.rstrip('/')
        self.timeout = timeout
        self.session = requests.Session()

    def _request(self, method: str, endpoint: str, data: Optional[Dict] = None,
                 params: Optional[Dict] = None) -> Dict[str, Any]:
        """Make an API request"""
        url = f"{self.base_url}{endpoint}"

        try:
            if method == "GET":
                response = self.session.get(url, params=params, timeout=self.timeout)
            elif method == "POST":
                response = self.session.post(url, json=data, timeout=self.timeout)
            else:
                raise ValueError(f"Unsupported HTTP method: {method}")

            response.raise_for_status()
            return response.json()

        except requests.exceptions.ConnectionError:
            raise ConnectionError(f"Failed to connect to {url}. Is the daemon running?")
        except requests.exceptions.Timeout:
            raise TimeoutError(f"Request to {url} timed out after {self.timeout} seconds")
        except requests.exceptions.RequestException as e:
            raise RuntimeError(f"API request failed: {e}")
        except ValueError as e:
            # Try to extract error from response if it exists
            if 'response' in locals():
                try:
                    error_data = response.json()
                    if 'error' in error_data:
                        raise RuntimeError(f"API Error: {error_data['error']}")
                except:
                    pass
            raise RuntimeError(f"API response error: {e}")

    def health_check(self) -> Dict[str, Any]:
        """Check daemon health"""
        return self._request("GET", "/api/v1/health")

    def status(self) -> Dict[str, Any]:
        """Get daemon status"""
        return self._request("GET", "/api/v1/status")

    def uptime(self) -> Dict[str, Any]:
        """Get daemon uptime"""
        return self._request("GET", "/api/v1/uptime")

    def metrics(self) -> Dict[str, Any]:
        """Get all system metrics"""
        return self._request("GET", "/api/v1/metrics")

    def memory_metrics(self) -> Dict[str, Any]:
        """Get memory metrics"""
        return self._request("GET", "/api/v1/metrics/memory")

    def process_metrics(self) -> Dict[str, Any]:
        """Get all process metrics"""
        return self._request("GET", "/api/v1/metrics/processes")

    def process_detail(self, pid: int) -> Dict[str, Any]:
        """Get metrics for a specific process"""
        return self._request("GET", f"/api/v1/metrics/process/{pid}")

    def ai_processes(self) -> Dict[str, Any]:
        """Get AI workload processes"""
        return self._request("GET", "/api/v1/metrics/ai")

    def gaming_processes(self) -> Dict[str, Any]:
        """Get gaming workload processes"""
        return self._request("GET", "/api/v1/metrics/gaming")

    def optimize(self) -> Dict[str, Any]:
        """Trigger optimization"""
        return self._request("POST", "/api/v1/optimize")

    def optimize_process(self, pid: int) -> Dict[str, Any]:
        """Optimize a specific process"""
        return self._request("POST", f"/api/v1/optimize/process/{pid}")

    def set_mode(self, mode: str) -> Dict[str, Any]:
        """Set optimization mode"""
        return self._request("POST", "/api/v1/optimize/mode", data={"mode": mode})

    def get_mode(self) -> Dict[str, Any]:
        """Get current optimization mode"""
        return self._request("GET", "/api/v1/optimize/mode")

    def get_actions(self) -> Dict[str, Any]:
        """Get recent optimization actions"""
        return self._request("GET", "/api/v1/optimize/actions")

    def get_config(self) -> Dict[str, Any]:
        """Get configuration"""
        return self._request("GET", "/api/v1/config")

    def reload(self) -> Dict[str, Any]:
        """Reload daemon configuration"""
        return self._request("POST", "/api/v1/control/reload")


class Formatter:
    """Formats output for display"""

    @staticmethod
    def format_bytes(num_bytes: int) -> str:
        """Format bytes to human-readable format"""
        for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
            if abs(num_bytes) < 1024.0:
                return f"{num_bytes:.1f} {unit}"
            num_bytes /= 1024.0
        return f"{num_bytes:.1f} PB"

    @staticmethod
    def format_percent(value: float, decimals: int = 1) -> str:
        """Format percentage"""
        return f"{value:.{decimals}f}%"

    @staticmethod
    def format_time(seconds: float) -> str:
        """Format seconds to human-readable time"""
        if seconds < 60:
            return f"{seconds:.0f}s"
        elif seconds < 3600:
            minutes = seconds / 60
            return f"{minutes:.0f}m"
        else:
            hours = seconds / 3600
            return f"{hours:.1f}h"


def print_health(client: APIClient, verbose: bool = False):
    """Print health check result"""
    result = client.health_check()

    if result.get('success', False):
        print("✓ Daemon is healthy")
        if verbose:
            print(f"  Message: {result.get('message', 'No message')}")
    else:
        print_error(result)


def print_status(client: APIClient, verbose: bool = False):
    """Print daemon status"""
    result = client.status()

    if result.get('success', False):
        data = result.get('data', {})
        status = data.get('status', 'unknown')
        uptime = data.get('uptime', 'unknown')
        version = data.get('version', 'unknown')

        print(f"Status: {status}")
        print(f"Version: {version}")
        print(f"Uptime: {uptime}")

        if verbose:
            print(f"\nFull response: {json.dumps(data, indent=2)}")
    else:
        print_error(result)


def print_memory_metrics(client: APIClient, verbose: bool = False):
    """Print memory metrics"""
    result = client.memory_metrics()

    if result.get('success', False):
        data = result.get('data', {})

        print("Memory Metrics")
        print("-" * 40)

        total = data.get('total', 0)
        free = data.get('free', 0)
        used = data.get('used', 0)
        available = data.get('available', 0)
        usage_percent = data.get('usage_percent', 0)

        print(f"Total:       {Formatter.format_bytes(total)}")
        print(f"Used:       {Formatter.format_bytes(used)} ({Formatter.format_percent(usage_percent)})")
        print(f"Free:       {Formatter.format_bytes(free)}")
        print(f"Available:  {Formatter.format_bytes(available)}")

        swap_total = data.get('swap_total', 0)
        swap_used = data.get('swap_used', 0)
        swap_free = data.get('swap_free', 0)

        print(f"\nSwap:")
        print(f"  Total: {Formatter.format_bytes(swap_total)}")
        print(f"  Used:  {Formatter.format_bytes(swap_used)}")
        print(f"  Free:  {Formatter.format_bytes(swap_free)}")

        if verbose:
            print(f"\nRaw data: {json.dumps(data, indent=2)}")
    else:
        print_error(result)


def print_processes(client: APIClient, filter_type: Optional[str] = None,
                     verbose: bool = False, limit: int = 20):
    """Print process metrics"""
    if filter_type == "ai":
        result = client.ai_processes()
    elif filter_type == "gaming":
        result = client.gaming_processes()
    else:
        result = client.process_metrics()

    if result.get('success', False):
        processes = result.get('data', [])

        if filter_type:
            print(f"{filter_type.capitalize()} Processes ({len(processes)})")
        else:
            print(f"All Processes ({len(processes)})")
        print("-" * 80)

        # Header
        header = f"{'PID':>8} {'Name':<20} {'RSS':>10} {'Priority':>8} {'Type':>10} {'Node':>5}"
        print(header)
        print("-" * 80)

        # Display processes
        for proc in processes[:min(len(processes), limit)]:
            pid = proc.get('pid', 0)
            name = proc.get('name', '')[:19]  # Truncate to 19 chars
            rss = Formatter.format_bytes(int(proc.get('memory_rss', 0)))
            priority = proc.get('priority', 'P2')
            workload = proc.get('workload_type', 'default')[:9]
            node = proc.get('numa_node', 0)

            print(f"{pid:>8} {name:<20} {rss:>10} {priority:>8} {workload:>10} {node:>5}")

        if len(processes) > limit:
            print(f"\n... and {len(processes) - limit} more processes")

        if verbose:
            print(f"\nFull data: {json.dumps(processes[:5], indent=2)}")
    else:
        print_error(result)


def print_optimization_actions(client: APIClient):
    """Print recent optimization actions"""
    result = client.get_actions()

    if result.get('success', False):
        actions = result.get('data', [])

        print(f"Recent Optimization Actions ({len(actions)})")
        print("-" * 40)

        if actions:
            for i, action in enumerate(actions, 1):
                print(f"{i}. {action}")
        else:
            print("No recent actions")
    else:
        print_error(result)


def print_mode(client: APIClient):
    """Print current optimization mode"""
    result = client.get_mode()

    if result.get('success', False):
        data = result.get('data', {})
        mode = data.get('mode', 'unknown')
        print(f"Current optimization mode: {mode}")
    else:
        print_error(result)


def set_mode(client: APIClient, mode: str):
    """Set optimization mode"""
    valid_modes = ['default', 'aggressive', 'conservative', 'gaming', 'ai']

    if mode not in valid_modes:
        print_error({"error": f"Invalid mode. Valid modes: {', '.join(valid_modes)}"})
        return

    result = client.set_mode(mode)

    if result.get('success', False):
        print(f"✓ Optimization mode set to: {mode}")
    else:
        print_error(result)


def trigger_optimize(client: APIClient, pid: Optional[int] = None):
    """Trigger optimization"""
    if pid is not None:
        result = client.optimize_process(pid)
        if result.get('success', False):
            print(f"✓ Optimization triggered for process {pid}")
        else:
            print_error(result)
    else:
        result = client.optimize()
        if result.get('success', False):
            print("✓ System-wide optimization triggered")
        else:
            print_error(result)


def reload_config(client: APIClient):
    """Reload daemon configuration"""
    result = client.reload()

    if result.get('success', False):
        print("✓ Configuration reload requested")
    else:
        print_error(result)


def print_error(result: Dict[str, Any]):
    """Print error message from API response"""
    error_msg = result.get('error', 'Unknown error')
    success = result.get('success', False)

    if not success:
        print(f"✗ Error: {error_msg}")
    else:
        print(f"✗ Unexpected error: {result}")


def check_daemon_running(api_url: str) -> bool:
    """Check if daemon is running"""
    client = APIClient(base_url=api_url)
    try:
        result = client.health_check()
        return result.get('success', False)
    except (ConnectionError, TimeoutError):
        return False


def main():
    """Main entry point"""

    parser = argparse.ArgumentParser(
        prog='uroamctl',
        description='Universal RAM Optimization Layer - CLI Control Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  uroamctl status                    Show daemon status
  uroamctl metrics                  Show system memory metrics
  uroamctl processes                List all tracked processes
  uroamctl processes --ai           List AI workload processes
  uroamctl optimize                  Trigger system optimization
  uroamctl mode gaming              Set optimization mode to gaming
  uroamctl monitor                  Real-time monitoring (not yet implemented)
        """
    )

    # Global options
    parser.add_argument(
        '--api-url',
        default=DEFAULT_API_URL,
        help=f'Daemon API URL (default: {DEFAULT_API_URL})'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Show verbose output'
    )
    parser.add_argument(
        '--json',
        action='store_true',
        help='Output in JSON format'
    )

    # Subcommands
    subparsers = parser.add_subparsers(dest='command', help='Available commands')

    # Status command
    status_parser = subparsers.add_parser('status', help='Show daemon status')

    # Health command
    subparsers.add_parser('health', help='Check daemon health')

    # Uptime command
    subparsers.add_parser('uptime', help='Show daemon uptime')

    # Metrics command
    metrics_parser = subparsers.add_parser('metrics', help='Show system metrics')
    metrics_parser.add_argument(
        '--memory',
        action='store_true',
        help='Show only memory metrics'
    )

    # Processes command
    processes_parser = subparsers.add_parser('processes', help='Show process metrics')
    processes_parser.add_argument(
        '--ai',
        action='store_true',
        help='Show only AI workload processes'
    )
    processes_parser.add_argument(
        '--gaming',
        action='store_true',
        help='Show only gaming workload processes'
    )
    processes_parser.add_argument(
        '--pid',
        type=int,
        help='Show metrics for a specific process'
    )
    processes_parser.add_argument(
        '-n', '--limit',
        type=int,
        default=20,
        help='Limit number of processes shown (default: 20)'
    )

    # Optimize command
    optimize_parser = subparsers.add_parser('optimize', help='Trigger optimization')
    optimize_parser.add_argument(
        'pid',
        nargs='?',
        type=int,
        help='Optimize a specific process (optional, triggers system-wide if not provided)'
    )

    # Mode command
    mode_parser = subparsers.add_parser('mode', help='Get or set optimization mode')
    mode_parser.add_argument(
        'mode',
        nargs='?',
        help='Set optimization mode (default: show current mode)'
    )

    # Actions command
    subparsers.add_parser('actions', help='Show recent optimization actions')

    # Config command
    config_parser = subparsers.add_parser('config', help='Show configuration')

    # Reload command
    subparsers.add_parser('reload', help='Reload daemon configuration')

    # Monitor command (for future real-time monitoring)
    subparsers.add_parser('monitor', help='Real-time monitoring (not yet implemented)')

    # Parse arguments
    args = parser.parse_args()

    # Check if daemon is running (except for help)
    if args.command not in ['help', None]:
        if not check_daemon_running(args.api_url):
            print(f"Error: Daemon not running at {args.api_url}")
            print("Start the daemon with: sudo uroamd")
            sys.exit(1)

    # Create API client
    client = APIClient(base_url=args.api_url)

    # Route commands
    try:
        if args.command == 'status':
            if args.json:
                result = client.status()
                print(json.dumps(result, indent=2))
            else:
                print_status(client, args.verbose)

        elif args.command == 'health':
            if args.json:
                result = client.health_check()
                print(json.dumps(result, indent=2))
            else:
                print_health(client, args.verbose)

        elif args.command == 'uptime':
            if args.json:
                result = client.uptime()
                print(json.dumps(result, indent=2))
            else:
                print_health(client, args.verbose)  # For now, same as health

        elif args.command == 'metrics':
            if args.json:
                result = client.metrics() if not args.memory else client.memory_metrics()
                print(json.dumps(result, indent=2))
            elif args.memory:
                print_memory_metrics(client, args.verbose)
            else:
                result = client.metrics()
                if result.get('success', False):
                    print_memory_metrics(client, args.verbose)
                    print()
                    processes = result.get('data', {}).get('processes', [])
                    print(f"Tracked Processes: {len(processes)}")

        elif args.command == 'processes':
            filter_type = None
            if args.ai:
                filter_type = "ai"
            elif args.gaming:
                filter_type = "gaming"

            if args.pid:
                if args.json:
                    result = client.process_detail(args.pid)
                    print(json.dumps(result, indent=2))
                else:
                    result = client.process_detail(args.pid)
                    if result.get('success', False):
                        data = result.get('data', {})
                        print(f"Process {data.get('pid', '?')}")
                        print(f"  Name: {data.get('name', '?')}")
                        print(f"  RSS: {Formatter.format_bytes(int(data.get('memory_rss', 0)))}")
                        print(f"  VMS: {Formatter.format_bytes(int(data.get('memory_vms', 0)))}")
                        print(f"  Priority: {data.get('priority', '?')}")
                        print(f"  Workload: {data.get('workload_type', '?')}")
                        print(f"  NUMA Node: {data.get('numa_node', '?')}")
            elif args.json:
                if filter_type:
                    result = client.ai_processes() if filter_type == "ai" else client.gaming_processes()
                else:
                    result = client.process_metrics()
                print(json.dumps(result, indent=2))
            else:
                print_processes(client, filter_type, args.verbose, args.limit)

        elif args.command == 'optimize':
            trigger_optimize(client, args.pid)

        elif args.command == 'mode':
            if args.json:
                result = client.get_mode()
                print(json.dumps(result, indent=2))
            elif args.mode:
                set_mode(client, args.mode)
            else:
                print_mode(client)

        elif args.command == 'actions':
            if args.json:
                result = client.get_actions()
                print(json.dumps(result, indent=2))
            else:
                print_optimization_actions(client)

        elif args.command == 'config':
            result = client.get_config()
            if args.json:
                print(json.dumps(result, indent=2))
            else:
                if result.get('success', False):
                    print("Configuration:")
                    print("-" * 40)
                    print(json.dumps(result.get('data', {}), indent=2))
                else:
                    print_error(result)

        elif args.command == 'reload':
            reload_config(client)

        elif args.command == 'monitor':
            print("Real-time monitoring is not yet implemented.")
            print("Running 'uroamctl metrics --memory' in watch mode:")
            print()
            # Simple watch-like behavior
            try:
                while True:
                    os.system('clear' if os.name == 'posix' else 'cls')
                    print_memory_metrics(client)
                    print()
                    time.sleep(2)
            except KeyboardInterrupt:
                print("\nMonitoring stopped.")

        elif args.command is None or args.command == 'help':
            parser.print_help()

        else:
            print(f"Unknown command: {args.command}")
            parser.print_help()

    except (ConnectionError, TimeoutError) as e:
        print_error({"error": str(e)})
        sys.exit(1)
    except Exception as e:
        print_error({"error": f"Unexpected error: {e}"})
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
