import time

class Counter:
    def __init__(self, value):
        self.value = value
    def bump(self):
        self.value = self.value + 1
        return self.value
def benchmark(n):
    checksum = 0
    for i in range(n):
        counter = Counter(i)
        checksum = checksum + counter.bump()
    return checksum

for _ in range(0):
    benchmark(30000)
checksums = []
started = time.perf_counter()
for _ in range(1):
    checksum = benchmark(30000)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
