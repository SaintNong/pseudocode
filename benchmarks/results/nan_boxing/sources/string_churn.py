import time

def benchmark(n):
    checksum = 0
    for i in range(n):
        value = "scsa-" + str(i)
        checksum = checksum + len(value)
    return checksum

for _ in range(2):
    benchmark(50000)
checksums = []
started = time.perf_counter()
for _ in range(5):
    checksum = benchmark(50000)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
