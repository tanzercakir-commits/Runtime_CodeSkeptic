# W^X / JIT avı — Apple Silicon (M1) canlı gözlem prompt'u

Amaç: 16K avı **statik** kanıtla (ELF) sınırlıydı ve yüzey kapalıydı. Bu av
**canlı**: elindeki M1, W^X (write-xor-execute) ve JIT-entitlement sınıfını
*çalışırken* gözlemleyebilir. Bu, aracın "honest limit"ini (statik ≠ canlı)
tam da kapatan şey.

İki iş birden (senin seçtiklerin, 1 + 3):

```
MOD 1 — BİLİNENİ DOĞRULA (option 1)        statik tahmini CANLI kanıta çevir
  Araç box64'ün dynarec'i için RS-VM-0009/0011 UNSUPPORTED diyor (Kart 2).
  M1'de box64'ü koştur, gerçekten ne yaptığını gözlemle:
    - RWX'i tek mmap'te isteyip tutuyorsa  → tahmin CANLI DOĞRULANDI.
    - W→X flip / MAP_JIT yapıyorsa          → tahmin ÇÜRÜDÜ (bu build uyumlu).
  İkisi de kazanç: biri "araç haklı", diğeri "sözleşme eski commit'e sabit,
  güncel box64 Apple Silicon'a uyarlanmış" — ikisi de dürüst bilim.

MOD 2 — YENİ AVLA (option 3)                raporlanmamış bir W^X kurbanı
  Naif RWX yapan (MAP_JIT'siz, flip'siz), Apple Silicon'a taşınmamış bir
  JIT/dynarec/FFI. Ana-akım JIT'ler (V8, JSC, LuaJIT, Mono, .NET) bunu ÇOKTAN
  çözdü — av kenarda: az-bakımlı yorumlayıcılar, libffi trampolinleri, eski
  dil çalışma-zamanları, eklenti/mod yükleyiciler.
```

---

## Sınıf: Apple Silicon ne zorlar

```
RS-VM-0009  simultaneous write+execute restricted
  Bir sayfa aynı ANDA yazılabilir VE çalıştırılabilir olamaz. Klasik
  mmap(PROT_WRITE|PROT_EXEC) bir sayfayı böyle ister → reddedilir.

RS-VM-0011  executable memory requires an entitlement
  Çalıştırılabilir bellek, hardened runtime altında bir entitlement ister:
    com.apple.security.cs.allow-jit                    (MAP_JIT yolu)
    com.apple.security.cs.allow-unsigned-executable-memory  (naif RWX yolu)

Apple'ın kutsadığı JIT yolu (uyumlu, BULGU DEĞİL):
    mmap(..., PROT_READ|PROT_EXEC, MAP_JIT, ...)        (flags & 0x800)
    + pthread_jit_write_protect_np(false) yaz / (true) çalıştır  (per-thread flip)

W^X-uyumlu ikinci yol (uyumlu, BULGU DEĞİL):
    mmap W → kod yaz → mprotect X   (aynı anda değil, SIRAYLA flip)
```

Not: macOS'te naif RWX, `allow-unsigned-executable-memory` entitlement'ı VARSA
çalışabilir — yani macOS'te olay entitlement-kapılı. Asahi Linux aarch64'te
(16K + W^X birlikte) daha serttir. Bu yüzden en temiz canlı kanıt: **entitlement
statik okuması + runtime gözlemi birlikte.**

---

## Enstrüman: mevcut gözlemciyi yeniden kullan (yeni araç YOK)

`tools/campaign/observe_requirements.py` zaten macOS'te dtrace ile hem `mmap`
(prot + **flags**, yani PROT_EXEC=0x4 ve MAP_JIT=0x800 görünür) hem `mprotect`
hem mach `vm_protect` kapısını izliyor; W→X geçişini (`write_then_execute_pairs`)
zaten çıkarıyor. Yani W^X av için ekstra kod gerekmez.

```bash
# Bir hedefi 3 koşu boyunca gözle (SIP kapalı olmalı; macos-14 runner'da kapalı):
sudo tools/campaign/observe_requirements.py \
    --out /tmp/wx/box64 --runs 3 --label box64-hello \
    -- box64 /path/to/some-x86_64-binary

# Çıktı DIR'ında her mmap/mprotect/vm_protect için RSOBS satırları + türetilmiş
# requirement JSON'ları olur.
```

Entitlement'ı statik oku (kurban macOS app'iyse):
```bash
codesign -d --entitlements :- /path/to/App.app 2>/dev/null | \
    grep -E 'allow-jit|unsigned-executable|allow-dyld-environment'
```

---

## Karar ağacı (gözlenen protection davranışını sınıfla)

```
her mmap / mprotect / vm_protect kaydı için:
│
├─ mmap prot = WRITE|EXEC (tek çağrı, prot & 0x6 == 0x6)
│   ├─ flags & MAP_JIT (0x800)  → UYUMLU (Apple JIT yolu). Bulgu değil.
│   └─ MAP_JIT yok              → ADAY. Naif RWX. RS-VM-0009 shape.
│                                  entitlement var mı? yoksa canlı fault beklenir.
│
├─ mmap W (yalnız) … sonra mprotect/vm_protect aynı adrese EXEC
│   → W→X FLIP. W^X-uyumlu. Bulgu değil. (write_then_execute_pairs bunu verir.)
│
└─ ne RWX ne flip → alakasız (veri/yığın). Atla.
```

**Aday = "MAP_JIT yok" dalı + entitlement yok + canlı bir EXC_BAD_ACCESS/SIGKILL.**
Sadece "RWX istedi ama entitlement'ı var ve çalıştı" → uyumlu, rapor değil.

---

## Hedef listesi (dürüst novelty notlarıyla)

| Hedef | Sınıf | Neden aday / novelty riski |
|---|---|---|
| **box64** (güncel) | dynarec | MOD 1 doğrulama çıpası. Muhtemelen artık W→X flip yapıyor → tahmini çürütür (değerli). |
| **libffi kapanışları** (closures) | FFI trampolin | tarihsel RWX; statik-trampolin düzeltmesi her yere gitmedi. Bir libffi kullanan eski app iyi aday. |
| **Mono/eski Unity** oyun | JIT | modern Mono uyumlu; ESKİ gömülü Mono taşımamış olabilir. |
| **eski Lua/LuaJIT gömen app** | JIT | LuaJIT MAP_JIT'i yeni ekledi; eski gömme naif RWX taşır. |
| **QEMU TCG** (user-mode) | dynarec | Apple Silicon'a uyarlı; ama belirli bir build/bayrak naif olabilir. |
| **az-bakımlı yorumlayıcı** (forth, scheme, jit'li regex) | JIT | ana-akım dışı = kapanmamış yüzey. En yüksek novelty. |

Ana-akım = büyük olasılıkla uyumlu (flip/MAP_JIT). Novelty kenarda: bir şeyin
**hem** naif RWX yaptığını **hem** raporlanmamış olduğunu göster.

---

## Her aday için (bulguların bana gelince, ben buradan)

```
1. Gözlenen requirement'ı al (observe_requirements.py çıktısı) → sözleşme yaz.
2. rs-check ile macos-14-arm64-native profiline karşı koştur:
     build/bin/rs-check <contract>.json \
       --profile profiles/measured/macos-14-arm64-native.measured.json --bundle DIR
   → RS-VM-0009/0011 UNSUPPORTED bekliyorum (naif RWX ise).
3. rs-replay ile bundle'ı doğrula (tamper-evident, hash-checked).
4. Upstream'de W^X/apple-silicon/MAP_JIT issue taraması → raporlanmamış mı?
5. Raporlanmamış + canlı fault + eşleşen tahmin = rapor edilebilir kanıt zinciri.
```

---

## Dürüst sınırlar (yanmamak için)

- **macOS ≠ Asahi.** macOS'te naif RWX entitlement'la çalışabilir; bu bir "bulgu"
  değil, "izinli". Gerçek W^X reddi ya entitlement'sız hardened runtime ister
  ya da Asahi Linux aarch64. Hangisinde gözlediğini yaz.
- **pthread_jit_write_protect_np bir syscall DEĞİL** — per-thread hızlı flip,
  dtrace'te mmap/mprotect gibi görünmez. Yani "flip görmedim" ≠ "flip yok";
  MAP_JIT'li mmap'i görürsün, gerçek yazma-koruması geçişini göremezsin.
  (observe_requirements.py bunu zaten not ediyor.)
- **Canlı fault gerçek kanıttır; sadece RWX-isteği tahmindir.** Fault yoksa
  HYPOTHESIS, verilmez.
- **Ana-akım büyük olasılıkla temiz.** 16K avı gibi: negatif de sonuçtur.
  Buradan "araç W^X'i doğru modelliyor, yüzey ana-akımda kapalı, novelty
  kenarda" çıkarsa, o da yayınlanabilir dürüst bir bulgudur.

---

## Bana ne getir

Her hedef için:
```
hedef + sürüm
gözlenen protection olayları (RSOBS satırları ya da observe_requirements çıktısı)
  - RWX-tek-çağrı? MAP_JIT? W→X flip?
entitlement (codesign çıktısı, macOS app'iyse)
canlı sonuç: çalıştı mı / EXC_BAD_ACCESS / SIGKILL?
üzerinde koştuğun konak: macOS-arm64 (hardened?) / Asahi aarch64?
```
Ben her biri için sözleşme + rs-check verdict + bundle + upstream-taraması yaparım.
```
```
Yardımcı olur: Kart 2'yi (box64) önce koş — enstrümanın çalıştığını ve
tahminle canlıyı yan yana koyabildiğimizi kanıtlar; av oradan güven kazanır.
```
