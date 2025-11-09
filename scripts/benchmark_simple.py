#!/usr/bin/env python3
"""
Simplified NONCES_PER_THREAD tuning benchmark.

Tests values [8, 16, 32, 64, 128, 256] to find optimal batching parameter.
"""

import subprocess
import os
import sys
import re
import time
from pathlib import Path
from typing import Dict, List, Optional

PROJECT_ROOT = Path("/home/regis/develop/ohmy-miner-etc")
BUILD_DIR = PROJECT_ROOT / "build"

NONCES_TO_TEST = [8, 16, 32, 64, 128, 256]
RUNS_PER_VALUE = 2  # Reduced to 2 for faster iteration
BENCHMARK_TIMEOUT = 45  # seconds

# Colors
GREEN = '\033[0;32m'
YELLOW = '\033[1;33m'
RED = '\033[0;31m'
CYAN = '\033[0;36m'
NC = '\033[0m'

def log_info(msg: str):
    print(f"{GREEN}[INFO]{NC} {msg}")

def log_error(msg: str):
    print(f"{RED}[ERROR]{NC} {msg}")

def log_header(msg: str):
    print(f"\n{CYAN}{'='*70}{NC}\n{CYAN}  {msg}{NC}\n{CYAN}{'='*70}{NC}\n")

def run_cmd(cmd: str, cwd: Optional[Path] = None) -> tuple[int, str]:
    """Execute command, return (returncode, output)."""
    try:
        result = subprocess.run(
            cmd,
            shell=True,
            cwd=str(cwd) if cwd else None,
            capture_output=True,
            text=True,
            timeout=300
        )
        return result.returncode, result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return -1, "TIMEOUT"
    except Exception as e:
        return -2, str(e)

def compile_for_nonces(nonces: int) -> bool:
    """Compile with specific NONCES_PER_THREAD value."""
    log_info(f"Compiling for NONCES_PER_THREAD={nonces}...")
    
    # Clean old build
    if BUILD_DIR.exists():
        run_cmd(f"rm -rf {BUILD_DIR}")
    
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    
    # Build
    cmd = (f"cd {BUILD_DIR} && "
           f"OHMY_NONCES_PER_THREAD={nonces} "
           f"cmake .. -DCMAKE_BUILD_TYPE=Release && "
           f"make -j$(nproc)")
    
    rc, output = run_cmd(cmd)
    
    if rc != 0:
        log_error(f"Compilation failed!")
        print(output[-500:])  # Last 500 chars
        return False
    
    log_info(f"Compilation OK")
    return True

def run_single_benchmark(nonces: int, run_num: int) -> Optional[float]:
    """Run benchmark, extract hashrate."""
    log_info(f"Benchmark {run_num}/2 for NONCES={nonces}...")
    
    # Use bench_cuda directly
    benchmark_bin = BUILD_DIR / "tests" / "bench_cuda"
    if not benchmark_bin.exists():
        log_error(f"Benchmark binary not found: {benchmark_bin}")
        return None
    
    cmd = (f"OHMY_USE_OPTIMIZED_KERNEL=1 "
           f"OHMY_NONCES_PER_THREAD={nonces} "
           f"timeout {BENCHMARK_TIMEOUT} {benchmark_bin}")
    
    rc, output = run_cmd(cmd)
    
    # Extract hashrate
    patterns = [
        r"Average Hashrate:\s+([\d.]+)\s+MH/s",
        r"Hash Rate:\s+([\d.]+)\s+MH/s",
        r"(\d+\.\d+)\s+MH/s"
    ]
    
    for pattern in patterns:
        matches = re.findall(pattern, output)
        if matches:
            for match in matches:
                try:
                    hashrate = float(match)
                    if hashrate > 0:
                        log_info(f"  → {GREEN}{hashrate:.2f}{NC} MH/s")
                        return hashrate
                except ValueError:
                    pass
    
    log_error(f"Could not extract hashrate")
    return None

def benchmark_nonces(nonces: int) -> Optional[float]:
    """Benchmark value with 2 runs, return average."""
    if not compile_for_nonces(nonces):
        return None
    
    hashrates = []
    for run in range(1, RUNS_PER_VALUE + 1):
        hr = run_single_benchmark(nonces, run)
        if hr:
            hashrates.append(hr)
        time.sleep(1)
    
    if not hashrates:
        log_error(f"No valid hashrates for NONCES={nonces}")
        return None
    
    avg = sum(hashrates) / len(hashrates)
    log_info(f"NONCES={nonces}: Average {GREEN}{avg:.2f}{NC} MH/s")
    
    return avg

def main():
    log_header("NONCES_PER_THREAD TUNING")
    
    log_info(f"Testing: {NONCES_TO_TEST}")
    log_info(f"Runs per value: {RUNS_PER_VALUE}")
    log_info(f"Timeout per run: {BENCHMARK_TIMEOUT}s")
    
    results = {}
    
    for nonces in NONCES_TO_TEST:
        try:
            log_header(f"NONCES_PER_THREAD = {nonces}")
            avg_hashrate = benchmark_nonces(nonces)
            if avg_hashrate:
                results[nonces] = avg_hashrate
            time.sleep(2)
        except KeyboardInterrupt:
            log_error("Interrupted")
            break
        except Exception as e:
            log_error(f"Error: {e}")
            continue
    
    # Print summary
    if not results:
        log_error("No results collected!")
        return 1
    
    log_header("RESULTS SUMMARY")
    
    # Table
    print("NONCES | Hashrate (MH/s)")
    print("------|----------------")
    for nonces, hr in sorted(results.items()):
        print(f"{nonces:5d} | {hr:14.2f}")
    
    # Find optimal
    optimal_nonces = max(results.items(), key=lambda x: x[1])
    baseline_nonces = NONCES_TO_TEST[0]
    
    log_info(f"\nOptimal: NONCES={GREEN}{optimal_nonces[0]}{NC} ({optimal_nonces[1]:.2f} MH/s)")
    
    if baseline_nonces in results:
        baseline_hr = results[baseline_nonces]
        improvement = ((optimal_nonces[1] - baseline_hr) / baseline_hr) * 100
        log_info(f"Baseline (NONCES={baseline_nonces}): {baseline_hr:.2f} MH/s")
        log_info(f"Improvement: {GREEN}{improvement:+.2f}%{NC}")
    
    print(f"\n{YELLOW}Recommendation: export OHMY_NONCES_PER_THREAD={optimal_nonces[0]}{NC}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
