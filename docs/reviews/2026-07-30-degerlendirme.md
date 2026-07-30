# RuntimeSkeptic — durum değerlendirmesi

**Tarih:** 2026-07-30 · **Kapsam:** dört belge (ROADMAP / PLAN / TODO / PROGRESS),
Opus döneminde yapılan işin denetimi, bitirmeye ne kaldığı

---

## 0. Kısa cevap

Proje **sandığından daha ileride, ama Faz 3'ün çıkış kapısında (Gate B) takılı.**
İki somut gerekçe kapıyı açık tutuyor; ikisi de artık pusulada (compass) bir
maddeye sahip, ki bu değerlendirmeden önce doğru değildi.

Opus döneminde yapılan işte **uydurma, abartı ya da sessizce yanlış bir ölçüm
bulamadım.** Bulduğum şey farklı bir sınıftı ve tekrarlıydı: *belgeler kendi
durumları hakkında doğruyu söylüyordu, ama hiçbir mekanizma o doğruluğu
denetlemiyordu.* Üçü aşağıda.

---

## 1. Dört belge modeli — sahibin kuralı artık mekanik

Senin koyduğun kural:

```
ROADMAP.md          spesifikasyon   BOZULMAMALI
docs/PLAN.md        harita          BOZULMAMALI (yalnız durum işaretleri hareket eder)
docs/TODO.md        pusula          işler ilerledikçe DEĞİŞMELİ
docs/PROGRESS.md    geçmiş          append-only, en yeni en üstte
```

Bu kural artık bir cümle değil, çalışan kod:

| Ne | Nasıl |
|---|---|
| **+** ROADMAP donduruldu | `tools/guards/check_roadmap.py` — SHA-256 ile pinlenmiş (`roadmap.sha256`). Kazara bir düzenleme CI'ı düşürür; kasıtlı bir düzenleme *yüksek sesle karar* olur: aynı commit'te hash güncellenir, gerekçe PROGRESS'e yazılır |
| **+** PLAN aynayı tutmak zorunda | ROADMAP'in tanımladığı 11 fazın her biri PLAN'da anılmalı. Faz numaraları ROADMAP'in **kendisinden** okunuyor, guard'ın kendi kopyasından değil |
| **+** Pusula ve harita çelişemez | `check_todo.py` — PLAN'daki her bitmemiş kriter, TODO'daki bir maddenin id'sini taşımalı ya da açıkça `(untracked)` olup gerekçesi yazılmalı |
| **+** Haftalık bakım otomatik | Zamanlanmış görev (scheduled task), **Pazartesi 06:00 UTC**. Depoyu klonlar, git-ref CI kanallarını okur, 17 guard'ı çalıştırır, TODO/PROGRESS'i gerçeklikle karşılaştırır ve değişiklik varsa dosya olarak teslim eder |

**Bakım görevi hakkında dürüst not:** her tetiklenme **taze bir konteynerde**
açılıyor, yani GitHub kimlik bilgisi orada yok. Görev **push edemez**. Public
depoyu kimliksiz klonlayıp okuyabilir, guard'ları çalıştırabilir, güncellenmiş
belgeleri sana dosya olarak yollayabilir — ama commit'i sen (ya da o oturumda
kimlik sağlanırsa o) atmak zorunda. Bunu görevin kendi metnine yazdım ki her
hafta yeniden keşfetmesin.

---

## 2. Denetim: Opus döneminden ne çıktı

### 2.1 `[partial]` — hiçbir şeyin bakmadığı durum

`check_todo.py`, PLAN'daki `[open]` ve `[blocked]` kriterleri okuyordu.
`[partial]`'ı **okumuyordu.**

Bu, bu projenin *tam da bir şey yarı-doğru olduğunda ve bunu yüksek sesle
söylemek gerektiğinde* kullandığı işaret. Yani "burada bitmemiş iş var ve bu
konuda dürüst davranıyorum" diyen tek durum, hiçbir guard'ın bakmadığı durumdu.
Üç kriter orada oturuyordu:

| | Bulgu |
|---|---|
| **−** | **Gate B, ikinci gerekçe.** `RS-VM-0005` gerçek eşlemelerin **%42'sinde** tetikleniyor — doğru, `PROVEN`, ve bir kapıda kullanılamaz. Haritada kapının açık kalma nedeni olarak *adı geçiyordu*, ve pusulada **hiçbir maddesi yoktu.** Artık **T-019** |
| **−** | **Gate B, birinci gerekçe** `(T-004)` ile etiketliydi — Windows probe, `[done]`. Düz okunuşu: *kapıyı açık tutan iş tamamlandı.* Etiket yazıldığında doğruydu; altındaki gerekçe değişti, etiket kalmıştı. Artık `(T-018)` |
| **−** | **"rule coverage by execution"** `[partial]` ve sahipsizdi. İki kuralın **hiçbir türden** kapsamı yoktu. Artık **T-020**, ve kapatıldı (§3) |

Yeni iki kural + dört selftest vakası. `[partial]` artık okunuyor, ve
**bitmemiş bir kriter `[done]` bir maddeye ait olamaz.**

### 2.2 Aynı hatayı iki kez yaptım, aynı oturumda

Dürüst kayıt: `[partial]`'ı işaret kümesine eklemek **doğru belgeler üzerinde
iki yanlış alarm** üretti.

```
1. cümle ortasında geçen durum    "Still `[partial]` because the prose is
                                   unchecked" bir kriter sanıldı
                                   → check_plan.py'de AYNI hata, AYNI düzeltme,
                                     beş gün önce, yine benim tarafımdan

2. satır başına denk gelen durum   "...wrong again.\n  `[partial]` while the
                                    bucket is a backlog (T-021)"
                                   → ikiye bölündü, üstteki kriter kendi
                                     etiketini kaybetti ve sahipsiz raporlandı
```

Kural artık kesin: **bir kriter, ya sütun 0'da başlayan bir durum işaretidir ya
da bir madde imi (bullet) ile başlatılmış olandır.** Girintili ve imsiz bir
durum işareti, bir devam satırıdır — başka bir şey değil. İki yanlış alarm da
selftest vakası olarak duruyor, çünkü işaret kümesini genişletecek üçüncü kişi
bu hatayı yine yapacak.

### 2.3 Genel şekil — altıncı ya da yedinci kez

Bu depodaki kusur **hiçbir zaman herkesin baktığı yerde değil**:

```
[partial]                    hiçbir guard'ın okumadığı durum
Windows stub                 linker'ın sessizce tercih ettiği boş implementasyon
`unknown` satırları          ölçülen konağa göre anahtarlanmıştı
ctest -C'siz                 her Windows hatası "Missing -C" yazdı, ömrü boyunca
EEXIST sınıflandırması       probe'un kendi yerleşimine bağlıydı
```

Şüpheyi ifade etmek için var olan ve hiçbir şeyin denetlemediği bir durum,
hiç durum olmamasından **daha kötüdür** — özen gibi okunur.

---

## 3. T-020 kapatıldı (ve içindeki tahmin yanlıştı)

İki kuralın hiçbir türden kapsamı yoktu: ne ground-truth vakası, ne birim testi,
ne sentetik profil. `groundtruth_coverage.py` artık `0 have none at all` yazıyor.

| | |
|---|---|
| **+** | `RS-VM-0016` ve `RS-VM-0025` artık test ediliyor, **her biri negatif yarısıyla** — susması gereken konak. Yalnız tetiklendiği yerde test edilen bir kural, her yerde tetikleniyor olabilir |
| **+** | İkisi de **bilerek sentetik** test ediliyor ve gerekçe test dosyasının içinde: `RS-VM-0016` ölçümle erişilemez çünkü bu projenin dokunabildiği **her** runner'da primitif var (`MAP_FIXED_NOREPLACE`, `VM_FLAGS_FIXED`, `VirtualAlloc2`); `RS-VM-0025` ROADMAP §11'e göre `PREDICTIVE` — parçalanma (fragmentation) hakkında bir öngörü, ve bir ölçüm öngörüyü doğrulayamaz |
| **−** | Maddenin kendi tahmini yanlıştı: `RLIMIT_AS` lane'inin `RS-VM-0025`'i zaten tetiklediğini sanmıştım. **Tetiklemiyor.** `max_user_address` **tek sayfalık** `MAP_FIXED_NOREPLACE` ile ölçülüyor, ve `RLIMIT_AS` tek sayfayı ücretlendirmiyor — yani kısıtlı konak, kısıtsız olanla aynı mimari tavanı bildiriyor. Senin T-015 için doğru cevap yapan asimetri, burada işe yaramaz kılan şeyin ta kendisi |

Bir üst kova artık dürüst olan: bir grup kural birim testine sahip ve hiç
çekirdeğe gösterilmemiş. **T-021** olarak `Later`'a yazıldı — ve bilerek
"sıfıra indirilecek bir sayı" olarak değil, çünkü o kuralların bir kısmı bu
projenin erişebildiği hiçbir konakta çalıştırılamaz; **gerekçeyi yazmak işin
kendisi.**

Maddeye sayı **bilerek yazılmadı**, ve bu aynı oturumda yaptığım bir hatanın
düzeltmesi: önce "14" yazmıştım — tek konaklık bir çalıştırmadan. CI aracı
**iki** konak üzerinden çalıştırıyor (kısıtsız + `RLIMIT_AS` kısıtlı) ve 13
buluyor. Tek sayı, iki farklı ölçüm aleti, ve düzyazı yanlış olanı seçti — ki
bu, "13 of the 20 reachable" ifadesini en baştan üreten hatanın ta kendisi.
Sayıyı araç yazdırır; hiçbir belge onu tekrar etmez.

**CI'ın 13fdaf3 için ölçtüğü (her iki konak):**

```
gerçek çekirdeğe karşı çalıştırılan  10 / 23   (%43)
yalnızca sentetik kapsam             13
hiçbir türden kapsamı olmayan         0        ← T-020 bunu boşalttı
```

---

## 4. Proje gerçekte nerede

```
Faz 0  taksonomi + korpus         BİTTİ      korpus 44/30, vm 35/10
Faz 1  ortam probe'u              BİTTİ      Linux + macOS ×2 + Windows,
                                             hepsi ölçülmüş, hepsi aralık kuruyor
Faz 2  semantik IR + değerlendirici BİTTİ
Faz 3  VM analizör MVP            KISMİ      tek çıkış kriteri: Gate B
Faz 4  runtime wrapper            AÇIK       başlanmadı
Faz 5  CodeSkeptic entegrasyonu   BLOKE      senin talimatın
Faz 6-10                          AÇIK
```

Ölçülebilir gövde:

| | |
|---|---|
| kod | 13 230 satır (`src/` + `include/`) |
| test | 4 926 satır, **254 birim test vakası**, 15 test paketi |
| guard | **17**, hepsi CI'da her push'ta, **87 selftest vakası** ile kendileri test edilmiş |
| kural | 30 bulgu id'si (`RS-VM-00xx`) |
| commit | 120 |
| kapı | Gate A ✅ · Gate B kısmi · Gate C bloke · Gate D yok |

---

## 5. Bitirmek için ne kaldı

**Gate B'yi geçmek = Faz 3'ü bitirmek.** İki gerekçe var, ikisi de artık sahipli:

```
T-019  RS-VM-0005 kararı                 UCUZ    [next]
       %42'de tetikleniyor. Üç seçenek:
         (a) bilgilendirmeye indir
         (b) gereksinim "tam boyut" diyebiliyorsa koşullu yap
         (c) tut, ve %42'yi kapı metnine yaz
       İlk adım: requirement modelinin exactness'ı ifade edip
       edemediğini oku. Edemiyorsa (b) tek satırlık değil.

T-018  Kampanya Linux'tan çıkıyor        PAHALI  [next]
       "0 yanlış pozitif" tek OS'ta ölçüldü. strace Linux-only.
       macOS'ta dtrace (SIP izin verirse) ya da Windows'ta ETW.
       Windows daha değerli (adres alanı davranışı cins olarak farklı,
       probe orada 127 TiB kuruyor) ve daha zor.
```

Bunlardan sonra ROADMAP'in Faz 4+ kısmı var, ama **senin kendi kuralın** — *"bir
çok gerçek hayat testi olmadıkça birleştirme yok"* — orada duruyor. Faz 3'ü
gerçek bir kapıyla kapatmak, Faz 4'e başlamaktan daha değerli.

**Benim önerim, sırayla:**

```
1. T-019   karar + yeniden ölçüm          → Gate B'nin yarısı kapanır
2. T-018   ikinci OS'ta tracer            → Gate B kapanır, Faz 3 biter
3. dur     ve gerçek hayat testleri       → senin kuralın
```

---

## 6. Açık kalanlar — senin kararın gereken

| Konu | Durum |
|---|---|
| **PAT** | Mevcut token geçersiz (`Invalid username or token`). **3 commit yerelde bekliyor.** Ayrıca teşhis sırasında token'ı transkripte bastım — iptal edilmeli. Bu benim hatam |
| **T-011 / Faz 5** | CodeSkeptic bitti. Diferansiyel test onun çıktısını **değiştirmeden tüketir**. Senin bir kelimen bloğu açar |
| **macOS CI sıklığı** | Repo public olduğu için `macos-14` artık ücretsiz. Per-push'a geri alınabilir; şu an alınmadı |

---

*Bu belge bir anlık görüntüdür ve `docs/PROGRESS.md`'nin yerine geçmez. Kalıcı
kayıt oradadır.*
