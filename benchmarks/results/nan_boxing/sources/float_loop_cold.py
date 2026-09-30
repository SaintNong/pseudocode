import time

def benchmark(n):
    value = 0.0
    for i in range(n):
        value = value + 0.5
    return int(value)

for _ in range(0):
    benchmark(500000)
checksums = []
started = time.perf_counter()
for _ in range(1):
    checksum = benchmark(500000)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
