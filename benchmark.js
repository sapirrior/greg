const { execSync } = require('child_process');
const os = require('os');

const pattern = 'ls';
const targetDir = os.homedir();

// how many times we repeat each tool
const WARMUP_RUNS = 3;
const BENCH_RUNS = 10;

function runCommand(command) {
    const start = process.hrtime.bigint();

    try {
        execSync(command, { stdio: 'ignore' });
    } catch (e) {
        // ignore non-zero exit codes (no matches etc.)
    }

    const end = process.hrtime.bigint();
    return Number(end - start) / 1_000_000; // ms
}

function benchmark(name, command) {
    console.log(`\n🔥 Benchmarking ${name}`);

    // warmup phase (IMPORTANT for cache + JIT + disk)
    for (let i = 0; i < WARMUP_RUNS; i++) {
        runCommand(command);
    }

    const times = [];

    for (let i = 0; i < BENCH_RUNS; i++) {
        const t = runCommand(command);
        times.push(t);
        console.log(`  run ${i + 1}: ${t.toFixed(2)} ms`);
    }

    times.sort((a, b) => a - b);

    const min = times[0];
    const max = times[times.length - 1];
    const median = times[Math.floor(times.length / 2)];
    const avg = times.reduce((a, b) => a + b, 0) / times.length;

    console.log(`\n📊 ${name} summary:`);
    console.log(`  min    : ${min.toFixed(2)} ms`);
    console.log(`  max    : ${max.toFixed(2)} ms`);
    console.log(`  avg    : ${avg.toFixed(2)} ms`);
    console.log(`  median : ${median.toFixed(2)} ms`);

    return { name, median, avg };
}

console.log(`🚀 Starting benchmark for "${pattern}" in ${targetDir}`);

// tools
const rg = `rg "${pattern}" "${targetDir}"`;
const greg = `./greg "${pattern}" "${targetDir}"`;

// run benchmarks
const rgRes = benchmark("ripgrep (rg)", rg);
const gregRes = benchmark("greg", greg);

// compare by median (more stable than single run)
const results = [rgRes, gregRes].sort((a, b) => a.median - b.median);

console.log(`\n🏁 FINAL RANKING (by median):`);
results.forEach((r, i) => {
    console.log(`${i + 1}. ${r.name} → ${r.median.toFixed(2)} ms`);
});

console.log(`\n⚖️ Differences vs fastest:`);
const fastest = results[0];
for (const r of results) {
    const diff = r.median - fastest.median;
    console.log(`- ${r.name}: +${diff.toFixed(2)} ms`);
}
