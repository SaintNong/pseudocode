
function transform(value) { return value * 3 + 1; }
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) checksum = checksum + transform(i - Math.trunc(i / 97) * 97);
    return checksum;
}

for (let i = 0; i < 2; i++) benchmark(100000);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 5; i++) {
    const checksum = benchmark(100000);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
