"""
Universal RAM Optimization Layer - Python Integration

This package provides Python bindings for the UROAM system, allowing
AI frameworks like PyTorch, TensorFlow, and ONNX Runtime to integrate
with the RAM optimization layer.

Usage:
    import uroam

    # Initialize the optimization layer
    uroam.init()

    # Allocate optimized memory
    buffer = uroam.allocate(1024 * 1024)  # 1 MB

    # Optimize for AI workload
    with uroam.ai_context():
        # AI operations here
        model = load_model()
        result = model.infer(data)

    # Cleanup
    uroam.cleanup()
"""

__version__ = "0.1.0"

# Import core functionality
from . import core
from .core import (
    init,
    cleanup,
    allocate,
    free,
    allocate_aligned,
    allocate_hugepage,
    register_memory,
    unregister_memory,
    ai_context,
    inference_context,
    training_context,
    gaming_context,
    get_memory_stats,
    get_process_stats,
    set_priority,
    set_workload_type,
    enable_optimization,
    disable_optimization,
    is_enabled,
    get_config,
    set_config,
)

# Framework-specific submodules
# from . import pytorch
# from . import tensorflow
# from . import onnx

# Re-export common items
__all__ = [
    "init",
    "cleanup",
    "allocate",
    "free",
    "allocate_aligned",
    "allocate_hugepage",
    "register_memory",
    "unregister_memory",
    "ai_context",
    "inference_context",
    "training_context",
    "gaming_context",
    "get_memory_stats",
    "get_process_stats",
    "set_priority",
    "set_workload_type",
    "enable_optimization",
    "disable_optimization",
    "is_enabled",
    "get_config",
    "set_config",
]
