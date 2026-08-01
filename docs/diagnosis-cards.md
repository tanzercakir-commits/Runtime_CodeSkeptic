# RuntimeSkeptic — tanı kartları (paylaşılabilir örnekler)

**RuntimeSkeptic nedir, tek cümle:** bir programın sanal-bellek (VM) gereksinimini
bir konağın *ölçülmüş* profiliyle karşılaştırıp "burada çalışır / çalışmaz / şu
koşulla" der — ve cevabı **katmanlar arası bir kanıt zinciriyle** verir, sıradan
bir log'un veremeyeceği şekilde.

**Bu kartlar ne, ne değil (dürüst çerçeve):** aşağıdakiler gerçek, **kaynağı
gösterilen** olayların *tanısıdır* — projelerin kendi kodundaki varsayımı, aracın
o varsayımı hiç çalıştırmadan, sadece konak profilinden türeterek yakalaması. Bu
bir **doğrulama**, henüz "yeni bir bug bulduk" değil. İkisi farklı ağırlıkta;
ikincisi (pre-flight ile keşif) bir sonraki adım.

**Kredibilite çıpası:** araç, üç işletim sisteminde (Linux/macOS/Windows) çekirdek
tarafından **gerçekten gözlemlenmiş 1576 başarılı** eşleme isteğinde **0 yanlış
pozitif** verdi. Yani "çalışır" dediğine güvenilir; "çalışmaz" dediği de bu yüzden
inandırıcı.

Her kart `rs-check` ile tekrar üretilebilir — komut kartın altında.

---

## Kart 1 — Redis, Apple Silicon'da açılmıyor (4K vs 16K sayfa)

> Redis'in paketlediği **jemalloc**, 4 KiB sayfa varsayımıyla (LG_PAGE=12)
> derlendiğinde, 16 KiB sayfalı bir Apple Silicon Mac'te **`Unsupported system
> page size` ile ölür.** Araç bunu, jemalloc'u hiç çalıştırmadan söylüyor.

```
Proje / kaynak : redis → deps/jemalloc/src/pages.c:760
                 if (os_page > PAGE) { malloc_write("Unsupported system page size"); }
Konak          : macOS 14 arm64 (16 KiB sayfa, ölçülmüş)
Verdict        : UNSUPPORTED     (kendi Linux'unda: SUPPORTED — konağa göre değişir)
Bulgu          : RS-VM-0006 (critical) — host page size differs from required

Kanıt zinciri (katmanlar arası):
  application       → "program requires page size 4096"   (jemalloc LG_PAGE=12)
  operating_system  → "host page size is 16384"            (rs-env-probe: macOS sysconf)
  →                   "No mapping request can repair this."
```

**Neden log bunu söyleyemez:** log ancak Redis çöktükten *sonra*, o Mac'te
görülür. Araç, Redis'i o makineye hiç kurmadan, profilden **önceden** söyler.

**Tekrar üret:**
```
rs-check contracts/campaign/redis-jemalloc-page-size-lg12.json \
  --profile profiles/measured/macos-14-arm64-native.measured.json --format markdown
```

---

## Kart 2 — box64'ün dynarec'i Apple Silicon'da W^X'e çarpıyor

> **box64** (x86 programlarını ARM'da koşturur — Asahi, Steam Deck üzeri ARM)
> üretilen makine kodunu aynı sayfaya yazıp çalıştırır. Apple Silicon **W^X**
> (write-xor-execute) zorunlu kılar: bir sayfa aynı anda yazılabilir *ve*
> çalıştırılabilir olamaz. Araç iki ayrı kuralla yakalıyor.

```
Proje / kaynak : box64 → src/custommem.c:1801-1806  (dynarec blok ayırıcı)
Konak          : macOS 14 arm64 (W^X zorunlu, ölçülmüş)
Verdict        : UNSUPPORTED     (Linux/Windows'ta: SUPPORTED)
Bulgular       : RS-VM-0009 (high) — simultaneous write+execute restricted
                 RS-VM-0011 (high) — executable memory requires an entitlement

Modellenen sonuç : "protection can be flipped between writable and executable,
                    but never both at once"
Sonuç            : "Write-xor-execute is a security policy, not a defect. The
                    program's memory model has to change; the platform's will not."
```

**Dürüstlük ayrıntısı:** araç, box64'ün gerçek **failure sink**'ini de doğru
okuyor — box64 çökmüyor, `Cannot create dynamic map` yazıp o blok için dynarec'i
kapatıp yavaş yoldan devam ediyor (`custommem.c:1805`). Yani "kırılır" derken
*nasıl* kırıldığını da biliyor.

**Tekrar üret:**
```
rs-check contracts/campaign/box64-dynarec-rwx-block.json \
  --profile profiles/measured/macos-14-arm64-native.measured.json --format markdown
```

---

## Kart 3 — QEMU'nun i386 yükleyicisi Windows'ta sabit adrese oturamıyor (64K granülarite)

> **QEMU user-mode**, bir i386 ELF çalıştırılabilirini klasik `0x8048000` sabit
> adresine `MAP_FIXED_NOREPLACE` ile haritalar. Windows'un **tahsis granülaritesi
> 64 KiB** — ve `0x8048000` 64 KiB'ye bölünmüyor. Yani bu adrese tam yerleşim
> Windows'ta imkânsız. (Senin platformun; sen doğrulayabilirsin.)

```
Proje / kaynak : qemu → linux-user/elfload.c:1074
                 (ehdr->e_type == ET_EXEC ? MAP_FIXED_NOREPLACE : 0)
Konak          : Windows x86_64 (allocation granularity 65536, ölçülmüş)
Verdict        : UNSUPPORTED     (Linux'ta: SUPPORTED)
Bulgu          : RS-VM-0004 (high) — requested address doesn't satisfy granularity

Kanıt zinciri (katmanlar arası):
  application       → "program requires an exact mapping at 0x8048000"  (elfload.c:1074)
  operating_system  → "allocation granularity is 65536 bytes"  (rs-env-probe: GetSystemInfo)
  analyzer          → "0x8048000 % 65536 != 0"                 (hizalama aritmetiği)
  →                   "an exact placement is impossible."
```

**Dürüstlük ayrıntısı:** araç bu koşumda bir de `RS-VM-0017 (HYPOTHESIS)` basıyor
— "bu isteğin dayandığı bir olgu hiç ölçülmedi". Yani asıl bulguyu (0004)
verdikten sonra, *bilmediği* ikinci bir şeyi de bilmediği olarak işaretliyor. Blöf
yok.

**Tekrar üret:**
```
rs-check contracts/campaign/qemu-i386-etexec-fixed-noreplace.json \
  --profile profiles/measured/windows-server-2025-x86_64.measured.json --format markdown
```

---

## Toparlama

Üç farklı kural sınıfı (sayfa boyutu · W^X · tahsis granülaritesi), iki konak
ailesi (Apple 16K · Windows 64K), üç ünlü proje, hepsi **kendi kaynak
satırından** — ve araç hepsini konağı hiç çalıştırmadan, profilden türeterek
söylüyor. 37 gerçek olaydan 23'ü konağa göre verdict değiştiriyor: **"ortam bir
girdidir"** tezi ölçekte.

**Sınır, tekrar:** bunlar *bilinen, kaynağı gösterilen* olayların tanısı —
"yeni bulduk" değil. Bir sonraki adım (pre-flight ile keşif): az-test-edilmiş bir
konağı hedefleyip, bir projenin varsayımını çıkarıp, **gerçek konakta doğrulayıp**
upstream'e vermek. O zaman kart "biz bulduk" olur.
