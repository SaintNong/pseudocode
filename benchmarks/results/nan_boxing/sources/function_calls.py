import time

def transform(value):
    return value * 3 + 1
def benchmark(n):
    checksum = 0
    for i in range(n):
        checksum = checksum + transform(i - int(i / 97) * 97)
    return checksum

for _ in range(2):
    benchmark(100000)
checksums = []
started = time.perf_counter()
for _ in range(5):
    checksum = benchmark(100000)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
