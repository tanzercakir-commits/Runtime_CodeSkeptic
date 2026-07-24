# Determinism and Canonical Serialization

The byte-exact rules that make an environment profile, a requirement document and an analysis result reproducible across machines, toolchains and releases.

**Status:** ROADMAP Phase 1 exit criterion ("profile output passes deterministic canonicalization tests") and Phase 2 deliverable ("canonical serializer"). **Implemented** in `src/core/json.cpp`, `src/core/sha256.cpp`, `src/vm/profile.cpp` and `src/vm/address_range.cpp`. The conformance tests that would enforce it are **not implemented**: `tests/unit/` and `tests/conformance/` are empty, and the root `CMakeLists.txt` refers to `add_subdirectory(tests)` for a directory that has no `CMakeLists.txt`.

---

## 1. Why this is a product requirement

An environment profile is compared across machines and across releases, hashed for identity, and cited by findings. Three ROADMAP requirements make canonicalization non-optional:

- Phase 1: *repeated runs on the same stable host produce equivalent canonical profiles*;
- Phase 3: *findings are reproducible from the same requirement and profile*;
- section 17: *every analysis run should be reproducible from an evidence bundle*.

"Equivalent" here means byte-identical after canonicalization. Anything weaker requires a semantic comparator, and a semantic comparator is a second implementation of the schema that will drift from the first.

This is also the reason the project has **zero external dependencies**. From the root `CMakeLists.txt`:

> RuntimeSkeptic deliberately has ZERO external dependencies. Rationale: the tool must produce byte-identical canonical output across platforms and toolchains. Every serialization, hashing and formatting decision is therefore owned by this repository.

A third-party JSON library that changes its number formatting, key ordering or escaping in a minor release would silently change every `profile_id` in existence.

---

## 2. Canonical JSON

Implemented by `json::serialize_canonical()` in `src/core/json.cpp`. It returns `std::optional<std::string>` and yields no value when the document contains something with no canonical representation.

### 2.1 Object keys are sorted by UTF-8 code unit, ascending

Key order is a property of the document, not of the code that built it. Two profiles that differ only in the order fields were assigned must produce the same bytes.

The mechanism is the container type:

```cpp
// include/runtimeskeptic/core/json.hpp
// std::map keeps keys sorted, which is exactly the canonical order we want.
using Object = std::map<std::string, Value>;
```

`std::map<std::string, ...>` orders by `std::string`'s `operator<`, which compares `char` by `char` — an unsigned byte comparison of the UTF-8 encoding. This is *code-unit* order, not Unicode collation order, and the distinction matters: collation is locale-dependent and version-dependent, and both would make the output non-reproducible. Code-unit order is a total order over byte strings with no external inputs.

The canonical writer iterates the map in order and never re-sorts, so there is exactly one place where ordering is decided.

Duplicate keys cannot occur: `std::map` rejects them at insertion, so a document is structurally incapable of carrying two values for one key.

### 2.2 No insignificant whitespace

No spaces, no newlines, no indentation. Separators are a single `,` and a single `:`.

```json
{"origin":"measured","platform":{"os":"linux"},"schema":"runtime-skeptic.environment-profile.v1"}
```

`json::serialize_pretty()` exists for human consumption. It sorts keys the same way, so a pretty document canonicalizes to the same bytes after a parse round-trip. Only `serialize_canonical()` output may be hashed.

### 2.3 Doubles are rejected

Not rounded, not formatted to a fixed precision — **rejected**. `write_canonical()` returns `false` for `Type::Double`, and `serialize_canonical()` propagates that as an empty optional.

```cpp
case Type::Double:
    // Deliberate: floating point has no stable cross-platform textual
    // form we are willing to depend on for hashed artifacts.
    return false;
```

The reasoning: the shortest round-tripping decimal representation of a binary64 value depends on the algorithm used to produce it, implementations disagree at the last digit, and `NaN` and the infinities have no JSON representation at all. Any of those turns a hash into a platform-dependent value.

The parser still *accepts* doubles on input (`Type::Double` exists, and `serialize_pretty()` formats them with `%.17g`), so a malformed or extended document can be read and diagnosed rather than rejected at the lexer. It simply cannot be canonicalized, which means it cannot be hashed, which means it cannot become a profile identity.

**Consequence for schema design:** no schema in this project may use a floating-point field. Every quantity is an integer or a string. Durations are integer milliseconds; ratios, if ever needed, are a pair of integers.

### 2.4 Integers are emitted without exponent or fraction

Signed values via `%lld`, unsigned via `%llu`. The value type distinguishes `Type::Int` (signed 64-bit) from `Type::UInt` (unsigned 64-bit), so a size near `2^63` does not become negative on a round trip.

### 2.5 String escaping

The shortest legal escape, and nothing else:

| Input | Output |
| --- | --- |
| `"` | `\"` |
| `\` | `\\` |
| `U+0008 U+000C U+000A U+000D U+0009` | `\b` `\f` `\n` `\r` `\t` |
| other control characters below `U+0020` | `\u00xx`, lowercase hex |
| `/` | `/` — **not** escaped |
| bytes `>= 0x20` | passed through unchanged |

Non-ASCII text is emitted as raw UTF-8, not as `\uXXXX` escapes. Escaping would introduce a case choice for the hex digits and a decision about surrogate pairs, both of which are additional opportunities to disagree. The canonical form is UTF-8 bytes.

Forward slash is deliberately unescaped. Both `/` and `\/` are legal JSON for the same character, so a choice is required; the shorter one is chosen and is applied everywhere.

---

## 3. Addresses are hex strings, sizes are integers

### 3.1 The rule

| Kind | JSON representation | Example |
| --- | --- | --- |
| virtual address | lowercase hex **string** with `0x` prefix | `"0x1000000000"` |
| address-range bound | lowercase hex **string** | `"start": "0x1000000000"` |
| size, length, count, duration | JSON **integer** | `16384` |
| page size, allocation granularity | JSON **integer** | `4096` |

Implemented by `json::to_hex()` / `json::from_hex()`, and enforced at the type level: `rs::Address` is a distinct struct wrapping a `std::uint64_t`, so `Fact<T>::encode()` selects the hex-string branch for addresses and the integer branch for sizes without the call site having to remember which is which.

```cpp
// include/runtimeskeptic/core/fact.hpp
struct Address {
    std::uint64_t value = 0;
    Address() = default;
    explicit Address(std::uint64_t v) : value(v) {}
};
```

```cpp
if constexpr (std::is_same_v<T, Address>) {
    return json::Value(json::to_hex(v.value));
} else if constexpr (std::is_unsigned_v<T>) {
    return json::Value(static_cast<unsigned long long>(v));
}
```

### 3.2 Why addresses are not JSON numbers

**The interoperability reason (the decisive one).** JSON numbers are not safely interoperable above `2^53`. RFC 8259 says so directly, and the practical cause is that JavaScript's `Number` is a binary64: every JSON parser in a browser, in Node, and in a great many tools built on them represents integers as doubles and loses precision beyond `2^53 - 1` (9007199254740991).

A 64-bit virtual address routinely exceeds that. `0x7fffffffffff`, a perfectly ordinary user-space bound, is 140737488355327 — well past the safe range. A JavaScript consumer reading it as a number gets a value that is *close*, silently, with no error. For an arithmetic quantity a small error might be tolerable; for an address it is a different address.

Since profiles are meant to be read by report generators, dashboards, CI integrations and other people's tooling, and since the failure is silent, the format must make the mistake impossible rather than merely discouraged. A string cannot be silently rounded.

**The semantic reason.** At the schema level an address is an opaque identifier, not a quantity. The operations that make sense on it — comparison, containment, alignment — are performed inside the analyzer on `std::uint64_t`, not by a JSON consumer. Nothing downstream should be adding two addresses together, and representing them as strings makes that structurally awkward, which is the correct incentive.

**The readability reason.** `"0x1000000000"` is recognizable to a systems engineer; `68719476736` is not. Profiles are read by humans during triage.

### 3.3 Why sizes remain integers

Sizes *are* arithmetic quantities: they are added, rounded to granularity, and compared against limits. They are also small enough for the `2^53` problem to be theoretical — a size that large is not a mapping, and `AddressRange::from_base_size()` rejects a base+size that wraps the address space before any such value can appear.

Keeping the two kinds visibly distinct in the JSON has a secondary benefit: a reader can tell at a glance which fields are locations and which are extents, without consulting the schema.

### 3.4 Format details

- lowercase hex digits, always. `"0x1000000000"`, never `"0X1000000000"` or `"0x1000000ABC"`.
- `0x` prefix, always.
- no leading zero padding to a fixed width. `to_hex()` emits the minimal digit count, so `0x1000` is `"0x1000"` and not `"0x0000000000001000"`. Fixed-width padding would be a second formatting decision to keep consistent, and it buys nothing that a hash needs.
- zero is `"0x0"`.

`from_hex()` is the only accepted input path for an address field. `read_address()` in `fact.hpp` additionally tolerates a JSON integer on input — for hand-written documents — but the canonical *output* is always a hex string, so a document that came in with an integer address canonicalizes to the string form and hashes accordingly.

---

## 4. Address ranges are half-open

### 4.1 The convention

```text
[start, end)      start is included, end is excluded
```

Stated at the top of `include/runtimeskeptic/vm/address_range.hpp` and implemented throughout:

```cpp
struct AddressRange {
    std::uint64_t start = 0;
    std::uint64_t end = 0;  // exclusive

    bool empty()  const { return end <= start; }
    uint64_t length() const { return empty() ? 0 : end - start; }
    bool contains(uint64_t addr) const { return addr >= start && addr < end; }
    bool intersects(const AddressRange& o) const {
        if (empty() || o.empty()) return false;
        return start < o.end && o.start < end;
    }
};
```

`AddressRange::to_string()` prints the convention so it cannot be misread in a report: `[0x1000000000, 0x1000004000)`.

### 4.2 This is a deliberate deviation from the ROADMAP

ROADMAP section 10.1 shows an illustrative profile:

```yaml
unavailable_ranges:
  - start: 0x1000000000
    end: 0x6fffffffff
    evidence: measured
```

That `end` value reads as **inclusive**: `0x6fffffffff` looks like the last unavailable byte, not the first available one. Under the half-open convention the same region is written `end: 0x7000000000`.

The implementation uses half-open intervals and the header says so explicitly. The ROADMAP example is illustrative YAML written before the type existed; the code is authoritative.

**Consequence for anyone transcribing the ROADMAP example into a real profile:** add one. A profile written with `end: 0x6fffffffff` describes a range one byte shorter than intended, and the byte at `0x6fffffffff` will be reported as available.

### 4.3 Why half-open

**Length is `end - start`, with no off-by-one.** Under the inclusive convention it is `end - start + 1`, and that `+ 1` must be written correctly at every site that computes a length, compares one, or checks an overflow. `AddressRange::from_base_size()` constructs `{base, base + size}` directly and the arithmetic is the definition.

**An empty range is representable.** `start == end` is empty, and `empty()` is a total function. Under the inclusive convention an empty range has `end < start`, which is indistinguishable from a malformed one — so validation cannot reject `end < start`, and a transposition typo becomes silently legal. `rs-profile verify` rejects `end < start` precisely because half-open makes it unambiguously an error.

**Overlap and adjacency have no special cases.** The intersection test is one expression with no `+ 1` anywhere:

```cpp
return start < other.end && other.start < end;
```

Two ranges are adjacent exactly when `a.end == b.start`, which composes: splitting `[a, c)` at `b` gives `[a, b)` and `[b, c)` with no gap and no overlap, and `length([a,b)) + length([b,c)) == length([a,c))`. Under the inclusive convention the split point must be written twice, as `b - 1` and `b`, and every splitting operation is an opportunity for an off-by-one.

**It matches the domain.** Page and granularity arithmetic is naturally half-open: a page at `base` covers `[base, base + page_size)`. `align_up`, `align_down` and `round_up_to` all produce boundaries, and a boundary is an exclusive end.

### 4.4 The top of the address space

A range ending at the top of a 64-bit address space would need `end == 2^64`, which is not representable in `uint64_t`. Two rules keep this from becoming a lurking special case:

- **`end == 0` is never a sentinel for "the top".** A range with `end == 0` is empty, full stop.
- The maximum representable end is `UINT64_MAX`, and the byte at `UINT64_MAX` is outside every representable half-open range.

The header notes an `end_is_max` flag for the case where `contains()` should treat `UINT64_MAX` as included. No such flag exists on `AddressRange` in the current code, and no profile needs one: user address spaces end well below `UINT64_MAX` on every platform in scope. If the case ever arises, it must be added explicitly rather than encoded as a magic value.

---

## 5. `profile_id`

### 5.1 Definition

```text
profile_id = "sha256:" + hex(SHA-256(serialize_canonical(facts_json())))
```

Implemented as:

```cpp
// src/vm/profile.cpp
std::string EnvironmentProfile::profile_id() const {
    auto canonical = json::serialize_canonical(facts_json());
    if (!canonical) return "sha256:unavailable";
    return rs::hash::sha256_uri(*canonical);
}
```

`sha256_uri()` returns `"sha256:"` followed by 64 lowercase hex characters. The algorithm prefix is present so a future migration can coexist with existing identifiers rather than replacing them silently.

The hash is an **identity and integrity** hash, not a security primitive in an adversarial setting. Signed profiles are a Phase 10 concern.

### 5.2 What is hashed

```cpp
json::Value EnvironmentProfile::facts_json() const {
    json::Value v = json::Value::object();
    v["schema"]        = schema;
    v["origin"]        = std::string(to_string(origin));
    v["profile_name"]  = profile_name;
    v["platform"]      = platform.to_json();
    v["virtual_memory"] = vm.to_json();
    return v;
}
```

| Field | In the hash? |
| --- | --- |
| `schema` | yes |
| `origin` | yes |
| `profile_name` | **yes** — see the discrepancy note below |
| `platform` (os, versions, host_arch, process_arch, translation_mode) | yes |
| `virtual_memory` (every capability fact, including each fact's evidence class, source and note) | yes |
| `probe_run` (tool_version, probe_version, run_id, timestamp_utc, probe_binary_hash, duration_ms, warnings) | **no** |
| `notes` | **no** |
| `profile_id` itself | **no** — it is added by `to_json()` after `facts_json()` has been serialized |

### 5.3 Why `probe_run` is excluded

The Phase 1 exit criterion is *repeated runs on the same stable host produce equivalent canonical profiles*. Run metadata is different on every run by construction: the timestamp advances, the duration varies with load, the run id is fresh. Including any of it would make every profile unique and the identifier useless for its one job — answering "is this the same host configuration as before?"

The split has a second effect that is worth as much: it makes `rs-profile diff` meaningful. Two profiles with the same `profile_id` differ only in metadata; two with different ids differ in a fact, and the diff is guaranteed to show something that matters.

The exclusion is directional. Run metadata is still *recorded* — `to_json()` emits it, evidence bundles keep it, and `probe_binary_hash` remains the record of which probe produced the facts. It is simply not part of the identity.

### 5.4 Evidence classes are inside the hash

`VirtualMemoryModel::to_json()` serializes each `Fact<T>` with its `value`, its `evidence`, and its `source` and `note` where present. All of that is inside `facts_json()` and therefore inside the hash.

This is intentional. A profile that says "page size is 16384, measured" and one that says "page size is 16384, specified" are **not** the same profile: they support different confidence ceilings and therefore different findings. Two analyses that produce different results must not be able to cite the same `profile_id`.

### 5.5 Failure mode

If canonicalization fails — which today means a double somewhere in the document — `profile_id()` returns the literal string `"sha256:unavailable"`. This is a sentinel that is obviously not a hash, so it cannot be mistaken for one, and it propagates into `Finding::profile_id` where it is visible in the report.

`Requirement::requirement_id()` follows the same pattern, hashing the canonical form of the *entire* requirement document (there is no metadata subtree to exclude) and returning `"sha256:unavailable"` on the same failure.

### 5.6 Known discrepancy: `profile_name` is inside the hash

The header comment in `include/runtimeskeptic/vm/profile.hpp` describes the hashed subtree as:

> Canonical fact subtree: schema + origin + platform + virtual_memory.

The implementation also includes `profile_name`. **The code is authoritative and this document describes the code**, but the two should be reconciled, and the choice is not obvious:

- **Keep `profile_name` in the hash** (current behavior): renaming a profile produces a new `profile_id`. Defensible if the name is treated as part of the profile's identity — two differently-named descriptions of the same host are distinct artifacts.
- **Remove it** (what the comment says): renaming a profile preserves its id, so `rs-profile diff` reports no change for a pure rename. Defensible if the name is a human label over an unchanged set of facts.

The second reading is more consistent with the stated purpose — the id answers "is this the same host configuration?", and a rename does not change the host — but changing it now would invalidate every existing `profile_id`. Whichever is chosen, the comment and the code must agree, and the decision belongs in a schema version note.

---

## 6. Determinism outside serialization

Byte-exact output is necessary but not sufficient. Two further ordering decisions are part of the contract.

**Finding order.** `Analysis::run()` sorts findings with a `std::stable_sort` on `(severity, confidence, id)`. All three keys are needed: severity and confidence alone do not disambiguate, and the id does. The sort is stable so that two findings identical in all three keys retain rule-evaluation order, which is itself fixed — the fifteen rules are called in a literal sequence in `run()`, not dispatched from a container.

**Array order.** JSON arrays preserve insertion order and are never sorted by the serializer, so any array in a schema must have a defined construction order. `unavailable_ranges` and `available_ranges` are serialized in the order the profile carries them; a probe must therefore emit them in a deterministic order (`AddressRange::operator<` orders by `start`, then `end`, and is the natural choice) or two runs on the same host will produce different bytes despite identical facts. This is a probe requirement, and the probe does not exist yet.

**No wall-clock or environment inputs in the analysis path.** Nothing in `src/core/` or `src/vm/` reads the clock, the environment, a locale, or a random source. Number formatting uses `snprintf` with `%lld` / `%llu` / `%04x`, none of which is locale-sensitive for integers. Timestamps exist only in `ProbeRun`, which is outside the hash.

---

## 7. What a conformance test suite must check

None of these exist yet. They are the Phase 1 and Phase 2 exit criteria expressed as tests, listed so the gap is explicit.

| Property | Test |
| --- | --- |
| key order is insertion-independent | build the same document with fields assigned in several orders; canonical bytes identical |
| pretty round-trips to canonical | `parse(serialize_pretty(v))` canonicalizes to `serialize_canonical(v)` |
| doubles are rejected | a document containing a double yields an empty optional from `serialize_canonical` |
| escaping is exact | every control character, quote, backslash and a multi-byte UTF-8 string produce the documented bytes |
| addresses round-trip | `from_hex(to_hex(x)) == x` for boundary values including 0, `2^53`, `2^53 + 1`, `UINT64_MAX` |
| addresses are strings in output | no address field is ever emitted as a JSON number |
| `profile_id` ignores `probe_run` | mutate every field of `ProbeRun`; `profile_id()` unchanged |
| `profile_id` follows every fact | mutate each fact's value, evidence, source and note in turn; `profile_id()` changes each time |
| `profile_id` is stable across a round trip | `from_json(to_json(p))` has the same `profile_id()` |
| half-open arithmetic | `length`, `contains`, `intersects`, `intersection` against a reference implementation, including empty and adjacent ranges |
| `end < start` is rejected | profile verification fails rather than silently normalizing |
| SHA-256 correctness | FIPS 180-4 test vectors, plus the empty string |
| finding order is deterministic | the same requirement and profile produce the same finding sequence across runs and platforms |
| cross-platform identity | the same fixture canonicalizes to the same bytes on Linux, macOS and Windows, and on at least two compilers |

The last one is the only one that requires CI on three platforms, and it is the one that would actually catch a formatting divergence.
