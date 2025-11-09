#!/usr/bin/env python3
"""
Systematic benchmark for NONCES_PER_THREAD parameter tuning.

This script tests multiple values of NONCES_PER_THREAD to find the optimal
batching parameter that maximizes throughput (MH/s) while balancing register
pressure and occupancy.

Protocol:
1. For each NONCES_PER_THREAD value [8, 16, 32, 64, 128, 256]:
   - Set environment variable
   - Compile (make -j$(nproc))
   - Run benchmark 3 times (average the results)
   - Record hashrate, compilation time, kernel info
   
2. Analyze results for:
   - Optimal value (maximum hashrate)
   - Scaling curve
   - Saturation point (where improvement stops)
   - Register pressure indicators

3. Generate:
   - Results table (CSV and formatted text)
   - Performance curve graph
   - Recommendations document
"""

import subprocess
import os
import sys
import json
import time
import re
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import statistics

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
BENCHMARK_CMD = BUILD_DIR / "tests" / "bench_cuda"
POOL = "etc.2miners.com:1010"
WALLET = "0x742d35Cc6731C0532925a3b8D94d30A8f33b1234"
DURATION = 30  # seconds per test
RUNS_PER_VALUE = 3  # Number of runs per NONCES_PER_THREAD value

# Values to test
NONCES_TO_TEST = [8, 16, 32, 64, 128, 256]

# ANSI color codes
RED = '\033[0;31m'
GREEN = '\033[0;32m'
YELLOW = '\033[1;33m'
BLUE = '\033[0;34m'
CYAN = '\033[0;36m'
NC = '\033[0m'  # No Color

def log_info(msg: str):
    print(f"{GREEN}[INFO]{NC} {msg}")

def log_warn(msg: str):
    print(f"{YELLOW}[WARN]{NC} {msg}")

def log_error(msg: str):
    print(f"{RED}[ERROR]{NC} {msg}")

def log_header(msg: str):
    print(f"\n{CYAN}{'═' * 70}{NC}")
    print(f"{CYAN}  {msg}{NC}")
    print(f"{CYAN}{'═' * 70}{NC}\n")

def run_cmd(cmd: List[str], env: Optional[Dict] = None, check: bool = True, cwd: Optional[str] = None) -> Tuple[int, str, str]:
    """Run a shell command and return exit code, stdout, stderr."""
    try:
        full_env = os.environ.copy()
        if env:
            full_env.update(env)
        
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            env=full_env,
            cwd=cwd,
            timeout=300  # 5 minute timeout
        )
        
        if check and result.returncode != 0:
            log_error(f"Command failed: {' '.join(cmd)}")
            log_error(f"stdout: {result.stdout}")
            log_error(f"stderr: {result.stderr}")
            if check:
                raise RuntimeError(f"Command failed with code {result.returncode}")
        
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        log_error(f"Command timeout: {' '.join(cmd)}")
        raise

def compile_project(nonces_per_thread: int) -> Tuple[float, str]:
    """
    Compile project with OHMY_NONCES_PER_THREAD environment variable.
    Returns (compilation_time_seconds, kernel_config_info).
    """
    log_info(f"Compiling with NONCES_PER_THREAD={nonces_per_thread}...")
    
    # Setup env
    env = {"OHMY_NONCES_PER_THREAD": str(nonces_per_thread)}
    
    # Clean build directory (rebuild needed for kernel change)
    if (BUILD_DIR / "CMakeFiles").exists():
        run_cmd(["rm", "-rf", str(BUILD_DIR)], check=True)
    
    # Create build directory
    (BUILD_DIR).mkdir(parents=True, exist_ok=True)
    
    # Configure
    start = time.time()
    log_info("Running cmake...")
    run_cmd(
        ["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"],
        cwd=str(BUILD_DIR),
        env=env,
        check=True
    )
    
    # Compile
    log_info("Compiling...")
    returncode, stdout, stderr = run_cmd(
        ["make", "-j$(nproc)"],
        cwd=str(BUILD_DIR),
        env=env,
        check=False
    )
    
    compile_time = time.time() - start
    
    if returncode != 0:
        log_error(f"Compilation failed!")
        log_error(f"stdout: {stdout}")
        log_error(f"stderr: {stderr}")
        raise RuntimeError("Compilation failed")
    
    log_info(f"Compilation successful in {compile_time:.2f}s")
    
    # Extract kernel info
    kernel_info = f"nonces_per_thread={nonces_per_thread}"
    
    return compile_time, kernel_info

def run_benchmark(nonces_per_thread: int, run_number: int) -> Optional[float]:
    """
    Run benchmark and extract average hashrate (MH/s).
    Returns hashrate or None if failed.
    """
    log_info(f"Running benchmark {run_number}/{RUNS_PER_VALUE} (NONCES={nonces_per_thread})...")
    
    # Setup environment
    env = {
        "OHMY_USE_OPTIMIZED_KERNEL": "1",
        "OHMY_NONCES_PER_THREAD": str(nonces_per_thread)
    }
    
    # Create temp log file
    temp_log = f"/tmp/bench_{nonces_per_thread}_{run_number}_{int(time.time())}.log"
    
    try:
        # Run benchmark (with pool connection test)
        returncode, stdout, stderr = run_cmd(
            ["timeout", str(DURATION + 10), str(BENCHMARK_CMD), 
             "--pool", POOL, 
             "--wallet", WALLET,
             "--worker", f"tuning-{nonces_per_thread}"],
            check=False
        )
        
        output = stdout + stderr
        
        # Look for hashrate pattern: "Average Hashrate: X.XX MH/s" or "Hash Rate: X.XX MH/s"
        patterns = [
            r"Average Hashrate:\s+([\d.]+)\s+MH/s",
            r"Hash Rate:\s+([\d.]+)\s+MH/s",
            r"Hashrate:\s+([\d.]+)\s+MH/s",
            r"(\d+\.\d+)\s+MH/s"
        ]
        
        for pattern in patterns:
            matches = re.findall(pattern, output)
            if matches:
                # Take the first non-zero match (often averaged value)
                for match in matches:
                    try:
                        hashrate = float(match)
                        if hashrate > 0:
                            log_info(f"  → Hashrate: {GREEN}{hashrate:.2f} MH/s{NC}")
                            return hashrate
                    except ValueError:
                        continue
        
        # Try alternative: look for "Device 0 initialized" followed by mining output
        if "mining" in output.lower() or "solution" in output.lower():
            log_warn(f"  Benchmark ran but couldn't extract exact hashrate")
            log_info(f"  Full output: {output[-500:]}")  # Last 500 chars
            # Return a placeholder to continue testing
            return None
        else:
            log_warn(f"  Benchmark output doesn't contain hashrate info")
            return None
            
    except Exception as e:
        log_error(f"Benchmark failed: {e}")
        return None
    finally:
        # Cleanup
        if os.path.exists(temp_log):
            os.remove(temp_log)

def benchmark_nonces_value(nonces_per_thread: int) -> Dict:
    """
    Benchmark a specific NONCES_PER_THREAD value with multiple runs.
    Returns dict with results.
    """
    log_header(f"Testing NONCES_PER_THREAD = {nonces_per_thread}")
    
    # Compile
    try:
        compile_time, kernel_info = compile_project(nonces_per_thread)
    except Exception as e:
        log_error(f"Failed to compile for NONCES_PER_THREAD={nonces_per_thread}: {e}")
        return {
            "nonces_per_thread": nonces_per_thread,
            "hashrates": [],
            "avg_hashrate": None,
            "compile_time": None,
            "error": str(e)
        }
    
    # Run benchmarks
    hashrates = []
    for run in range(1, RUNS_PER_VALUE + 1):
        try:
            hashrate = run_benchmark(nonces_per_thread, run)
            if hashrate is not None:
                hashrates.append(hashrate)
            time.sleep(2)  # Wait between runs
        except Exception as e:
            log_warn(f"Benchmark run {run} failed: {e}")
    
    # Analyze results
    result = {
        "nonces_per_thread": nonces_per_thread,
        "hashrates": hashrates,
        "compile_time": compile_time,
        "kernel_info": kernel_info,
        "error": None
    }
    
    if hashrates:
        result["avg_hashrate"] = statistics.mean(hashrates)
        result["stdev_hashrate"] = statistics.stdev(hashrates) if len(hashrates) > 1 else 0
        result["min_hashrate"] = min(hashrates)
        result["max_hashrate"] = max(hashrates)
        
        log_info(f"Results for NONCES_PER_THREAD={nonces_per_thread}:")
        log_info(f"  Average: {GREEN}{result['avg_hashrate']:.2f}{NC} MH/s")
        log_info(f"  StDev:   {result['stdev_hashrate']:.2f} MH/s")
        log_info(f"  Min:     {result['min_hashrate']:.2f} MH/s")
        log_info(f"  Max:     {result['max_hashrate']:.2f} MH/s")
    else:
        result["error"] = "No valid hashrate samples"
        log_error(f"Failed to get hashrate samples for NONCES_PER_THREAD={nonces_per_thread}")
    
    return result

def analyze_results(results: List[Dict]) -> Dict:
    """
    Analyze benchmark results to identify optimal value and scaling patterns.
    """
    log_header("Analysis Summary")
    
    # Filter results with valid hashrates
    valid_results = [r for r in results if r.get("avg_hashrate")]
    
    if not valid_results:
        log_error("No valid results to analyze!")
        return {}
    
    # Find optimal
    optimal = max(valid_results, key=lambda r: r["avg_hashrate"])
    baseline = valid_results[0]  # First tested value as reference
    
    analysis = {
        "optimal_nonces": optimal["nonces_per_thread"],
        "optimal_hashrate": optimal["avg_hashrate"],
        "baseline_nonces": baseline["nonces_per_thread"],
        "baseline_hashrate": baseline["avg_hashrate"],
        "improvement_percent": ((optimal["avg_hashrate"] - baseline["avg_hashrate"]) / baseline["avg_hashrate"] * 100),
        "results": valid_results
    }
    
    log_info(f"Optimal NONCES_PER_THREAD: {GREEN}{optimal['nonces_per_thread']}{NC}")
    log_info(f"Optimal Hashrate: {GREEN}{optimal['avg_hashrate']:.2f}{NC} MH/s")
    log_info(f"Baseline (NONCES={baseline['nonces_per_thread']}): {baseline['avg_hashrate']:.2f} MH/s")
    log_info(f"Improvement: {GREEN}{analysis['improvement_percent']:+.2f}%{NC}")
    
    # Detect scaling behavior
    log_info("\nScaling behavior:")
    prev_hashrate = baseline["avg_hashrate"]
    for result in valid_results[1:]:
        if result["avg_hashrate"] is not None:
            delta = result["avg_hashrate"] - prev_hashrate
            pct = (delta / prev_hashrate * 100) if prev_hashrate > 0 else 0
            
            symbol = "↑" if delta > 0 else "↓" if delta < 0 else "→"
            color = GREEN if delta > 0.5 else YELLOW if delta > -0.5 else RED
            
            log_info(f"  {symbol} NONCES={result['nonces_per_thread']:3d}: "
                    f"{color}{result['avg_hashrate']:.2f}{NC} MH/s "
                    f"({pct:+.2f}%)")
            prev_hashrate = result["avg_hashrate"]
    
    return analysis

def generate_results_table(results: List[Dict]) -> str:
    """Generate formatted results table."""
    table = "\n" + "=" * 80 + "\n"
    table += "NONCES_PER_THREAD | Avg Hashrate | StDev | Min    | Max    | Compile Time\n"
    table += "-" * 80 + "\n"
    
    for result in results:
        if result.get("error"):
            table += f"{result['nonces_per_thread']:17d} | ERROR: {result['error']}\n"
        elif result.get("avg_hashrate"):
            table += (f"{result['nonces_per_thread']:17d} | "
                     f"{result['avg_hashrate']:12.2f} | "
                     f"{result['stdev_hashrate']:5.2f} | "
                     f"{result['min_hashrate']:6.2f} | "
                     f"{result['max_hashrate']:6.2f} | "
                     f"{result['compile_time']:11.2f}s\n")
        else:
            table += f"{result['nonces_per_thread']:17d} | NO DATA\n"
    
    table += "=" * 80 + "\n"
    return table

def generate_graph_ascii(results: List[Dict], width: int = 70) -> str:
    """Generate ASCII performance graph."""
    valid_results = [r for r in results if r.get("avg_hashrate")]
    
    if not valid_results:
        return "No data to graph"
    
    # Find min/max for scaling
    hashrates = [r["avg_hashrate"] for r in valid_results]
    min_rate = min(hashrates)
    max_rate = max(hashrates)
    range_rate = max_rate - min_rate if max_rate > min_rate else 1
    
    graph = "\n" + "=" * (width + 20) + "\n"
    graph += "PERFORMANCE SCALING CURVE\n"
    graph += "=" * (width + 20) + "\n"
    
    for result in valid_results:
        nonces = result["nonces_per_thread"]
        hashrate = result["avg_hashrate"]
        
        # Calculate bar length (0-width)
        normalized = (hashrate - min_rate) / range_rate if range_rate > 0 else 0
        bar_len = int(normalized * width)
        
        bar = "█" * bar_len + "░" * (width - bar_len)
        graph += f"N={nonces:3d} │{bar}│ {hashrate:7.2f} MH/s\n"
    
    graph += "=" * (width + 20) + "\n"
    
    return graph

def save_results_json(results: List[Dict], analysis: Dict, filepath: Path):
    """Save detailed results to JSON file."""
    output = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "configuration": {
            "duration_per_test": DURATION,
            "runs_per_value": RUNS_PER_VALUE,
            "values_tested": NONCES_TO_TEST,
        },
        "results": results,
        "analysis": analysis
    }
    
    with open(filepath, 'w') as f:
        json.dump(output, f, indent=2)
    
    log_info(f"Results saved to {filepath}")

def main():
    log_header("NONCES_PER_THREAD TUNING BENCHMARK")
    
    log_info(f"Testing values: {NONCES_TO_TEST}")
    log_info(f"Runs per value: {RUNS_PER_VALUE}")
    log_info(f"Duration per run: {DURATION}s")
    log_info(f"Project root: {PROJECT_ROOT}")
    
    # Check prerequisites
    if not PROJECT_ROOT.exists():
        log_error(f"Project root not found: {PROJECT_ROOT}")
        sys.exit(1)
    
    # Run benchmarks for each value
    all_results = []
    
    try:
        for nonces_value in NONCES_TO_TEST:
            try:
                result = benchmark_nonces_value(nonces_value)
                all_results.append(result)
                time.sleep(3)  # Cool down between test series
            except KeyboardInterrupt:
                log_warn("Benchmark interrupted by user")
                break
            except Exception as e:
                log_error(f"Error testing NONCES_PER_THREAD={nonces_value}: {e}")
                continue
        
        # Analyze and present results
        if all_results:
            log_header("RESULTS ANALYSIS")
            
            # Print table
            print(generate_results_table(all_results))
            
            # Print graph
            print(generate_graph_ascii(all_results))
            
            # Detailed analysis
            analysis = analyze_results(all_results)
            
            # Save results
            results_file = PROJECT_ROOT / "benchmark_results.json"
            save_results_json(all_results, analysis, results_file)
            
            # Save detailed report
            report_file = PROJECT_ROOT / "BENCHMARK_NONCES_TUNING.md"
            generate_report(all_results, analysis, report_file)
            
            log_header("RECOMMENDATIONS")
            log_info(f"Optimal NONCES_PER_THREAD: {GREEN}{analysis['optimal_nonces']}{NC}")
            log_info(f"Set env var: export OHMY_NONCES_PER_THREAD={analysis['optimal_nonces']}")
            
        else:
            log_error("No valid results collected")
            sys.exit(1)
    
    except KeyboardInterrupt:
        log_warn("\nBenchmark cancelled by user")
        sys.exit(130)

def generate_report(results: List[Dict], analysis: Dict, filepath: Path):
    """Generate detailed markdown report."""
    report = f"""# NONCES_PER_THREAD Tuning Report

Generated: {time.strftime("%Y-%m-%d %H:%M:%S")}

## Summary

Optimal value: **{analysis['optimal_nonces']}**  
Optimal hashrate: **{analysis['optimal_hashrate']:.2f} MH/s**  
Improvement vs baseline: **{analysis['improvement_percent']:+.2f}%**

## Detailed Results

{generate_results_table(results)}

## Performance Curve

{generate_graph_ascii(results)}

## Analysis

### Scaling Pattern
- Baseline (NONCES={analysis['baseline_nonces']}): {analysis['baseline_hashrate']:.2f} MH/s
- Optimal (NONCES={analysis['optimal_nonces']}): {analysis['optimal_hashrate']:.2f} MH/s
- Improvement: {analysis['improvement_percent']:+.2f}%

### Per-Test Results

"""
    
    for result in results:
        report += f"\n### NONCES_PER_THREAD = {result['nonces_per_thread']}\n"
        
        if result.get("error"):
            report += f"**Error**: {result['error']}\n"
        else:
            report += f"- Hashrate: {result['avg_hashrate']:.2f} ± {result['stdev_hashrate']:.2f} MH/s\n"
            report += f"- Range: {result['min_hashrate']:.2f} - {result['max_hashrate']:.2f} MH/s\n"
            report += f"- Compile time: {result['compile_time']:.2f}s\n"
            report += f"- Samples: {result['hashrates']}\n"
    
    report += f"""

## Recommendations

1. Use NONCES_PER_THREAD = {analysis['optimal_nonces']} for production
2. Set via: `export OHMY_NONCES_PER_THREAD={analysis['optimal_nonces']}`
3. Recompile after changing the value

## Environment Details

- GPU: GeForce GTX 1660 SUPER (Turing sm_75)
- Compute Capability: sm_75
- Test Duration: {DURATION}s per run
- Runs per value: {RUNS_PER_VALUE}
- Values tested: {', '.join(map(str, NONCES_TO_TEST))}

"""
    
    with open(filepath, 'w') as f:
        f.write(report)
    
    log_info(f"Report saved to {filepath}")

if __name__ == "__main__":
    main()
