
function benchmark(n) {
    if (n < 2) return n;
    return benchmark(n - 1) + benchmark(n - 2);
}

for (let i = 0; i < 0; i++) benchmark(25);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 1; i++) {
    const checksum = benchmark(25);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
