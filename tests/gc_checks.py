"""Exercise observable GC behavior by running SCSA programs, without C++ test code."""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


GARBAGE = """
FUNCTION garbage(n)
    checksum = 0
    FOR i = 1 TO n
        values = []
        values.append(values)
        dict = {}
        dict["array"] = values
        values.append(dict)
        checksum = checksum + values.length
    END FOR
    RETURN checksum
END garbage
"""


def execute(binary, source, threshold=None, repl=False, stress=False):
    environment = os.environ.copy()
    for key in ("SCSA_GC_STRESS", "SCSA_GC_THRESHOLD"):
        environment.pop(key, None)
    environment["SCSA_GC_STATS"] = "1"
    environment["NO_COLOR"] = "1"
    if threshold is not None:
        environment["SCSA_GC_THRESHOLD"] = str(threshold)
    if stress:
        environment["SCSA_GC_STRESS"] = "1"
    with tempfile.TemporaryDirectory(prefix="scsa-gc-test-") as directory:
        path = Path(directory) / "test.scsa"
        path.write_text(source)
        result = subprocess.run([binary] if repl else [binary, str(path)],
                                input=source if repl else None, capture_output=True,
                                text=True, env=environment, timeout=30)
    if result.returncode:
        raise AssertionError(f"Interpreter failed:\n{result.stdout}\n{result.stderr}")
    match = re.search(r"GC: (.*)", result.stderr)
    if not match:
        raise AssertionError(f"Missing GC diagnostics: {result.stderr}")
    stats = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", match.group(1))}
    return result, stats


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    for threshold in (4096, 65536):
        result, stats = execute(binary, GARBAGE + '\nPRINT(garbage(5000))\n', threshold)
        check(result.stdout.strip() == "10000", "cyclic program checksum")
        check(stats["collections"] > 0, "collector ran automatically")
        check(stats["reclaimed"] >= 15000, "unreachable cycles actually reclaimed")
        check(stats["live_objects"] < 1000, "cyclic garbage remains bounded")
        print(f"Cyclic reclamation at threshold {threshold}: passed")

    # A reachable chain survives collection; its deletion and marking cannot recurse in C++.
    source = GARBAGE + """
FUNCTION chain(n)
    root = []
    cursor = root
    FOR i = 1 TO n
        child = []
        cursor.append(child)
        cursor = child
    END FOR
    RETURN root
END chain
root = chain(100000)
PRINT(root[0].length)
root = 0
PRINT(garbage(60000))
"""
    result, stats = execute(binary, source)
    check(result.stdout.strip() == "1\n120000", "reachable deep chain survived")
    check(stats["reclaimed"] >= 100000, "deep chain reclaimed after dropping root")
    check(stats["live_objects"] < 20000, "deep graph does not remain rooted")
    print("100,000-edge graph survival and reclamation: passed")

    # Repeated REPL errors must unwind the VM before later compilation/collection.
    source = """
FUNCTION fail()
    values = ["temporary"]
    RETURN values[10]
END fail
""" + "fail()\n" * 20 + 'PRINT("recovered")\nexit\n'
    result, stats = execute(binary, source, repl=True, stress=True)
    check("recovered" in result.stdout, "REPL executed after repeated runtime errors")
    check(result.stderr.count("Array index out of bounds") == 20,
          "all failing REPL calls raised the expected error")
    check(stats["reclaimed"] >= 20, "failed executions did not retain temporary arrays")
    print("REPL error recovery under forced GC: passed")


if __name__ == "__main__":
    main()
