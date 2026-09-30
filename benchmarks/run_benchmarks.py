#!/usr/bin/env python3
import os
import sys
import json
import time
import platform
import subprocess
import argparse
import re
import random

def get_cpu_info():
    system = platform.system()
    if system == "Linux":
        try:
            with open("/proc/cpuinfo", "r") as f:
                for line in f:
                    if "model name" in line:
                        return re.sub(r".*model name.*:\s*", "", line).strip()
        except Exception:
            pass
    elif system == "Darwin":
        try:
            return subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"]).decode().strip()
        except Exception:
            pass
    elif system == "Windows":
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            val, _ = winreg.QueryValueEx(key, "ProcessorNameString")
            return val.strip()
        except Exception:
            pass
    return platform.processor() or "Unknown CPU"

def find_scsa_binary(user_path=None):
    if user_path:
        if os.path.exists(user_path):
            return os.path.abspath(user_path)
        else:
            raise FileNotFoundError(f"Specified scsa binary not found at {user_path}")
            
    # Search common paths relative to repository root (parent of benchmarks folder)
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    possible_paths = [
        os.path.join(base_dir, "build", "scsa"),
        os.path.join(base_dir, "build", "Release", "scsa"),
        os.path.join(base_dir, "build", "Release", "scsa.exe"),
        os.path.join(base_dir, "scsa"),
        os.path.join(base_dir, "scsa.exe")
    ]
    for p in possible_paths:
        if os.path.exists(p) and os.path.isfile(p):
            if platform.system() != "Windows" and not os.access(p, os.X_OK):
                continue
            return p
            
    raise FileNotFoundError("Could not find 'scsa' binary. Please build the interpreter first.")

def generate_matrix(size):
    return [[random.randint(1, 10) for _ in range(size)] for _ in range(size)]

def multiply_matrices(A, B):
    n = len(A)
    C = [[0] * n for _ in range(n)]
    for i in range(n):
        for j in range(n):
            s = 0
            for k in range(n):
                s += A[i][k] * B[k][j]
            C[i][j] = s
    return C

def compute_checksum(C):
    return sum(sum(row) for row in C)

def run_sieve_python(n):
    is_prime = [True] * (n + 1)
    is_prime[0] = False
    is_prime[1] = False
    p = 2
    while p * p <= n:
        if is_prime[p]:
            i = p * p
            while i <= n:
                is_prime[i] = False
                i += p
        p += 1
    return sum(i for i, val in enumerate(is_prime) if val)

def fibonacci(n):
    if n < 2:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

def run_language_benchmark(name, scsa_bin, scsa_file, py_file, js_file,
                          runs, expected_checksum, run_js):
    commands = [
        ("SCSA", [scsa_bin, scsa_file]),
        ("Python", [sys.executable, py_file])
    ]
    if run_js:
        commands.append(("NodeJS", ["node", js_file]))

    results = {}
    print(f"\nRunning {name}...")
    for language, command in commands:
        print(f"\nRunning {language}...")
        results[language] = run_benchmark_runs(
            language, command, runs, expected_checksum
        )
    return results

def parse_output(stdout):
    checksum_match = re.search(r"Checksum:\s*(-?\d+)", stdout)
    if checksum_match:
        return int(checksum_match.group(1))
    return None

def run_benchmark_runs(name, cmd, runs, expected_checksum):
    times = []
    checksum_ok = True
    
    for r in range(runs):
        t0 = time.perf_counter()
        res = subprocess.run(cmd, capture_output=True, text=True)
        t1 = time.perf_counter()
        
        if res.returncode != 0:
            print(f"  Run {r+1} failed with return code {res.returncode}")
            print(f"  stderr: {res.stderr}")
            checksum_ok = False
            continue
            
        checksum = parse_output(res.stdout)
        
        if checksum != expected_checksum:
            print(f"  Run {r+1} Checksum Mismatch! Got {checksum}, expected {expected_checksum}")
            checksum_ok = False
        else:
            times.append(t1 - t0)
        
        print(f"  Run {r+1}: Time = {t1-t0:.4f}s")
        
    if times:
        return {
            "min_time": min(times),
            "avg_time": sum(times) / len(times),
            "checksum_ok": checksum_ok
        }
    else:
        return {
            "min_time": 0,
            "avg_time": 0,
            "checksum_ok": False
        }

def format_markdown_table(results, run_js):
    scsa_min = results["SCSA"]["min_time"]
    scsa_avg = results["SCSA"]["avg_time"]
    scsa_checksum = "Passed" if results["SCSA"]["checksum_ok"] else "Failed"
    
    py_min = results["Python"]["min_time"]
    py_avg = results["Python"]["avg_time"]
    py_checksum = "Passed" if results["Python"]["checksum_ok"] else "Failed"
    py_speedup = scsa_avg / py_avg if py_avg > 0 else 0
    
    if run_js and "NodeJS" in results:
        js_min = results["NodeJS"]["min_time"]
        js_avg = results["NodeJS"]["avg_time"]
        js_checksum = "Passed" if results["NodeJS"]["checksum_ok"] else "Failed"
        js_speedup = scsa_avg / js_avg if js_avg > 0 else 0
        js_row = f"| Node.js | {js_min:.4f}s | {js_avg:.4f}s | {js_checksum} | {js_speedup:.1f}x |"
    else:
        js_row = "| Node.js | N/A | N/A | Skipped/Missing | N/A |"
        
    table = f"""| Language | Min Time | Avg Time | Checksum | Relative Speed (vs SCSA) |
| --- | --- | --- | --- | --- |
| SCSA Pseudocode | {scsa_min:.4f}s | {scsa_avg:.4f}s | {scsa_checksum} | 1.0x (Baseline) |
| Python 3 | {py_min:.4f}s | {py_avg:.4f}s | {py_checksum} | {py_speedup:.1f}x |
{js_row}"""
    return table

def main():
    parser = argparse.ArgumentParser(description="Run SCSA benchmarks.")
    parser.add_argument("-s", "--size", type=int, default=60, help="Dimension N of NxN matrices (default: 60)")
    parser.add_argument("-l", "--sieve-limit", type=int, default=12000, help="Limit N for Sieve of Eratosthenes (default: 12000)")
    parser.add_argument("--sort-size", type=int, default=1000, help="Number of values to quicksort (default: 1000)")
    parser.add_argument("--fibonacci-n", type=int, default=25, help="Input to recursive Fibonacci (default: 25)")
    parser.add_argument("-r", "--runs", type=int, default=3, help="Number of runs to average (default: 3)")
    parser.add_argument("-p", "--scsa-path", type=str, default=None, help="Path to scsa interpreter binary")
    parser.add_argument("--skip-js", action="store_true", help="Skip Node.js benchmarking")
    args = parser.parse_args()

    benchmarks_dir = os.path.dirname(os.path.abspath(__file__))
    
    print(f"=== SCSA Benchmark Runner ===")
    print(f"Matrix Size: {args.size}x{args.size}")
    print(f"Sieve Limit: {args.sieve_limit}")
    print(f"Runs: {args.runs}")
    
    # Find scsa
    try:
        scsa_bin = find_scsa_binary(args.scsa_path)
        print(f"Using SCSA binary: {scsa_bin}")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
        
    # Check if node is available
    run_js = not args.skip_js
    if run_js:
        try:
            subprocess.run(["node", "--version"], capture_output=True, check=True)
            print("Using Node.js: Available")
        except Exception:
            print("Node.js: Not available (skipping JS benchmarks)")
            run_js = False
            
    # ==========================================
    # Benchmark 1: Matrix Multiplication Setup
    # ==========================================
    print("\n--- Preparing Matrix Multiplication Dataset ---")
    A = generate_matrix(args.size)
    B = generate_matrix(args.size)
    
    print("Calculating expected checksum in Python...")
    expected_C = multiply_matrices(A, B)
    expected_matrix_checksum = compute_checksum(expected_C)
    print(f"Expected Checksum: {expected_matrix_checksum}")
    
    a_str = json.dumps(A)
    b_str = json.dumps(B)
    
    scsa_matrix_file = os.path.join(benchmarks_dir, "matrix_multiply.scsa")
    py_matrix_file = os.path.join(benchmarks_dir, "matrix_multiply.py")
    js_matrix_file = os.path.join(benchmarks_dir, "matrix_multiply.js")
    
    # Generate files
    with open(scsa_matrix_file, "w") as f:
        f.write(f"""# Auto-generated matrix multiplication benchmark
A = {a_str}
B = {b_str}

FUNCTION matrix_multiply(A, B)
    n = A.length
    C = []
    FOR i = 0 TO n - 1
        row = []
        FOR j = 0 TO n - 1
            row.append(0)
        END FOR
        C.append(row)
    END FOR
    
    FOR i = 0 TO n - 1
        FOR j = 0 TO n - 1
            s = 0
            FOR k = 0 TO n - 1
                s = s + A[i][k] * B[k][j]
            END FOR
            C[i][j] = s
        END FOR
    END FOR
    
    RETURN C
END matrix_multiply

start = TIME()
C = matrix_multiply(A, B)
end = TIME()

n = A.length
checksum = 0
FOR i = 0 TO n - 1
    FOR j = 0 TO n - 1
        checksum = checksum + C[i][j]
    END FOR
END FOR

PRINT("Checksum: " + STRING(checksum))
PRINT("Execution Time: " + STRING(end - start) + "s")
""")

    with open(py_matrix_file, "w") as f:
        f.write(f"""# Auto-generated matrix multiplication benchmark
import time

A = {a_str}
B = {b_str}

def matrix_multiply(A, B):
    n = len(A)
    C = [[0] * n for _ in range(n)]
    for i in range(n):
        for j in range(n):
            s = 0
            for k in range(n):
                s += A[i][k] * B[k][j]
            C[i][j] = s
    return C

start = time.time()
C = matrix_multiply(A, B)
end = time.time()

n = len(A)
checksum = 0
for i in range(n):
    for j in range(n):
        checksum += C[i][j]

print(f"Checksum: {{checksum}}")
print(f"Execution Time: {{end - start}}s")
""")

    with open(js_matrix_file, "w") as f:
        f.write(f"""// Auto-generated matrix multiplication benchmark
const {{ performance }} = require('perf_hooks');

const A = {a_str};
const B = {b_str};

function matrixMultiply(A, B) {{
    const n = A.length;
    const C = Array.from({{ length: n }}, () => Array(n).fill(0));
    for (let i = 0; i < n; i++) {{
        for (let j = 0; j < n; j++) {{
            let s = 0;
            for (let k = 0; k < n; k++) {{
                s += A[i][k] * B[k][j];
            }}
            C[i][j] = s;
        }}
    }}
    return C;
}}

const start = performance.now();
const C = matrixMultiply(A, B);
const end = performance.now();

const n = A.length;
let checksum = 0;
for (let i = 0; i < n; i++) {{
    for (let j = 0; j < n; j++) {{
        checksum += C[i][j];
    }}
}}

console.log("Checksum: " + checksum);
console.log("Execution Time: " + ((end - start) / 1000) + "s");
""")

    # ==========================================
    # Benchmark 2: Sieve of Eratosthenes Setup
    # ==========================================
    print("\n--- Preparing Sieve of Eratosthenes Dataset ---")
    expected_sieve_checksum = run_sieve_python(args.sieve_limit)
    print(f"Expected Checksum: {expected_sieve_checksum}")
    
    scsa_sieve_file = os.path.join(benchmarks_dir, "sieve.scsa")
    py_sieve_file = os.path.join(benchmarks_dir, "sieve.py")
    js_sieve_file = os.path.join(benchmarks_dir, "sieve.js")
    
    # Generate files
    with open(scsa_sieve_file, "w") as f:
        f.write(f"""# Auto-generated sieve of Eratosthenes benchmark
# Limit: {args.sieve_limit}

FUNCTION sieve(n)
    is_prime = [TRUE] * (n + 1)
    is_prime[0] = FALSE
    is_prime[1] = FALSE
    p = 2
    WHILE p * p <= n
        IF is_prime[p] THEN
            i = p * p
            WHILE i <= n
                is_prime[i] = FALSE
                i = i + p
            END WHILE
        END IF
        p = p + 1
    END WHILE
    
    checksum = 0
    FOR i = 0 TO n
        IF is_prime[i] THEN
            checksum = checksum + i
        END IF
    END FOR
    RETURN checksum
END sieve

start = TIME()
checksum = sieve({args.sieve_limit})
end = TIME()

PRINT("Checksum: " + STRING(checksum))
PRINT("Execution Time: " + STRING(end - start) + "s")
""")

    with open(py_sieve_file, "w") as f:
        f.write(f"""# Auto-generated sieve of Eratosthenes benchmark
# Limit: {args.sieve_limit}
import time

def sieve(n):
    is_prime = [True] * (n + 1)
    is_prime[0] = False
    is_prime[1] = False
    p = 2
    while p * p <= n:
        if is_prime[p]:
            i = p * p
            while i <= n:
                is_prime[i] = False
                i += p
        p += 1
        
    checksum = 0
    for i in range(n + 1):
        if is_prime[i]:
            checksum += i
    return checksum

start = time.time()
checksum = sieve({args.sieve_limit})
end = time.time()

print(f"Checksum: {{checksum}}")
print(f"Execution Time: {{end - start}}s")
""")

    with open(js_sieve_file, "w") as f:
        f.write(f"""// Auto-generated sieve of Eratosthenes benchmark
// Limit: {args.sieve_limit}
const {{ performance }} = require('perf_hooks');

function sieve(n) {{
    const is_prime = Array(n + 1).fill(true);
    is_prime[0] = false;
    is_prime[1] = false;
    let p = 2;
    while (p * p <= n) {{
        if (is_prime[p]) {{
            let i = p * p;
            while (i <= n) {{
                is_prime[i] = false;
                i += p;
            }}
        }}
        p += 1;
    }}
    let checksum = 0;
    for (let i = 0; i <= n; i++) {{
        if (is_prime[i]) {{
            checksum += i;
        }}
    }}
    return checksum;
}}

const start = performance.now();
const checksum = sieve({args.sieve_limit});
const end = performance.now();

console.log("Checksum: " + checksum);
console.log("Execution Time: " + ((end - start) / 1000) + "s");
""")

    # ==========================================
    # Benchmark 3: Recursive Fibonacci
    # ==========================================
    print("\n--- Preparing Recursive Fibonacci Benchmark ---")
    expected_fibonacci_checksum = fibonacci(args.fibonacci_n)
    scsa_fibonacci_file = os.path.join(benchmarks_dir, "fibonacci.scsa")
    py_fibonacci_file = os.path.join(benchmarks_dir, "fibonacci.py")
    js_fibonacci_file = os.path.join(benchmarks_dir, "fibonacci.js")

    with open(scsa_fibonacci_file, "w") as f:
        f.write(f"""# Recursive Fibonacci benchmark
FUNCTION fibonacci(n)
    IF n < 2 THEN
        RETURN n
    END IF
    RETURN fibonacci(n - 1) + fibonacci(n - 2)
END fibonacci

checksum = fibonacci({args.fibonacci_n})
PRINT("Checksum: " + STRING(checksum))
""")

    with open(py_fibonacci_file, "w") as f:
        f.write(f"""# Recursive Fibonacci benchmark
def fibonacci(n):
    if n < 2:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

checksum = fibonacci({args.fibonacci_n})
print(f"Checksum: {{checksum}}")
""")

    with open(js_fibonacci_file, "w") as f:
        f.write(f"""// Recursive Fibonacci benchmark
function fibonacci(n) {{
    if (n < 2) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}}

const checksum = fibonacci({args.fibonacci_n});
console.log("Checksum: " + checksum);
""")

    # ==========================================
    # Benchmark 4: Quicksort
    # ==========================================
    print("\n--- Preparing Quicksort Benchmark ---")
    sort_rng = random.Random(20260930)
    sort_values = [sort_rng.randint(0, 1000) for _ in range(args.sort_size)]
    expected_sorted = sorted(sort_values)
    expected_sort_checksum = sum(
        (index + 1) * value for index, value in enumerate(expected_sorted)
    )
    values_literal = json.dumps(sort_values)
    scsa_sort_file = os.path.join(benchmarks_dir, "quicksort.scsa")
    py_sort_file = os.path.join(benchmarks_dir, "quicksort.py")
    js_sort_file = os.path.join(benchmarks_dir, "quicksort.js")

    with open(scsa_sort_file, "w") as f:
        f.write(f"""# Quicksort benchmark
values = {values_literal}

FUNCTION partition(items, low, high)
    pivot = items[high]
    i = low - 1
    FOR j = low TO high - 1
        IF items[j] < pivot THEN
            i = i + 1
            temp = items[i]
            items[i] = items[j]
            items[j] = temp
        END IF
    END FOR
    temp = items[i + 1]
    items[i + 1] = items[high]
    items[high] = temp
    RETURN i + 1
END partition

FUNCTION quicksort(items, low, high)
    IF low < high THEN
        pivot_index = partition(items, low, high)
        quicksort(items, low, pivot_index - 1)
        quicksort(items, pivot_index + 1, high)
    END IF
    RETURN items
END quicksort

values = quicksort(values, 0, values.length - 1)
checksum = 0
FOR i = 0 TO values.length - 1
    checksum = checksum + (i + 1) * values[i]
END FOR
PRINT("Checksum: " + STRING(checksum))
""")

    with open(py_sort_file, "w") as f:
        f.write(f"""# Quicksort benchmark
values = {values_literal}

def partition(items, low, high):
    pivot = items[high]
    i = low - 1
    for j in range(low, high):
        if items[j] < pivot:
            i += 1
            items[i], items[j] = items[j], items[i]
    items[i + 1], items[high] = items[high], items[i + 1]
    return i + 1

def quicksort(items, low, high):
    if low < high:
        pivot_index = partition(items, low, high)
        quicksort(items, low, pivot_index - 1)
        quicksort(items, pivot_index + 1, high)
    return items

values = quicksort(values, 0, len(values) - 1)
checksum = sum((i + 1) * value for i, value in enumerate(values))
print(f"Checksum: {{checksum}}")
""")

    with open(js_sort_file, "w") as f:
        f.write(f"""// Quicksort benchmark
const values = {values_literal};

function partition(items, low, high) {{
    const pivot = items[high];
    let i = low - 1;
    for (let j = low; j < high; j++) {{
        if (items[j] < pivot) {{
            i++;
            [items[i], items[j]] = [items[j], items[i]];
        }}
    }}
    [items[i + 1], items[high]] = [items[high], items[i + 1]];
    return i + 1;
}}

function quicksort(items, low, high) {{
    if (low < high) {{
        const pivotIndex = partition(items, low, high);
        quicksort(items, low, pivotIndex - 1);
        quicksort(items, pivotIndex + 1, high);
    }}
    return items;
}}

quicksort(values, 0, values.length - 1);
const checksum = values.reduce((sum, value, i) => sum + (i + 1) * value, 0);
console.log("Checksum: " + checksum);
""")

    print(f"Generated benchmark source files in {benchmarks_dir}.")
    
    # Run Matrix multiplication benchmarks
    print("\n==========================================")
    print("Running Matrix Multiplication Benchmarks")
    print("==========================================")
    
    runs_to_do_matrix = [
        ("SCSA", [scsa_bin, scsa_matrix_file]),
        ("Python", [sys.executable, py_matrix_file])
    ]
    if run_js:
        runs_to_do_matrix.append(("NodeJS", ["node", js_matrix_file]))
        
    matrix_results = {}
    for name, cmd in runs_to_do_matrix:
        print(f"\nRunning {name}...")
        matrix_results[name] = run_benchmark_runs(name, cmd, args.runs, expected_matrix_checksum)

    # Run Sieve of Eratosthenes benchmarks
    print("\n==========================================")
    print("Running Sieve of Eratosthenes Benchmarks")
    print("==========================================")
    
    runs_to_do_sieve = [
        ("SCSA", [scsa_bin, scsa_sieve_file]),
        ("Python", [sys.executable, py_sieve_file])
    ]
    if run_js:
        runs_to_do_sieve.append(("NodeJS", ["node", js_sieve_file]))
        
    sieve_results = {}
    for name, cmd in runs_to_do_sieve:
        print(f"\nRunning {name}...")
        sieve_results[name] = run_benchmark_runs(name, cmd, args.runs, expected_sieve_checksum)

    fibonacci_results = run_language_benchmark(
        "Recursive Fibonacci", scsa_bin, scsa_fibonacci_file, py_fibonacci_file,
        js_fibonacci_file, args.runs, expected_fibonacci_checksum, run_js
    )
    sort_results = run_language_benchmark(
        "Quicksort", scsa_bin, scsa_sort_file, py_sort_file,
        js_sort_file, args.runs, expected_sort_checksum, run_js
    )

    # System specs
    cpu_info = get_cpu_info()
    os_name = platform.system()
    os_release = platform.release()
    date_time = time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime())
    
    matrix_iterations = args.size ** 3
    
    markdown_content = f"""# Performance Benchmark Results

System specifications and performance results comparing the SCSA Pseudocode Interpreter to Python and Node.js.

## System Information
- **OS**: {os_name} {os_release}
- **CPU**: {cpu_info}
- **Date**: {date_time}
- **Runs**: {args.runs}

---

## Benchmark 1: Matrix Multiplication

- **Matrix Size**: {args.size}x{args.size} ({matrix_iterations:,} inner loop operations)

{format_markdown_table(matrix_results, run_js)}

---

## Benchmark 2: Sieve of Eratosthenes

- **Limit**: {args.sieve_limit:,}

{format_markdown_table(sieve_results, run_js)}

---

## Benchmark 3: Recursive Fibonacci

- **Input**: {args.fibonacci_n}

{format_markdown_table(fibonacci_results, run_js)}

---

## Benchmark 4: Quicksort

- **Array Size**: {args.sort_size:,}
- **Checksum**: Weighted sum of sorted values

{format_markdown_table(sort_results, run_js)}

---
"""

    results_md_path = os.path.join(benchmarks_dir, "README.md")
    with open(results_md_path, "w") as f:
        f.write(markdown_content)
        
    print(f"\nBenchmark completed successfully! Results written to {results_md_path}")

if __name__ == "__main__":
    main()
