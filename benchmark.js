const { execSync } = require('child_process');
const path = require('path');

const pattern = 'ls';
const targetDir = require('os').homedir(); // Test in user home directory (~/)

function benchmark(command, name) {
    console.log(`Running: ${name}...`);
    const start = process.hrtime.bigint();
    try {
        execSync(command, { stdio: 'ignore' });
    } catch (e) {
        // If no matches are found, it might return exit code 1
    }
    const end = process.hrtime.bigint();
    const ms = Number(end - start) / 1_000_000;
    console.log(`${name} completed in: ${ms.toFixed(2)} ms\n`);
    return ms;
}

console.log(`Starting benchmark for pattern "${pattern}" in "${targetDir}"...\n`);

const rgCommand = `rg "${pattern}" "${targetDir}"`;
const gregCommand = `./greg "${pattern}" "${targetDir}"`;

const rgTime = benchmark(rgCommand, 'ripgrep (rg)');
const gregTime = benchmark(gregCommand, 'greg');

const diff = gregTime - rgTime;
if (diff > 0) {
    console.log(`ripgrep is faster by ${(diff).toFixed(2)} ms (${(gregTime / rgTime).toFixed(2)}x)`);
} else {
    console.log(`greg is faster by ${(-diff).toFixed(2)} ms (${(rgTime / gregTime).toFixed(2)}x)`);
}
