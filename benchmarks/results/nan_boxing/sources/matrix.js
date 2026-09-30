
function benchmark(n) {
    const a = [], b = [];
    for (let i = 0; i < n; i++) {
        const rowA = [], rowB = [];
        for (let j = 0; j < n; j++) {
            rowA.push(i * 7 + j * 3 - Math.trunc((i * 7 + j * 3) / 10) * 10 + 1);
            rowB.push(i * 5 + j * 9 - Math.trunc((i * 5 + j * 9) / 10) * 10 + 1);
        }
        a.push(rowA); b.push(rowB);
    }
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        for (let j = 0; j < n; j++) {
            let value = 0;
            for (let k = 0; k < n; k++) value = value + a[i][k] * b[k][j];
            checksum = checksum + value;
        }
    }
    return checksum;
}

for (let i = 0; i < 2; i++) benchmark(80);
const checksums = [];
const started = process.hrtime.bigint();
for (let i = 0; i < 5; i++) {
    const checksum = benchmark(80);
    checksums.push(checksum);
}
const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
for (const checksum of checksums) console.log("Checksum:", checksum);
console.log("BatchKernel:", elapsed);
