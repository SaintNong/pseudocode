import time

def benchmark(n):
    primes = [True] * (n + 1)
    primes[0] = primes[1] = False
    p = 2
    while p * p <= n:
        if primes[p]:
            i = p * p
            while i <= n:
                primes[i] = False
                i = i + p
        p = p + 1
    checksum = 0
    for i in range(n + 1):
        if primes[i]:
            checksum = checksum + i
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
