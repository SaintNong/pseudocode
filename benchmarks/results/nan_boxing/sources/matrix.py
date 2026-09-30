import time

def benchmark(n):
    a, b = [], []
    for i in range(n):
        row_a, row_b = [], []
        for j in range(n):
            row_a.append(i * 7 + j * 3 - int((i * 7 + j * 3) / 10) * 10 + 1)
            row_b.append(i * 5 + j * 9 - int((i * 5 + j * 9) / 10) * 10 + 1)
        a.append(row_a)
        b.append(row_b)
    checksum = 0
    for i in range(n):
        for j in range(n):
            value = 0
            for k in range(n):
                value = value + a[i][k] * b[k][j]
            checksum = checksum + value
    return checksum

for _ in range(2):
    benchmark(80)
checksums = []
started = time.perf_counter()
for _ in range(5):
    checksum = benchmark(80)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
