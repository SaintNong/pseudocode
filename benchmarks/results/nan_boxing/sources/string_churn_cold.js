
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        const value = "scsa-" + String(i);
        checksum = checksum + value.length;
    }
    return checksum;
}

for (let i = 0; i < 0; i++) benchmark(50000);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 1; i++) {
    const checksum = benchmark(50000);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
