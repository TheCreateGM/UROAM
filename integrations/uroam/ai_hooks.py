"""
UROAM - PyTorch and TensorFlow Integration Module

This module provides memory optimization hooks for AI frameworks.
"""

import os
import sys
import ctypes
import threading
import gc
import weakref
from typing import Optional, Dict, Any, Callable
from contextlib import contextmanager

try:
    import torch
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False

try:
    import tensorflow as tf
    TF_AVAILABLE = True
except ImportError:
    TF_AVAILABLE = False

try:
    import numpy as np
    NUMPY_AVAILABLE = True
except ImportError:
    NUMPY_AVAILABLE = False


class UROAMMemoryTracker:
    """Track memory allocations for AI frameworks"""

    def __init__(self):
        self.enabled = False
        self.allocations = {}
        self.total_allocated = 0
        self.total_freed = 0
        self._lock = threading.Lock()

    def enable(self):
        self.enabled = True

    def disable(self):
        self.enabled = False

    def record_allocation(self, size: int, tag: str = ""):
        if not self.enabled:
            return

        with self._lock:
            self.total_allocated += size
            self.allocations[tag] = self.allocations.get(tag, 0) + size

    def record_free(self, size: int, tag: str = ""):
        if not self.enabled:
            return

        with self._lock:
            self.total_freed += size
            self.allocations[tag] = max(0, self.allocations.get(tag, 0) - size)

    def get_stats(self) -> Dict[str, Any]:
        with self._lock:
            return {
                'total_allocated': self.total_allocated,
                'total_freed': self.total_freed,
                'current': self.total_allocated - self.total_freed,
                'allocations': dict(self.allocations)
            }


memory_tracker = UROAMMemoryTracker()


class TorchMemoryOptimizer:
    """Optimize memory for PyTorch workloads"""

    def __init__(self):
        self.enabled = False
        self.original_alloc = None
        self.original_free = None

    def enable(self):
        if not TORCH_AVAILABLE:
            return False

        try:
            torch.cuda.empty_cache() if torch.cuda.is_available() else None

            memory_tracker.enable()
            self.enabled = True

            return True
        except Exception as e:
            print(f"Failed to enable PyTorch optimizer: {e}")
            return False

    def disable(self):
        self.enabled = False
        memory_tracker.disable()

    def optimize_inference(self):
        """Optimize for inference workloads"""
        if not TORCH_AVAILABLE:
            return

        for obj in gc.get_objects():
            try:
                if torch.is_tensor(obj):
                    if not obj.requires_grad:
                        obj.requires_grad_(False)
            except:
                pass

        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    def optimize_training(self):
        """Optimize for training workloads"""
        if not TORCH_AVAILABLE:
            return

        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
            torch.cuda.synchronize()

    def enable_gradient_checkpointing(self, model):
        """Enable gradient checkpointing to save memory"""
        if not TORCH_AVAILABLE:
            return

        if hasattr(model, 'gradient_checkpointing_enable'):
            model.gradient_checkpointing_enable()

    def set_memory_fraction(self, fraction: float = 0.9):
        """Set GPU memory fraction"""
        if not TORCH_AVAILABLE or not torch.cuda.is_available():
            return

        torch.cuda.set_per_process_memory_fraction(fraction)

    def get_memory_stats(self) -> Dict[str, Any]:
        if not TORCH_AVAILABLE:
            return {}

        stats = {
            'framework': 'pytorch',
            'cuda_available': torch.cuda.is_available()
        }

        if torch.cuda.is_available():
            stats.update({
                'memory_allocated': torch.cuda.memory_allocated(),
                'memory_reserved': torch.cuda.memory_reserved(),
                'max_memory_allocated': torch.cuda.max_memory_allocated(),
                'max_memory_reserved': torch.cuda.max_memory_reserved(),
            })

        return stats


class TFMemoryOptimizer:
    """Optimize memory for TensorFlow workloads"""

    def __init__(self):
        self.enabled = False

    def enable(self):
        if not TF_AVAILABLE:
            return False

        try:
            memory_tracker.enable()

            gpus = tf.config.list_physical_devices('GPU')
            for gpu in gpus:
                tf.config.experimental.set_memory_growth(gpu, True)

            self.enabled = True
            return True
        except Exception as e:
            print(f"Failed to enable TensorFlow optimizer: {e}")
            return False

    def disable(self):
        self.enabled = False
        memory_tracker.disable()

    def optimize_inference(self):
        """Optimize for inference"""
        if not TF_AVAILABLE:
            return

        tf.keras.backend.clear_session()
        gc.collect()

    def optimize_training(self):
        """Optimize for training"""
        if not TF_AVAILABLE:
            return

        tf.keras.backend.clear_session()
        gc.collect()

    def enable_xla(self):
        """Enable XLA compilation"""
        if not TF_AVAILABLE:
            return

        tf.config.optimizer.set_jit(True)

    def set_memory_growth(self, enabled: bool = True):
        """Enable memory growth for GPUs"""
        if not TF_AVAILABLE:
            return

        gpus = tf.config.list_physical_devices('GPU')
        for gpu in gpus:
            try:
                tf.config.experimental.set_memory_growth(gpu, enabled)
            except:
                pass

    def get_memory_stats(self) -> Dict[str, Any]:
        if not TF_AVAILABLE:
            return {}

        stats = {
            'framework': 'tensorflow',
        }

        try:
            import tensorflow as tf
            if tf.config.list_physical_devices('GPU'):
                stats['gpu_available'] = True
        except:
            stats['gpu_available'] = False

        return stats


class ONNXRuntimeOptimizer:
    """Optimize memory for ONNX Runtime"""

    def __init__(self):
        self.enabled = False

    def enable(self):
        self.enabled = True
        memory_tracker.enable()

    def disable(self):
        self.enabled = False
        memory_tracker.disable()

    def set_memory_pattern(self, enable: bool = True):
        """Enable memory pattern optimization"""
        if not self.enabled:
            return {}

    def get_memory_stats(self) -> Dict[str, Any]:
        return {'framework': 'onnxruntime'}


torch_optimizer = TorchMemoryOptimizer()
tf_optimizer = TFMemoryOptimizer()
onnx_optimizer = ONNXRuntimeOptimizer()


@contextmanager
def inference_context(framework: str = "auto"):
    """
    Context manager for AI inference workloads.
    Automatically optimizes memory based on the detected framework.

    Usage:
        with uroam.inference_context("pytorch"):
            result = model(input_data)
    """
    if framework == "auto":
        if TORCH_AVAILABLE:
            framework = "pytorch"
        elif TF_AVAILABLE:
            framework = "tensorflow"

    if framework == "pytorch":
        torch_optimizer.optimize_inference()
    elif framework == "tensorflow":
        tf_optimizer.optimize_inference()
    elif framework == "onnxruntime":
        onnx_optimizer.optimize_inference()

    try:
        yield
    finally:
        gc.collect()


@contextmanager
def training_context(framework: str = "auto"):
    """
    Context manager for AI training workloads.
    Automatically optimizes memory based on the detected framework.
    """
    if framework == "auto":
        if TORCH_AVAILABLE:
            framework = "pytorch"
        elif TF_AVAILABLE:
            framework = "tensorflow"

    if framework == "pytorch":
        torch_optimizer.optimize_training()
    elif framework == "tensorflow":
        tf_optimizer.optimize_training()

    try:
        yield
    finally:
        gc.collect()


def detect_framework() -> Optional[str]:
    """Detect which AI framework is being used"""
    if TORCH_AVAILABLE:
        return "pytorch"
    elif TF_AVAILABLE:
        return "tensorflow"

    frame = sys.modules.get('onnxruntime')
    if frame:
        return "onnxruntime"

    return None


def get_memory_stats() -> Dict[str, Any]:
    """Get memory statistics from all frameworks"""
    stats = {
        'uroam_tracker': memory_tracker.get_stats(),
        'framework': detect_framework()
    }

    if TORCH_AVAILABLE:
        stats['pytorch'] = torch_optimizer.get_memory_stats()

    if TF_AVAILABLE:
        stats['tensorflow'] = tf_optimizer.get_memory_stats()

    return stats


def optimize_workload(workload_type: str):
    """
    Optimize memory for specific workload type.

    Args:
        workload_type: One of "inference", "training", "gaming", "general"
    """
    if workload_type == "inference":
        torch_optimizer.optimize_inference()
        tf_optimizer.optimize_inference()
    elif workload_type == "training":
        torch_optimizer.optimize_training()
        tf_optimizer.optimize_training()
    elif workload_type == "gaming":
        gc.collect()
    elif workload_type == "general":
        gc.collect()


class MemoryOptimizer:
    """Main class for AI memory optimization"""

    def __init__(self):
        self.torch = torch_optimizer
        self.tf = tf_optimizer
        self.onnx = onnx_optimizer

    def enable(self, framework: str = "auto"):
        if framework == "auto":
            framework = detect_framework() or "all"

        if framework in ("pytorch", "all"):
            torch_optimizer.enable()
        if framework in ("tensorflow", "all"):
            tf_optimizer.enable()
        if framework in ("onnxruntime", "all"):
            onnx_optimizer.enable()

    def disable(self):
        torch_optimizer.disable()
        tf_optimizer.disable()
        onnx_optimizer.disable()

    def get_stats(self) -> Dict[str, Any]:
        return get_memory_stats()


optimizer = MemoryOptimizer()

__all__ = [
    'optimizer',
    'torch_optimizer',
    'tf_optimizer',
    'onnx_optimizer',
    'inference_context',
    'training_context',
    'detect_framework',
    'get_memory_stats',
    'optimize_workload',
    'MemoryOptimizer',
    'memory_tracker',
]
