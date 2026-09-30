
function benchmark(n) {
    const primes = Array(n + 1).fill(true);
    primes[0] = primes[1] = false;
    let p = 2;
    while (p * p <= n) {
        if (primes[p]) {
            let i = p * p;
            while (i <= n) { primes[i] = false; i = i + p; }
        }
        p = p + 1;
    }
    let checksum = 0;
    for (let i = 0; i <= n; i++) if (primes[i]) checksum = checksum + i;
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
