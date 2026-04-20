"""
Universal RAM Optimization Layer - Core Python Module

This module provides the core Python API for interacting with the UROAM
memory optimization system from Python.
"""

import contextlib
import ctypes
import ctypes.util
import os
import sys
import threading
from typing import Any, Dict, List, Optional, Union


class UROAMError(Exception):
    """Base exception for UROAM errors"""

    pass


class MemoryAllocationError(UROAMError):
    """Exception for memory allocation failures"""

    pass


class NotInitializedError(UROAMError):
    """Exception raised when UROAM is not initialized"""

    pass


# Workload types
WORKLOAD_DEFAULT = "default"
WORKLOAD_AI = "ai"
WORKLOAD_INFERENCE = "inference"
WORKLOAD_TRAINING = "training"
WORKLOAD_GAMING = "gaming"
WORKLOAD_BACKGROUND = "background"

# Priority levels
PRIORITY_P0 = "P0"  # Highest - AI Inference
PRIORITY_P1 = "P1"  # Gaming
PRIORITY_P2 = "P2"  # AI Training
PRIORITY_P3 = "P3"  # Background
PRIORITY_P4 = "P4"  # Lowest - Idle


class UROAMConfig:
    """Configuration for UROAM"""

    def __init__(self, **kwargs):
        self.enabled = kwargs.get("enabled", True)
        self.debug = kwargs.get("debug", False)
        self.daemon_socket = kwargs.get("daemon_socket", "/var/run/uroam.sock")
        self.api_address = kwargs.get("api_address", "http://localhost:8080")
        self.optimization_level = kwargs.get("optimization_level", "balanced")
        self.workload_type = kwargs.get("workload_type", WORKLOAD_DEFAULT)
        self.priority = kwargs.get("priority", PRIORITY_P2)
        self.numa_aware = kwargs.get("numa_aware", True)
        self.use_hugepages = kwargs.get("use_hugepages", True)
        self.enable_compression = kwargs.get("enable_compression", True)
        self.enable_deduplication = kwargs.get("enable_deduplication", True)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary"""
        return {
            "enabled": self.enabled,
            "debug": self.debug,
            "daemon_socket": self.daemon_socket,
            "api_address": self.api_address,
            "optimization_level": self.optimization_level,
            "workload_type": self.workload_type,
            "priority": self.priority,
            "numa_aware": self.numa_aware,
            "use_hugepages": self.use_hugepages,
            "enable_compression": self.enable_compression,
            "enable_deduplication": self.enable_deduplication,
        }


class MemoryStats:
    """Memory statistics"""

    def __init__(self, **kwargs):
        self.total = kwargs.get("total", 0)
        self.used = kwargs.get("used", 0)
        self.free = kwargs.get("free", 0)
        self.available = kwargs.get("available", 0)
        self.usage_percent = kwargs.get("usage_percent", 0.0)

    def __repr__(self) -> str:
        return f"MemoryStats(total={self.total}, used={self.used}, free={self.free}, usage={self.usage_percent:.1f}%)"


class ProcessStats:
    """Process-specific statistics"""

    def __init__(self, **kwargs):
        self.pid = kwargs.get("pid", 0)
        self.name = kwargs.get("name", "")
        self.memory_rss = kwargs.get("memory_rss", 0)
        self.memory_vms = kwargs.get("memory_vms", 0)
        self.memory_swap = kwargs.get("memory_swap", 0)
        self.numa_node = kwargs.get("numa_node", 0)
        self.priority = kwargs.get("priority", PRIORITY_P2)
        self.workload_type = kwargs.get("workload_type", WORKLOAD_DEFAULT)

    def __repr__(self) -> str:
        return (
            f"ProcessStats(pid={self.pid}, name='{self.name}', rss={self.memory_rss}, "
            f"priority={self.priority}, workload={self.workload_type})"
        )


# Global state
_initialized = False
_library_loaded = False
_library = None
_config = UROAMConfig()
_lock = threading.Lock()


def _load_library() -> bool:
    """Load the UROAM C library"""
    global _library_loaded, _library

    if _library_loaded:
        return True

    # Try to load the library
    library_names = [
        "uroam",
        "liburoam",
        "liburoam.so",
        "liburoam_python.so",
    ]

    for name in library_names:
        try:
            if os.name == "posix":
                # Try using ctypes.util on POSIX
                _library = ctypes.CDLL(name)
            else:
                # On Windows, try direct loading
                if os.name == "nt":
                    # Try with .dll extension
                    _library = ctypes.windll[name]
                    if _library is None:
                        _library = ctypes.windll[name + ".dll"]

            if _library is not None:
                _library_loaded = True
                return True
        except Exception:
            continue

    # Failed to load
    return False


def _initialize_library() -> None:
    """Initialize the UROAM library"""
    global _initialized

    if _initialized:
        return

    if not _load_library():
        # Library not available, use fallback or raise
        # For now, we'll just set initialized to True but warn
        import warnings

        warnings.warn(
            "UROAM C library not found, using limited functionality", RuntimeWarning
        )

    _initialized = True


def init(config: Optional[UROAMConfig] = None) -> None:
    """
    Initialize the UROAM optimization layer.

    Args:
        config: Optional configuration. If None, default configuration is used.
    """
    global _config, _initialized

    with _lock:
        if config is not None:
            _config = config

        _initialize_library()

        # TODO: Connect to daemon
        # TODO: Register process
        # TODO: Set workload type and priority

        _initialized = True

        if _config.debug:
            print(f"UROAM initialized with config: {_config.to_dict()}")


def cleanup() -> None:
    """Cleanup and shutdown the UROAM optimization layer"""
    global _initialized, _library_loaded, _library

    with _lock:
        # TODO: Unregister process
        # TODO: Disconnect from daemon

        _initialized = False
        _library_loaded = False
        _library = None


def is_initialized() -> bool:
    """Check if UROAM is initialized"""
    return _initialized


def is_enabled() -> bool:
    """Check if UROAM optimizations are enabled"""
    return _config.enabled and _initialized


def enable_optimization(enabled: bool = True) -> None:
    """Enable or disable UROAM optimizations"""
    global _config
    _config.enabled = enabled


def disable_optimization() -> None:
    """Disable UROAM optimizations"""
    enable_optimization(False)


def allocate(
    size: int, alignment: int = 0, hugepage: bool = False
) -> Optional[ctypes.c_void_p]:
    """
    Allocate memory through UROAM.

    Args:
        size: Size in bytes to allocate
        alignment: Alignment requirement (0 for default)
        hugepage: Whether to use huge pages

    Returns:
        Pointer to allocated memory, or None on failure
    """
    if not is_enabled():
        # Fallback to regular malloc
        try:
            libc = ctypes.CDLL(ctypes.util.find_library("c"))
            malloc = libc.malloc
            malloc.argtypes = [ctypes.c_size_t]
            malloc.restype = ctypes.c_void_p
            return malloc(size)
        except Exception as e:
            raise MemoryAllocationError(f"Failed to allocate memory: {e}")

    # TODO: Use UROAM's optimized allocator
    # For now, use system malloc
    try:
        libc = ctypes.CDLL(ctypes.util.find_library("c"))
        if alignment > 0:
            # Use aligned allocation
            posix_memalign = libc.posix_memalign
            posix_memalign.argtypes = [
                ctypes.POINTER(ctypes.c_void_p),
                ctypes.c_size_t,
                ctypes.c_size_t,
            ]
            posix_memalign.restype = ctypes.c_int

            ptr = ctypes.c_void_p()
            result = posix_memalign(ctypes.byref(ptr), alignment, size)
            if result != 0:
                raise MemoryAllocationError(
                    f"Failed to allocate aligned memory: {result}"
                )
            return ptr
        else:
            malloc = libc.malloc
            malloc.argtypes = [ctypes.c_size_t]
            malloc.restype = ctypes.c_void_p
            return malloc(size)
    except Exception as e:
        raise MemoryAllocationError(f"Failed to allocate memory: {e}")


def allocate_aligned(size: int, alignment: int = 64) -> Optional[ctypes.c_void_p]:
    """
    Allocate aligned memory through UROAM.

    Args:
        size: Size in bytes to allocate
        alignment: Alignment requirement (must be power of 2)

    Returns:
        Pointer to aligned allocated memory
    """
    return allocate(size, alignment, False)


def allocate_hugepage(size: int) -> Optional[ctypes.c_void_p]:
    """
    Allocate memory using huge pages.

    Note: Huge page allocation may fail if huge pages are not available
    or if the size is not a multiple of the huge page size.

    Args:
        size: Size in bytes to allocate (rounded up to huge page size)

    Returns:
        Pointer to allocated memory
    """
    # TODO: Implement huge page allocation
    # For now, just use regular allocation
    return allocate(size, 0, True)


def free(ptr: Union[ctypes.c_void_p, int, None]) -> None:
    """
    Free memory allocated through UROAM.

    Args:
        ptr: Pointer to the memory to free
    """
    if ptr is None:
        return

    try:
        libc = ctypes.CDLL(ctypes.util.find_library("c"))
        cfree = libc.free
        cfree.argtypes = [ctypes.c_void_p]
        cfree.restype = None

        if isinstance(ptr, int):
            ptr = ctypes.c_void_p(ptr)

        cfree(ptr)
    except Exception as e:
        raise MemoryAllocationError(f"Failed to free memory: {e}")


def register_memory(
    address: Union[ctypes.c_void_p, int],
    size: int,
    priority: str = PRIORITY_P2,
    workload: str = WORKLOAD_DEFAULT,
) -> bool:
    """
    Register externally allocated memory with UROAM for optimization.

    This allows UROAM to track and optimize memory that wasn't allocated
    through its own allocator.

    Args:
        address: Memory address to register
        size: Size of the memory region
        priority: Priority level for this memory
        workload: Workload type

    Returns:
        True if registration succeeded
    """
    # TODO: Implement memory registration via daemon IPC
    # For now, just return True
    return True


def unregister_memory(address: Union[ctypes.c_void_p, int]) -> bool:
    """
    Unregister memory from UROAM tracking.

    Args:
        address: Memory address to unregister

    Returns:
        True if unregistration succeeded
    """
    # TODO: Implement memory unregistration
    return True


def set_priority(priority: str) -> None:
    """
    Set the priority for the current process.

    Args:
        priority: Priority level (P0, P1, P2, P3, P4)
    """
    global _config
    _config.priority = priority
    # TODO: Notify daemon


def set_workload_type(workload: str) -> None:
    """
    Set the workload type for the current process.

    Args:
        workload: Workload type (default, ai, inference, training, gaming, background)
    """
    global _config
    _config.workload_type = workload
    # TODO: Notify daemon


def get_memory_stats() -> MemoryStats:
    """
    Get current memory statistics.

    Returns:
        MemoryStats object with current memory information
    """
    # Try to get from daemon API
    try:
        import requests

        response = requests.get(
            f"{_config.api_address}/api/v1/metrics/memory", timeout=5
        )
        if response.status_code == 200:
            data = response.json()
            if data.get("success", False):
                stats_data = data.get("data", {})
                return MemoryStats(
                    total=stats_data.get("total", 0),
                    used=stats_data.get("used", 0),
                    free=stats_data.get("free", 0),
                    available=stats_data.get("available", 0),
                    usage_percent=stats_data.get("usage_percent", 0.0),
                )
    except Exception:
        pass

    # Fallback to system metrics
    try:
        import psutil

        mem = psutil.virtual_memory()
        return MemoryStats(
            total=mem.total,
            used=mem.used,
            free=mem.free,
            available=mem.available,
            usage_percent=mem.percent,
        )
    except ImportError:
        # If psutil is not available, return 0 values
        return MemoryStats()


def get_process_stats(pid: Optional[int] = None) -> ProcessStats:
    """
    Get process statistics.

    Args:
        pid: Process ID to query. If None, uses current process.

    Returns:
        ProcessStats object with process information
    """
    if pid is None:
        pid = os.getpid()

    # Try to get from daemon API
    try:
        import requests

        response = requests.get(
            f"{_config.api_address}/api/v1/metrics/process/{pid}", timeout=5
        )
        if response.status_code == 200:
            data = response.json()
            if data.get("success", False):
                proc_data = data.get("data", {})
                return ProcessStats(
                    pid=proc_data.get("pid", pid),
                    name=proc_data.get("name", ""),
                    memory_rss=proc_data.get("memory_rss", 0),
                    memory_vms=proc_data.get("memory_vms", 0),
                    memory_swap=proc_data.get("memory_swap", 0),
                    numa_node=proc_data.get("numa_node", 0),
                    priority=proc_data.get("priority", PRIORITY_P2),
                    workload_type=proc_data.get("workload_type", WORKLOAD_DEFAULT),
                )
    except Exception:
        pass

    # Fallback to psutil
    try:
        import psutil

        proc = psutil.Process(pid)
        mem_info = proc.memory_info()
        return ProcessStats(
            pid=pid,
            name=proc.name(),
            memory_rss=mem_info.rss,
            memory_vms=mem_info.vms,
            memory_swap=0,
            numa_node=0,
            priority=_config.priority,
            workload_type=_config.workload_type,
        )
    except ImportError:
        # If psutil is not available, return minimal info
        return ProcessStats(pid=pid)


def get_config() -> UROAMConfig:
    """Get current UROAM configuration"""
    return _config


def set_config(config: UROAMConfig) -> None:
    """Set UROAM configuration"""
    global _config
    _config = config


@contextlib.contextmanager
def ai_context():
    """Context manager for AI workload operations"""
    old_workload = _config.workload_type
    old_priority = _config.priority

    set_workload_type(WORKLOAD_AI)
    set_priority(PRIORITY_P2)

    try:
        yield
    finally:
        set_workload_type(old_workload)
        set_priority(old_priority)


@contextlib.contextmanager
def inference_context():
    """Context manager for inference operations"""
    old_workload = _config.workload_type
    old_priority = _config.priority

    set_workload_type(WORKLOAD_INFERENCE)
    set_priority(PRIORITY_P0)

    try:
        yield
    finally:
        set_workload_type(old_workload)
        set_priority(old_priority)


@contextlib.contextmanager
def training_context():
    """Context manager for training operations"""
    old_workload = _config.workload_type
    old_priority = _config.priority

    set_workload_type(WORKLOAD_TRAINING)
    set_priority(PRIORITY_P2)

    try:
        yield
    finally:
        set_workload_type(old_workload)
        set_priority(old_priority)


@contextlib.contextmanager
def gaming_context():
    """Context manager for gaming operations"""
    old_workload = _config.workload_type
    old_priority = _config.priority

    set_workload_type(WORKLOAD_GAMING)
    set_priority(PRIORITY_P1)

    try:
        yield
    finally:
        set_workload_type(old_workload)
        set_priority(old_priority)


# WyHash-compatible hash function for memory tracking
def _wyhash64(data: Union[bytes, bytearray, memoryview], seed: int = 0) -> int:
    """
    Compute WyHash64 hash of data for memory deduplication detection.

    This is a simple implementation for demonstration. In production,
    we would use the actual WyHash algorithm or a kernel-supported hash.

    Args:
        data: Data to hash
        seed: Optional seed value

    Returns:
        64-bit hash value
    """
    if not data:
        return 0

    # Simple hash for demonstration
    # In production, use a proper hash function
    import hashlib

    h = hashlib.sha256(data)
    return int.from_bytes(h.digest()[:8], "little")


def estimate_memory_savings(data: Union[bytes, bytearray, memoryview]) -> float:
    """
    Estimate potential memory savings if this data were deduplicated.

    Args:
        data: Data to analyze

    Returns:
        Estimated savings ratio (0.0 to 1.0)
    """
    # This is a placeholder implementation
    # In production, this would use actual analysis
    return 0.0


# Memory pool for small allocations
class MemoryPool:
    """Simple memory pool for small allocations"""

    def __init__(self, size: int = 4096, count: int = 1024):
        """
        Initialize a memory pool.

        Args:
            size: Size of each block in bytes
            count: Number of blocks to pre-allocate
        """
        self.size = size
        self.count = count
        self.free_blocks = []
        self.allocated = 0

        # Pre-allocate blocks
        for _ in range(count):
            block = allocate(size)
            if block:
                self.free_blocks.append(block)

    def allocate(self) -> Optional[ctypes.c_void_p]:
        """Allocate a block from the pool"""
        if self.free_blocks:
            block = self.free_blocks.pop()
            self.allocated += 1
            return block
        return allocate(self.size)

    def free(self, block: ctypes.c_void_p) -> None:
        """Free a block back to the pool"""
        if len(self.free_blocks) < self.count:
            self.free_blocks.append(block)
            self.allocated -= 1
        else:
            free(block)

    def get_stats(self) -> Dict[str, int]:
        """Get pool statistics"""
        return {
            "size": self.size,
            "count": self.count,
            "free": len(self.free_blocks),
            "allocated": self.allocated,
        }
