import time

def partition(items, low, high):
    pivot = items[high]
    i = low - 1
    for j in range(low, high):
        if items[j] < pivot:
            i = i + 1
            temp = items[i]
            items[i] = items[j]
            items[j] = temp
    temp = items[i + 1]
    items[i + 1] = items[high]
    items[high] = temp
    return i + 1
def quicksort(items, low, high):
    if low < high:
        pivot = partition(items, low, high)
        quicksort(items, low, pivot - 1)
        quicksort(items, pivot + 1, high)
    return items
def benchmark(n):
    items = []
    for i in range(n):
        items.append(i * 7919 + 17 - int((i * 7919 + 17) / 10007) * 10007)
    quicksort(items, 0, n - 1)
    checksum = 0
    for i in range(n):
        checksum = checksum + (i + 1) * items[i]
        if checksum >= 1000000007:
            checksum = checksum - 1000000007
    return checksum

for _ in range(0):
    benchmark(8000)
checksums = []
started = time.perf_counter()
for _ in range(1):
    checksum = benchmark(8000)
    checksums.append(checksum)
elapsed = time.perf_counter() - started
for checksum in checksums:
    print("Checksum:", checksum)
print("BatchKernel:", elapsed)
