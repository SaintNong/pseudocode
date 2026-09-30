
function benchmark(n) {
    const values = new Map();
    for (let i = 0; i < n; i++) values.set(i, i);
    let checksum = 0;
    for (let i = 0; i < n; i++) if (values.has(i)) checksum = checksum + values.get(i);
    return checksum;
}

for (let i = 0; i < 0; i++) benchmark(40000);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 1; i++) {
    const checksum = benchmark(40000);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
