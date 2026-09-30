"""Equivalent, deterministic workloads for the VM, tree walker, CPython and V8.

Each workload returns a checksum. Timers and repetitions are supplied by the runner.
Keep language-specific implementations together so differences are reviewable.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Workload:
    name: str
    size: int
    expected: int
    scsa: str
    python: str
    javascript: str


def workloads():
    matrix_size = 80
    a = [[(i * 7 + j * 3) % 10 + 1 for j in range(matrix_size)]
         for i in range(matrix_size)]
    b = [[(i * 5 + j * 9) % 10 + 1 for j in range(matrix_size)]
         for i in range(matrix_size)]
    matrix_checksum = sum(sum(a[i][k] * b[k][j] for k in range(matrix_size))
                          for i in range(matrix_size) for j in range(matrix_size))
    yield Workload("matrix", matrix_size, matrix_checksum, """
FUNCTION benchmark(n)
    a = []
    b = []
    FOR i = 0 TO n - 1
        rowA = []
        rowB = []
        FOR j = 0 TO n - 1
            rowA.append(i * 7 + j * 3 - INT((i * 7 + j * 3) / 10) * 10 + 1)
            rowB.append(i * 5 + j * 9 - INT((i * 5 + j * 9) / 10) * 10 + 1)
        END FOR
        a.append(rowA)
        b.append(rowB)
    END FOR
    checksum = 0
    FOR i = 0 TO n - 1
        FOR j = 0 TO n - 1
            value = 0
            FOR k = 0 TO n - 1
                value = value + a[i][k] * b[k][j]
            END FOR
            checksum = checksum + value
        END FOR
    END FOR
    RETURN checksum
END benchmark
""", """
def benchmark(n):
    a, b = [], []
    for i in range(n):
        row_a, row_b = [], []
        for j in range(n):
            row_a.append(i * 7 + j * 3 - int((i * 7 + j * 3) / 10) * 10 + 1)
            row_b.append(i * 5 + j * 9 - int((i * 5 + j * 9) / 10) * 10 + 1)
        a.append(row_a)
        b.append(row_b)
    checksum = 0
    for i in range(n):
        for j in range(n):
            value = 0
            for k in range(n):
                value = value + a[i][k] * b[k][j]
            checksum = checksum + value
    return checksum
""", """
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
""")

    limit = 100000
    primes = [True] * (limit + 1)
    primes[0] = primes[1] = False
    for p in range(2, int(limit ** 0.5) + 1):
        if primes[p]:
            for i in range(p * p, limit + 1, p):
                primes[i] = False
    yield Workload("sieve", limit, sum(i for i, prime in enumerate(primes) if prime), """
FUNCTION benchmark(n)
    primes = [TRUE] * (n + 1)
    primes[0] = FALSE
    primes[1] = FALSE
    p = 2
    WHILE p * p <= n
        IF primes[p] THEN
            i = p * p
            WHILE i <= n
                primes[i] = FALSE
                i = i + p
            END WHILE
        END IF
        p = p + 1
    END WHILE
    checksum = 0
    FOR i = 0 TO n
        IF primes[i] THEN
            checksum = checksum + i
        END IF
    END FOR
    RETURN checksum
END benchmark
""", """
def benchmark(n):
    primes = [True] * (n + 1)
    primes[0] = primes[1] = False
    p = 2
    while p * p <= n:
        if primes[p]:
            i = p * p
            while i <= n:
                primes[i] = False
                i = i + p
        p = p + 1
    checksum = 0
    for i in range(n + 1):
        if primes[i]:
            checksum = checksum + i
    return checksum
""", """
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
""")

    yield Workload("fibonacci", 25, 75025, """
FUNCTION benchmark(n)
    IF n < 2 THEN
        RETURN n
    END IF
    RETURN benchmark(n - 1) + benchmark(n - 2)
END benchmark
""", """
def benchmark(n):
    if n < 2:
        return n
    return benchmark(n - 1) + benchmark(n - 2)
""", """
function benchmark(n) {
    if (n < 2) return n;
    return benchmark(n - 1) + benchmark(n - 2);
}
""")

    sort_size = 8000
    items = sorted((i * 7919 + 17) % 10007 for i in range(sort_size))
    yield Workload("quicksort", sort_size,
                   sum((i + 1) * value for i, value in enumerate(items)) % 1000000007, """
FUNCTION partition(items, low, high)
    pivot = items[high]
    i = low - 1
    FOR j = low TO high - 1
        IF items[j] < pivot THEN
            i = i + 1
            temp = items[i]
            items[i] = items[j]
            items[j] = temp
        END IF
    END FOR
    temp = items[i + 1]
    items[i + 1] = items[high]
    items[high] = temp
    RETURN i + 1
END partition
FUNCTION quicksort(items, low, high)
    IF low < high THEN
        pivot = partition(items, low, high)
        quicksort(items, low, pivot - 1)
        quicksort(items, pivot + 1, high)
    END IF
    RETURN items
END quicksort
FUNCTION benchmark(n)
    items = []
    FOR i = 0 TO n - 1
        items.append(i * 7919 + 17 - INT((i * 7919 + 17) / 10007) * 10007)
    END FOR
    quicksort(items, 0, n - 1)
    checksum = 0
    FOR i = 0 TO n - 1
        checksum = checksum + (i + 1) * items[i]
        IF checksum >= 1000000007 THEN
            checksum = checksum - 1000000007
        END IF
    END FOR
    RETURN checksum
END benchmark
""", """
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
""", """
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
""")

    n = 500000
    yield Workload("integer_loop", n, n * (n - 1) // 2 % 1000003, """
FUNCTION benchmark(n)
    checksum = 0
    FOR i = 0 TO n - 1
        checksum = checksum + i
        IF checksum >= 1000003 THEN
            checksum = checksum - 1000003
        END IF
    END FOR
    RETURN checksum
END benchmark
""", """
def benchmark(n):
    checksum = 0
    for i in range(n):
        checksum = checksum + i
        if checksum >= 1000003:
            checksum = checksum - 1000003
    return checksum
""", """
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        checksum = checksum + i;
        if (checksum >= 1000003) checksum = checksum - 1000003;
    }
    return checksum;
}
""")
    yield Workload("float_loop", n, n // 2, """
FUNCTION benchmark(n)
    value = 0.0
    FOR i = 0 TO n - 1
        value = value + 0.5
    END FOR
    RETURN INT(value)
END benchmark
""", """
def benchmark(n):
    value = 0.0
    for i in range(n):
        value = value + 0.5
    return int(value)
""", """
function benchmark(n) {
    let value = 0.0;
    for (let i = 0; i < n; i++) value = value + 0.5;
    return Math.trunc(value);
}
""")

    n = 50000
    yield Workload("string_churn", n, sum(len("scsa-" + str(i)) for i in range(n)), """
FUNCTION benchmark(n)
    checksum = 0
    FOR i = 0 TO n - 1
        value = "scsa-" + STRING(i)
        checksum = checksum + value.length
    END FOR
    RETURN checksum
END benchmark
""", """
def benchmark(n):
    checksum = 0
    for i in range(n):
        value = "scsa-" + str(i)
        checksum = checksum + len(value)
    return checksum
""", """
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        const value = "scsa-" + String(i);
        checksum = checksum + value.length;
    }
    return checksum;
}
""")

    n = 40000
    yield Workload("dictionary_ops", n, n * (n - 1) // 2, """
FUNCTION benchmark(n)
    values = {}
    FOR i = 0 TO n - 1
        values[i] = i
    END FOR
    checksum = 0
    FOR i = 0 TO n - 1
        IF i IN values THEN
            checksum = checksum + values[i]
        END IF
    END FOR
    RETURN checksum
END benchmark
""", """
def benchmark(n):
    values = {}
    for i in range(n):
        values[i] = i
    checksum = 0
    for i in range(n):
        if i in values:
            checksum = checksum + values[i]
    return checksum
""", """
function benchmark(n) {
    const values = new Map();
    for (let i = 0; i < n; i++) values.set(i, i);
    let checksum = 0;
    for (let i = 0; i < n; i++) if (values.has(i)) checksum = checksum + values.get(i);
    return checksum;
}
""")

    n = 100000
    yield Workload("function_calls", n, sum((i % 97) * 3 + 1 for i in range(n)), """
FUNCTION transform(value)
    RETURN value * 3 + 1
END transform
FUNCTION benchmark(n)
    checksum = 0
    FOR i = 0 TO n - 1
        checksum = checksum + transform(i - INT(i / 97) * 97)
    END FOR
    RETURN checksum
END benchmark
""", """
def transform(value):
    return value * 3 + 1
def benchmark(n):
    checksum = 0
    for i in range(n):
        checksum = checksum + transform(i - int(i / 97) * 97)
    return checksum
""", """
function transform(value) { return value * 3 + 1; }
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) checksum = checksum + transform(i - Math.trunc(i / 97) * 97);
    return checksum;
}
""")

    n = 30000
    yield Workload("object_methods", n, n * (n - 1) // 2 + n, """
CLASS Counter
    ATTRIBUTES:
        value = 0
    METHODS:
        FUNCTION Counter(value)
            this.value = value
        END Counter
        FUNCTION bump()
            this.value = this.value + 1
            RETURN this.value
        END bump
END Counter
FUNCTION benchmark(n)
    checksum = 0
    FOR i = 0 TO n - 1
        counter = NEW Counter(i)
        checksum = checksum + counter.bump()
    END FOR
    RETURN checksum
END benchmark
""", """
class Counter:
    def __init__(self, value):
        self.value = value
    def bump(self):
        self.value = self.value + 1
        return self.value
def benchmark(n):
    checksum = 0
    for i in range(n):
        counter = Counter(i)
        checksum = checksum + counter.bump()
    return checksum
""", """
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
""")

    n = 20000
    yield Workload("cyclic_garbage", n, 2 * n, """
FUNCTION benchmark(n)
    checksum = 0
    FOR i = 0 TO n - 1
        values = []
        values.append(values)
        dict = {}
        dict["array"] = values
        values.append(dict)
        checksum = checksum + values.length
    END FOR
    RETURN checksum
END benchmark
""", """
def benchmark(n):
    checksum = 0
    for i in range(n):
        values = []
        values.append(values)
        dictionary = {}
        dictionary["array"] = values
        values.append(dictionary)
        checksum = checksum + len(values)
    return checksum
""", """
function benchmark(n) {
    let checksum = 0;
    for (let i = 0; i < n; i++) {
        const values = [];
        values.push(values);
        const dict = new Map();
        dict.set("array", values);
        values.push(dict);
        checksum = checksum + values.length;
    }
    return checksum;
}
""")
