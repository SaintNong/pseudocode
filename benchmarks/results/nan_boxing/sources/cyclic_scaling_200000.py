import time

def benchmark(n):
    checksum = 0
    for i in range(n):
        values = []
        values.append(values)
        dictionary = {}
        dictionary["array"] = values
        values.append(dictionary)
        checksum = checksum + len(values)
    return checksum

for _ in range(0):
    benchmark(200000)
checksums = []
started = time.perf_counter()
for _ in range(1):
    checksum = benchmark(200000)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
