
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        checksum = checksum + i;
        if (checksum >= 1000003) checksum = checksum - 1000003;
    }
    return checksum;
}

for (let i = 0; i < 2; i++) benchmark(500000);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 5; i++) {
    const checksum = benchmark(500000);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
