# Runtime wrapper overhead benchmark

`rs-runtime-benchmark` performs paired allocate/free cycles in five modes:

1. native APIs;
2. compile-disabled header aliases;
3. linked wrappers with runtime monitoring disabled;
4. enabled fixed-buffer recording;
5. enabled recording plus a sealed trace flush.

The versioned output contract is `schemas/runtime-overhead.v1.json`. Each mode
reports integer median and p95 nanoseconds, median delta from native, and a
ratio in thousandths. Example:

```text
rs-runtime-benchmark --iterations 128 --output overhead.json
```

The CTest smoke lane executes the benchmark and the binary validates its own
artifact against the embedded schema. `validate_schemas.py` independently runs
a fresh benchmark and validates it with Python `jsonschema` when the built
binary is present.

Hosted-runner timings are evidence, not a stable performance threshold. CI
requires execution and schema validity; it does not fail on a noisy timing
ratio. Compare repeated artifacts from the same host class and build mode before
making a performance claim.
