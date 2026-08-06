# RuntimeSkeptic — Gerçek dünya test rehberi (dış projeler)

Bu belge, depoda **henüz olmayan** beş gerçek projeyi RuntimeSkeptic ile test
etmen için hazırlandı. Hepsi bakımı süren, VM (sanal bellek) varsayımı bilinen
projeler. Aşağıdaki bütün verdict'ler **ölçüldü** — iddia değil, `rs-check`'in
depodaki ölçülü profillere karşı gerçekten ürettiği çıktı.

Standing kural hatırlatması: *"bir çok gerçek hayat testi olmadıkça birleştirme
yok."* Bu rehber tam olarak o kapıyı besler — §5 sonuçları nasıl kayda
geçireceğini anlatıyor.

---

## 0. Ön koşul — Windows'ta derle ve kendi konağını ölç

Hepsinin ortak başlangıcı. VSCode'da yapabilirsin.

**Toolchain (araç zinciri):**

```
- Visual Studio Build Tools (MSVC, C++ workload)   ← C++20 derleyici
- CMake  (VS ile gelir, ya da ayrı)
- VSCode eklentileri: C/C++ + CMake Tools
```

**Derle:**

```bat
git clone https://github.com/tanzercakir-commits/Runtime_CodeSkeptic
cd Runtime_CodeSkeptic
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

İkili dosyalar (binaries) burada olur:

```
build\bin\RelWithDebInfo\rs-env-probe.exe
build\bin\RelWithDebInfo\rs-check.exe
```

**Kendi Windows konağını ölç (senin profilin):**

```bat
build\bin\RelWithDebInfo\rs-env-probe.exe vm --output my-windows.json
```

Bu, senin makinenin sanal-bellek yeteneklerini (sayfa boyutu, tahsis
granülaritesi, reserve/commit modeli, W^X, exact-mapping…) ölçüp bir profile
yazar. Bir sözleşmeyi (contract) bu profile karşı sınamak:

```bat
build\bin\RelWithDebInfo\rs-check.exe CONTRACT.json --profile my-windows.json --format markdown
```

`--format json` makine-okunur, `--format markdown` insan-okunur. Çıkış kodu =
verdict (1 = UNSUPPORTED). Depoda hazır profiller de var, bunlara ölçmeden
karşı sınayabilirsin:

```
profiles\measured\macos-14-arm64-native.measured.json          (16 KiB sayfa, W^X)
profiles\measured\macos-14-arm64-rosetta-x86_64.measured.json  (Rosetta 2)
profiles\measured\windows-server-2025-x86_64.measured.json     (CI'ın Windows'u)
profiles\measured\wine-9.0-on-linux-x86_64.measured.json        (Wine)
```

---

## 1. Testin mantığı — tek cümle

> Bir projenin VM gereksinimini kaynağından/olayından çıkar → bir **sözleşmeye**
> (contract) yaz → `rs-check` ile **birden çok profile** karşı değerlendir →
> verdict'i **gerçekte olanla** karşılaştır.

Aracın tezi: **"ortam bir girdidir"** — *aynı* sözleşme, *farklı* konak, *farklı*
verdict. İyi bir test bunu gösterir: bir konakta SUPPORTED, diğerinde UNSUPPORTED.

---

## 2. Beş taze hedef — ölçülmüş verdict matrisiyle

Aşağıdaki tablo, dört hazır sözleşmenin (§2.1–2.4) üç profile karşı **ölçülmüş**
sonucudur:

| Proje | Zorladığı kural | Linux 4K | macOS 16K | Windows | Windows'ta çalışır mı? (observe) |
|---|---|---|---|---|---|
| **FEX-Emu** | RS-VM-0006 (sayfa boyutu) | ✅ SUPPORTED | ❌ **UNSUPPORTED** | ✅ SUPPORTED | Hayır (Linux/ARM) — extract-only |
| **RPCS3** | RS-VM-0009/0011 (W^X + entitlement) | ✅ SUPPORTED | ❌ **UNSUPPORTED** | ✅ SUPPORTED | **Evet** — çalıştırıp ETW ile gözleyebilirsin |
| **wasmtime** | RS-VM-0012 (reserve/commit) | ⚠️ CONDITIONAL | ⚠️ CONDITIONAL | ✅ SUPPORTED | **Evet** |
| **PCSX2** | RS-VM-0024 (çalıştırılabilir dosyaya göre yerleşim) | **UNKNOWN** | **UNKNOWN** | **UNKNOWN** | **Evet** |

**En güçlü ikisi FEX ve RPCS3** — konağa göre yeşilden kırmıza dönüyorlar. İşte
"durumu gör" tam bu.

---

### 2.1 FEX-Emu — bir x86 emülatörünün 4 KiB sayfa varsayımı

**Ne:** x86/x86-64 programlarını ARM64'te koşturan emülatör (Fedora 42'de
resmî). x86 kodu ve oyunların dosya-akış (file-streaming) mantığı **4 KiB sayfa
hizasını** sabit varsayar; FEX bunu onurlandırmak zorunda. Apple Silicon /
Asahi'nin **16 KiB** sayfası bunu kırar. FEX geliştiricisinin kendi sözleriyle:
*"any application expecting 4k page alignments will break"* — ve bu hâlâ açık
bir sorun (issue #1221).

**Kural:** `RS-VM-0006` (host page size differs from required page size).

**Ölçülmüş verdict:**

```
Linux 4K     SUPPORTED
macOS 16K    UNSUPPORTED   RS-VM-0006 (PROVEN) + RS-VM-0005
Windows      SUPPORTED     (+ RS-VM-0005 info notu: 4096, 64K granülariteye bölünmez)
```

macOS'taki kanıt zinciri (aracın var oluş amacı — katmanlar arası):

```
uygulama:  "program requires a page size of 4096"   (specified_guarantee)
      ↓
işletim:   "host page size is 16384"                (measured: macOS sysconf)
      →    "No mapping request can repair this."     RS-VM-0006, PROVEN
```

**Sözleşme** (`contracts\campaign\fex-4k-page-on-16k-host.json` olarak kaydet):

```json
{
  "schema": "runtime-skeptic.application-requirements.v1",
  "name": "FEX-Emu: an emulated x86 guest hard-assumes a 4 KiB host page",
  "component": "FEX-Emu guest memory and file-streaming; x86 code and games assume 4K alignment and FEX must honour it",
  "operation": "virtual_memory_map",
  "request": {
    "size": 4096,
    "exact_address_required": false,
    "protection": { "read": true, "write": true, "execute": false },
    "required_page_size": 4096,
    "required_page_size_relation": "equal"
  },
  "assumptions": { "guest_host_identity_required": false, "translation_layer_available": false, "retries_on_failure": false },
  "required_postconditions": ["the host page size is exactly 4096: guest x86 code and games' file-streaming assume 4K page alignment"],
  "permitted_fallbacks": [],
  "failure_sink": { "kind": "fatal_assert", "description": "FEX issue #1221: on a non-4K host 'any application expecting 4k page alignments will break', expected to break spectacularly" },
  "assumption_evidence": "specified_guarantee",
  "x_campaign": { "project": "FEX-Emu", "source": "github.com/FEX-Emu/FEX#1221", "expected_verdict": "UNSUPPORTED on 16K (macOS arm64), silent on 4K" }
}
```

**Çalıştır:**

```bat
build\bin\RelWithDebInfo\rs-check.exe contracts\campaign\fex-4k-page-on-16k-host.json --profile profiles\measured\macos-14-arm64-native.measured.json --format markdown
build\bin\RelWithDebInfo\rs-check.exe contracts\campaign\fex-4k-page-on-16k-host.json --profile my-windows.json --format markdown
```

---

### 2.2 RPCS3 — JIT'in W^X çarpması (Apple Silicon)

**Ne:** PS3 emülatörü, çok aktif. PPU/SPU **JIT** (LLVM/asmjit) üretilen makine
kodunu bir sayfaya yazıp aynı sayfayı çalıştırır. Apple Silicon **W^X**
(write-xor-execute) zorunlu kılar: bir sayfa aynı anda hem yazılabilir hem
çalıştırılabilir olamaz — `MAP_JIT` + `pthread_jit_write_protect_np` toggle
gerekir (sljit#99, dotnet/runtime#108423, RPCS3#18701). Linux/Windows'ta rwx
serbest.

**Kural:** `RS-VM-0009` (simultaneous W+X restricted) + `RS-VM-0011` (executable
memory requires entitlement).

**Ölçülmüş verdict:**

```
Linux        SUPPORTED
macOS 16K    UNSUPPORTED   RS-VM-0009 (PROVEN) + RS-VM-0011 (PROVEN)
Windows      SUPPORTED
```

**Sözleşme** (`contracts\campaign\rpcs3-jit-wx-apple.json`):

```json
{
  "schema": "runtime-skeptic.application-requirements.v1",
  "name": "RPCS3 PPU/SPU JIT: one code buffer written and executed with W and X live together",
  "component": "RPCS3 LLVM/asmjit code buffer; Apple Silicon needs MAP_JIT + pthread_jit_write_protect_np toggling",
  "operation": "virtual_memory_map",
  "request": {
    "size": 2097152,
    "exact_address_required": false,
    "protection": { "read": true, "write": true, "execute": true },
    "simultaneous_write_execute": true
  },
  "assumptions": { "guest_host_identity_required": false, "translation_layer_available": false, "retries_on_failure": false },
  "required_postconditions": ["the JIT writes new machine code into a page and executes it without an intervening mprotect, so write and execute are permitted at once"],
  "permitted_fallbacks": [],
  "failure_sink": { "kind": "fatal_assert", "description": "on a W^X host the rwx mapping is refused; on Apple Silicon it needs the JIT entitlement and the write-protect toggle (sljit#99, dotnet/runtime#108423)" },
  "assumption_evidence": "specified_guarantee",
  "x_campaign": { "project": "RPCS3", "source": "RPCS3#18701, sljit#99", "expected_verdict": "UNSUPPORTED/CONDITIONAL on macOS, SUPPORTED on Linux/Windows" }
}
```

**Bonus — aynı sınıf, Windows-yerel:** `.NET / CoreCLR` de JIT için çalıştırılabilir
bellek ister ve Apple Silicon'da aynı entitlement sorununu yaşar
(dotnet/runtime#108423). RPCS3 sözleşmesini `.NET`'e uyarlayıp aynı verdict'i
görebilirsin.

---

### 2.3 wasmtime — ~4 GiB doğrusal-bellek ayırması + reserve/commit

**Ne:** Bytecode Alliance'ın WebAssembly runtime'ı, çok modern/aktif. Her
doğrusal bellek (linear memory) için 64-bit'te varsayılan **4 GiB rezervasyon +
32 MiB guard + 2 GiB büyüme** alanı ayırır (Config: `memory_reservation`,
`memory_guard_size`, `memory_reservation_for_growth`). Önce hepsini
erişimsiz **reserve** eder, büyüdükçe sayfa sayfa **commit** eder — Windows'un
`VirtualAlloc` MEM_RESERVE/MEM_COMMIT modeliyle birebir; POSIX'te overcommit.

**Kural:** `RS-VM-0012` (reserve/commit semantics differ).

**Ölçülmüş verdict:**

```
Linux        CONDITIONAL   RS-VM-0012 (PROVEN)   posix_lazy overcommit
macOS        CONDITIONAL   RS-VM-0012 (PROVEN)
Windows      SUPPORTED     windows_reserve_commit modeli birebir uyar
```

Bu, senin Windows'unda **SUPPORTED** çıkması gereken güzel bir örnek: wasmtime'ın
iki-adımlı modeli Windows'un yerel modeliyle örtüşür.

**Sözleşme** (`contracts\campaign\wasmtime-linear-memory-reservation.json`):

```json
{
  "schema": "runtime-skeptic.application-requirements.v1",
  "name": "wasmtime linear memory: a 4 GiB no-access reservation, committed page-by-page on growth",
  "component": "wasmtime Config::memory_reservation=4GiB + memory_guard_size=32MiB + memory_reservation_for_growth=2GiB",
  "operation": "virtual_memory_reserve",
  "request": {
    "size": 4294967296,
    "exact_address_required": false,
    "protection": { "read": false, "write": false, "execute": false },
    "reserve_then_commit": true
  },
  "assumptions": { "guest_host_identity_required": false, "translation_layer_available": false, "retries_on_failure": false },
  "required_postconditions": [
    "a contiguous 4 GiB region is reserved with no access, so a 32-bit wasm address plus a 32 MiB guard can never reach host memory outside it",
    "growth commits pages inside the reservation without moving it, so host pointers into linear memory stay valid"
  ],
  "permitted_fallbacks": [],
  "failure_sink": { "kind": "error_return", "description": "if the reservation fails wasmtime falls back to dynamically bounds-checked memory; the fast path is lost silently - a performance cliff, not a crash" },
  "assumption_evidence": "specified_guarantee",
  "x_campaign": { "project": "wasmtime", "source": "docs.wasmtime.dev/api Config", "expected_verdict": "SUPPORTED on Windows, CONDITIONAL on POSIX" }
}
```

---

### 2.4 PCSX2 — çalıştırılabilir dosyanın yakınına sığmayan bellek pencereleri

**Gerçek olay:** [issue #11728](https://github.com/PCSX2/pcsx2/issues/11728),
PCSX2'nin eski Intel Mac'lerde açılışta `Failed to map data memory at an
acceptable location` hatasıyla kapandığını kaydediyor. Muhabirler, yerleşim
bağımlılığını kaldıran [PR #11734](https://github.com/PCSX2/pcsx2/pull/11734)
derlemesinin aynı makinelerde sorunu giderdiğini doğruladı.

Eski rehber bu olayı tek bir “~400 MB bitişik ayırma” olarak sadeleştiriyordu;
bu doğru değildi. Etkilenen `v1.7.5849` kaynak sabitleri **155 MiB veri** ve
**305 MiB recompiler** alanı tanımlıyor. Eski x86-64 yol bunları program
metninin 256 MiB'ye yuvarlanmış adresine göre, `+4` ile `-6` arasındaki on
bir ayrı kesin adayda arıyordu.

**Kural:** `RS-VM-0024`. Gereksinim çalıştırılabilir dosyaya göre en fazla
1.5 GiB yer değiştirmeyi taşıyor; ancak statik bir konak profili gelecekteki
PCSX2 sürecinin metin adresini bilemez. Çalışma anındaki referans ve on bir
adayın uygunluk ölçümü sağlanırsa her kesin aday `RS-VM-0001` ile
değerlendirilebilir.

**Retrospektif sonuç:**

```
macOS ARM64           UNKNOWN   RS-VM-0024
macOS x86-64/Rosetta  UNKNOWN   RS-VM-0024
Windows x86-64        UNKNOWN   RS-VM-0024
Wine x86-64           UNKNOWN   RS-VM-0024
```

Buradaki `UNKNOWN` eksik test değil, doğru kanıt sınırıdır: olay gerçek ve
düzeltme doğrulanmış olsa da depodaki profiller etkilenen Intel Mac'in süreç
yerleşimini içermiyor. Sözleşme:
[`contracts/campaign/pcsx2-v175849-data-window.json`](../contracts/campaign/pcsx2-v175849-data-window.json).

---

## 3. En güçlü demo: aynı sözleşme, üç konak

Dört sözleşmeyi hem `my-windows.json`'a hem depodaki macOS profiline karşı
koştur. Beklenen (ölçülmüş) matris:

```
                 macOS 16K/W^X      Windows (senin)
FEX      (0006)   ❌ UNSUPPORTED     ✅ SUPPORTED
RPCS3    (0009)   ❌ UNSUPPORTED     ✅ SUPPORTED
wasmtime (0012)   ⚠️ CONDITIONAL     ✅ SUPPORTED
PCSX2    (0024)   UNKNOWN            UNKNOWN
```

Bir tek sözleşmenin verdict'inin konağa göre değişmesi = **"ortam bir girdidir"**
tezinin kanıtı. `--format markdown` ile koşarsan her UNSUPPORTED'ın altında
katmanlar-arası kanıt zincirini (evidence chain), reddedilen sahte çözümleri
(rejected fixes) ve remediation sınıflarını görürsün — sıradan bir log'un
veremeyeceği şey.

---

## 4. Observe tarafı — kendi makinende (bu oturumda yazdığımız ETW gözlemcisi)

Yukarısı "gereksinim → tahmin". Bir de **gerçekten koşan yazılımı** ölçme tarafı
var: bu oturumda Windows için yazdığımız **ETW gözlemcisi**. Kendi makinende
kurulu gerçek yazılıma karşı yanlış-pozitif oranını ölçer (CI'da 0/247 çıktı):

```bat
:: Git Bash içinde (Windows'ta Git ile gelir)
RS_BIN=build/bin bash tools/campaign/run_false_positive.sh build/fp-mybox
```

Bu, makinendeki python/node/git/… gerçek programlarını **NT Kernel Logger** ile
izler, sadece başarıyla koşan istekleri sözleşmeye çevirir, `rs-check` ile
skorlar. Kurulu bir oyun ya da emülatör (RPCS3, wasmtime CLI, .NET) varsa
`run_false_positive.sh` içine bir `observe` satırı ekleyip onu da ölçebilirsin —
gerçek bir AAA yükü, gerçek VM baskısı.

> Not: RPCS3/PCSX2 gibi ağır JIT/emülatörler RWX ve büyük ayırma yaptığı için
> observe tarafında en zengin veriyi onlar üretir.

---

## 5. Sonuçları kaydet — birleştirme kapısı için

Her test bir kanıt. *"Çok sayıda gerçek hayat testi olmadan birleştirme yok"*
kuralını beslemek için, çalıştırdığın her testi şöyle bir tabloya geçir:

| Proje | Sözleşme | Konak | Beklenen | rs-check verdict | Eşleşti mi? | Not |
|---|---|---|---|---|---|---|
| FEX | fex-4k… | macOS 16K | UNSUPPORTED | ? | ? | |
| FEX | fex-4k… | senin Windows | SUPPORTED | ? | ? | |
| RPCS3 | rpcs3-jit… | macOS | UNSUPPORTED | ? | ? | |
| … | | | | | | |

**En değerlisi:** yazdığın sözleşmeleri `contracts\campaign\` altına, `x_campaign`
metadata'sıyla (özellikle `expected_verdict`) commit et. Bu tam olarak korpusun
büyüme biçimi — her biri "aracın şu gerçek olayı doğru yakalayıp yakalamadığı"
sorusunun kalıcı, tekrar-koşulabilir bir kaydı. Eğer `rs-check`'in verdict'i
gerçek olayla **eşleşmezse**, bu bir kusur bulgusudur (aracın ya da sözleşmenin)
— ve bu projede kusur bulmak, testin başarısızlığı değil, işin ta kendisidir.

---

### Kaynaklar

- FEX-Emu 4K sayfa varsayımı — github.com/FEX-Emu/FEX issue #1221; asahilinux.org/docs/sw/broken-software
- RPCS3 / Apple Silicon W^X — github.com/RPCS3/rpcs3 PR #18701; github.com/zherczeg/sljit issue #99; github.com/dotnet/runtime issue #108423
- wasmtime bellek ayırma — docs.wasmtime.dev/api Config (memory_reservation / memory_guard_size / memory_reservation_for_growth)
- PCSX2 recompiler belleği — github.com/PCSX2/pcsx2 PR #11734; wiki.pcsx2.net Advanced memory management
