import time

def benchmark(n):
    values = {}
    for i in range(n):
        values[i] = i
    checksum = 0
    for i in range(n):
        if i in values:
            checksum = checksum + values[i]
    return checksum

for _ in range(0):
    benchmark(40000)
checksums = []
started = time.perf_counter()
for _ in range(1):
    checksum = benchmark(40000)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
