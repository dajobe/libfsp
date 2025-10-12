# Fuzzing libfsp

## Requirements

Fuzzing requires **LLVM Clang** with libFuzzer support (not available
in Apple Clang on macOS).

### Linux

Install LLVM/Clang and C++ standard library:

```bash
# Ubuntu/Debian
sudo apt-get install clang g++

# Fedora/RHEL
sudo dnf install clang gcc-c++
```

**Note:** The sanitizers require C++ standard library support (libstdc++),
even though libfsp is written in C.

### macOS (via Homebrew)

**IMPORTANT:** Apple Clang (macOS default) does **not** include
libFuzzer support.

You must install and use LLVM Clang from Homebrew:

```bash
brew install llvm

# Add LLVM to PATH (use /usr/local for Intel Macs)
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# Verify LLVM Clang is in PATH
which clang
# Should show: /opt/homebrew/opt/llvm/bin/clang

clang --version
# Should show: clang version X.X.X (not "Apple clang")
```

If you see "Apple clang", the system clang is still being used. Make
sure LLVM's bin directory is first in your PATH.

## Building the Fuzzer

```bash
make -f GNUMakefile fuzz
```

This builds:

- `fuzz_fsp_parse` - Parser fuzzer with AddressSanitizer and
  UndefinedBehaviorSanitizer

The fuzzer is built with:

- `-fsanitize=fuzzer` - libFuzzer instrumentation
- `-fsanitize=address` - Detect buffer overflows, use-after-free, etc.
- `-fsanitize=undefined` - Detect undefined behavior

## Running the Fuzzer

### Quick Run (60 seconds)

```bash
make -f GNUMakefile fuzz-parse-run
```

### Custom Duration

```bash
# Run for 5 minutes
make -f GNUMakefile fuzz-parse-run FUZZ_TIME=300

# Run for 1 hour
make -f GNUMakefile fuzz-parse-run FUZZ_TIME=3600
```

### Advanced Usage

```bash
# Direct invocation with custom options
./fuzz_fsp_parse -timeout=10 -max_total_time=3600 -jobs=4 \
  -artifact_prefix=artifacts/ -dict=dicts/fsp.dict corpus/parse/
```

Common libFuzzer options:

- `-timeout=N` - Timeout for each input (default: 10 seconds)
- `-max_total_time=N` - Total fuzzing time in seconds
- `-jobs=N` - Number of parallel fuzzing jobs
- `-max_len=N` - Maximum input length (default: varies)
- `-dict=FILE` - Use mutation dictionary

## Corpus

Seed corpus files are in `corpus/parse/`:

- `1_basic.txt` - Simple print and let statements
- `2_multiline.txt` - Triple-quoted multi-line strings
- `3_mixed.txt` - Mixed statement types
- `4_empty.txt` - Empty input
- `5_single_char.txt` - Single character

The fuzzer will automatically:

- Minimize the corpus
- Generate new interesting inputs
- Save crashing inputs to `artifacts/`

## Dictionary

The fuzzer uses `dicts/fsp.dict` which contains:

- Language keywords (`print`, `let`)
- Operators (`=`, `;`)
- String delimiters (`"`, `"""`)
- Common values and edge cases

This helps the fuzzer generate valid inputs faster.

## Interpreting Results

### Success

```
INFO: seed corpus: files: 5 ...
#1024   pulse  cov: 89 ft: 123 ...
#2048   pulse  cov: 91 ft: 128 ...
Done 10000 runs in 60 seconds
```

### Crash Found

```
SUMMARY: AddressSanitizer: heap-buffer-overflow ...
artifact_prefix='artifacts/'; Test unit written to artifacts/crash-...
```

Crashing inputs are saved to `artifacts/` for debugging.

### Common Issues

**Timeout:**
Input caused hang or infinite loop. Saved to `artifacts/timeout-*`.

**Out of Memory:**
Input caused excessive memory allocation. Saved to `artifacts/oom-*`.

**Sanitizer Error:**
Memory safety violation or undefined behavior. Saved to `artifacts/crash-*`.

## Reproducing Crashes

```bash
# Re-run with a crashing input
./fuzz_fsp_parse artifacts/crash-abc123

# Debug with lldb/gdb
lldb ./fuzz_fsp_parse
(lldb) run artifacts/crash-abc123
```

## Continuous Fuzzing

For extended fuzzing campaigns:

```bash
# 24 hour run with 4 parallel jobs
./fuzz_fsp_parse -max_total_time=86400 -jobs=4 \
  -artifact_prefix=artifacts/ -dict=dicts/fsp.dict corpus/parse/
```

## Cleaning Up

```bash
make -f GNUMakefile fuzz-clean
rm -rf artifacts/
```

## What Gets Tested

The fuzzer exercises:

- **Streaming parser**: Various chunk sizes (1-64 bytes)
- **Token boundaries**: Splitting tokens at different positions
- **Triple-quoted strings**: Multi-line content across chunks
- **Buffer management**: Append, compact, read operations
- **Memory safety**: No leaks, overflows, or use-after-free
- **Undefined behavior**: Integer overflow, null dereferences, etc.

## Integration with CI/CD

Example GitHub Actions workflow (requires Ubuntu):

```yaml
- name: Install Clang
  run: sudo apt-get install -y clang

- name: Build fuzzer
  run: make -f GNUMakefile fuzz

- name: Fuzz for 5 minutes
  run: make -f GNUMakefile fuzz-parse-run FUZZ_TIME=300
```

## See Also

- [libFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html)
- [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
