import time

def benchmark(n):
    if n < 2:
        return n
    return benchmark(n - 1) + benchmark(n - 2)

for _ in range(2):
    benchmark(25)
checksums = []
started = time.perf_counter()
for _ in range(5):
    checksum = benchmark(25)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
