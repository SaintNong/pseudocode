
function partition(items, low, high) {
    const pivot = items[high];
    let i = low - 1;
    for (let j = low; j < high; j++) {
        if (items[j] < pivot) {
            i = i + 1;
            const temp = items[i]; items[i] = items[j]; items[j] = temp;
        }
    }
    const temp = items[i + 1]; items[i + 1] = items[high]; items[high] = temp;
    return i + 1;
}
function quicksort(items, low, high) {
    if (low < high) {
        const pivot = partition(items, low, high);
        quicksort(items, low, pivot - 1);
        quicksort(items, pivot + 1, high);
    }
    return items;
}
function benchmark(n) {
    const items = [];
    for (let i = 0; i < n; i++) items.push(i * 7919 + 17 - Math.trunc((i * 7919 + 17) / 10007) * 10007);
    quicksort(items, 0, n - 1);
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        checksum = checksum + (i + 1) * items[i];
        if (checksum >= 1000000007) checksum = checksum - 1000000007;
    }
    return checksum;
}

for (let i = 0; i < 0; i++) benchmark(8000);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 1; i++) {
    const checksum = benchmark(8000);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
