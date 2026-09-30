
class Counter {
    constructor(value) { this.value = value; }
    bump() { this.value = this.value + 1; return this.value; }
}
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        const counter = new Counter(i);
        checksum = checksum + counter.bump();
    }
    return checksum;
}

for (let i = 0; i < 0; i++) benchmark(30000);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 1; i++) {
    const checksum = benchmark(30000);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
