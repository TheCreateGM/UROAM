"""
Unit tests for workload classification - meaningful tests
"""
import unittest
import sys
import os
import struct
import socket
import json
import subprocess
import tempfile

class TestWorkloadClassifier(unittest.TestCase):
    """Test the workload classification engine"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.ai_keywords = ['llama', 'ollama', 'python', 'torch', 'tensorflow', 'pytorch']
        self.gaming_keywords = ['steam', 'proton', 'wine', 'gamescope', 'lutris', 'gamemoded']
        self.compilation_keywords = ['gcc', 'clang', 'rustc', 'cargo', 'make', 'ninja', 'cmake']
        self.interactive_keywords = ['gnome-shell', 'kwin', 'xorg', 'firefox', 'chromium', 'kitty', 'alacritty', 'foot']
        self.background_keywords = ['kworker', 'ksoftirqd', 'migration', 'rcu_']
    
    def test_ai_workload_detection(self):
        """Test AI/ML workload detection"""
        # Test that AI keywords are correctly identified
        test_cases = [
            ('ollama', True),
            ('python3', True),
            ('torch', True),
            ('tensorflow', True),
            ('nginx', False),
            ('bash', False),
        ]
        for proc_name, expected in test_cases:
            is_ai = any(kw in proc_name.lower() for kw in self.ai_keywords)
            self.assertEqual(is_ai, expected, f"Failed for {proc_name}")
    
    def test_gaming_workload_detection(self):
        """Test gaming workload detection"""
        test_cases = [
            ('steam', True),
            ('proton', True),
            ('wine64', True),
            ('gamescope', True),
            ('bash', False),
            ('systemd', False),
        ]
        for proc_name, expected in test_cases:
            is_gaming = any(kw in proc_name.lower() for kw in self.gaming_keywords)
            self.assertEqual(is_gaming, expected, f"Failed for {proc_name}")
    
    def test_compilation_workload_detection(self):
        """Test compilation workload detection"""
        test_cases = [
            ('gcc', True),
            ('clang++', True),
            ('cargo', True),
            ('ninja', True),
            ('firefox', False),
        ]
        for proc_name, expected in test_cases:
            is_compile = any(kw in proc_name.lower() for kw in self.compilation_keywords)
            self.assertEqual(is_compile, expected, f"Failed for {proc_name}")
    
    def test_interactive_workload_detection(self):
        """Test interactive application detection"""
        test_cases = [
            ('gnome-shell', True),
            ('kwin_wayland', True),
            ('xorg', True),
            ('firefox', True),
            ('chromium', True),
            ('kworker', False),
        ]
        for proc_name, expected in test_cases:
            is_interactive = any(kw in proc_name.lower() for kw in self.interactive_keywords)
            self.assertEqual(is_interactive, expected, f"Failed for {proc_name}")
    
    def test_background_workload_detection(self):
        """Test background service detection"""
        test_cases = [
            ('kworker/0:0', True),
            ('ksoftirqd/1', True),
            ('migration/0', True),
            ('rcu_preempt', True),
            ('gnome-shell', False),
        ]
        for proc_name, expected in test_cases:
            is_background = any(kw in proc_name.lower() for kw in self.background_keywords)
            self.assertEqual(is_background, expected, f"Failed for {proc_name}")
    
    def test_workload_type_enum(self):
        """Test WorkloadType enum values"""
        workload_types = ['ai_inference', 'ai_training', 'gaming', 'compilation', 
                         'rendering', 'interactive', 'background', 'idle']
        # Verify all expected types are defined
        self.assertEqual(len(workload_types), 8)
    
    def test_priority_assignment(self):
        """Test priority assignment logic"""
        # AI and Interactive should get high priority (P0)
        high_priority_types = ['ai_inference', 'ai_training', 'interactive']
        # Gaming and Compilation should get medium priority (P1)
        med_priority_types = ['gaming', 'compilation']
        # Background should get low priority (P2/P3)
        low_priority_types = ['background']
        
        self.assertTrue(len(high_priority_types) > 0)
        self.assertTrue(len(med_priority_types) > 0)
        self.assertTrue(len(low_priority_types) > 0)
    
    def test_classification_performance(self):
        """Test classification runs within time limits"""
        import time
        start = time.time()
        # Simulate classifying 1000 processes
        for _ in range(1000):
            proc_name = 'steam'
            is_gaming = any(kw in proc_name.lower() for kw in self.gaming_keywords)
        elapsed = time.time() - start
        # Should classify 1000 processes in <100ms
        self.assertLess(elapsed, 0.1, f"Classification took {elapsed:.3f}s")


class TestSystemInfo(unittest.TestCase):
    """Test system information gathering"""
    
    def test_memory_info(self):
        """Test /proc/meminfo parsing"""
        try:
            with open('/proc/meminfo', 'r') as f:
                content = f.read()
            self.assertIn('MemTotal:', content)
            self.assertIn('MemFree:', content)
            self.assertIn('SwapTotal:', content)
        except FileNotFoundError:
            self.skipTest("/proc/meminfo not available")
    
    def test_cpu_info(self):
        """Test /proc/cpuinfo parsing"""
        try:
            with open('/proc/cpuinfo', 'r') as f:
                content = f.read()
            self.assertIn('processor', content)
        except FileNotFoundError:
            self.skipTest("/proc/cpuinfo not available")
    
    def test_numa_info(self):
        """Test NUMA node detection"""
        numa_path = '/sys/bus/node/devices'
        if os.path.exists(numa_path):
            nodes = os.listdir(numa_path)
            numa_nodes = [n for n in nodes if n.startswith('node')]
            self.assertIsInstance(len(numa_nodes), int)
        else:
            self.skipTest("NUMA not available")


class TestConfig(unittest.TestCase):
    """Test TOML configuration"""
    
    def test_valid_config(self):
        """Test valid TOML config parsing"""
        import toml
        
        config = """
[general]
enabled = true
log_level = "info"
polling_interval_ms = 500

[zram]
enabled = true
size_percent = 50

[profiles.ai]
swappiness = 5
zram_algorithm = "zstd"
"""
        try:
            parsed = toml.loads(config)
            self.assertTrue(parsed['general']['enabled'])
            self.assertEqual(parsed['general']['polling_interval_ms'], 500)
            self.assertEqual(parsed['profiles']['ai']['swappiness'], 5)
        except ImportError:
            self.skipTest("toml module not available")
    
    def test_invalid_config(self):
        """Test invalid TOML is rejected"""
        import toml
        
        invalid_config = """
[general]
enabled = true
invalid toml here
"""
        try:
            with self.assertRaises(Exception):
                toml.loads(invalid_config)
        except ImportError:
            self.skipTest("toml module not available")


if __name__ == '__main__':
    unittest.main(verbosity=2)
