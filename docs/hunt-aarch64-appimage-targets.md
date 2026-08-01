# 16K av — daraltılmış aarch64 AppImage hedef listesi

Amaç: **güncel, bakımlı bir aarch64 AppImage'da, 16K sayfalı konakta (Asahi /
Pi5 16K / Apple Silicon Linux) kırılan AMA upstream'de raporlanmamış** bir
uyumsuzluk bulmak → RuntimeSkeptic ile tahmin et → upstream'e ver.

Daraltma çalıştı: çoğu AppImage yalnız x86_64. aarch64 yayınlayanlar küçük bir
küme, ve içlerinde **iki ayrı sınıf** var — ve bu sınıflar **farklı tarama
yöntemi** ister. Bunu karıştırmak yanlış negatif üretir.

---

## İki sınıf, iki yöntem (bunu karıştırma)

```
SINIF A — ELF YÜKLEME HİZASI                    ldso_predicate.py YAKALAR
  ld.so bir bundled .so'yu (p_vaddr − p_offset) mod 16K ≠ 0 olduğu için
  yüklemeyi REDDEDER → uygulama hiç açılmaz.
  Örnek: MuseScore/libsndfile (bizim ilk avımız). Kök neden: appimagetool /
  patchelf 4K varsayımıyla paketlenmiş.
  → Senin ldso_predicate.py'in TAM da bunu yakalar (statik ELF taraması).

SINIF B — ELECTRON / V8 ÇALIŞMA-ZAMANI          ldso_predicate.py YAKALAMAZ
  Binary sorunsuz YÜKLENİR, sonra renderer V8'in sabit-kodlu 4K sayfa
  varsayımı yüzünden ÇÖKER (Chromium 132 regresyonu).
  → ELF hizası normaldir; ldso_predicate burada YANLIŞ NEGATİF verir.
  → Sinyal: bundled ELECTRON SÜRÜMÜ (ELF değil). Aşağıda ayrı yöntem.
```

Doğrulama (bugünün gerçeği, kaynaklı):
- Electron 16K çökmesi çalışma-zamanı, ELF değil — Chromium 132'nin sabit 4K
  sayfası; workaround `--js-flags="--nodecommit_pooled_pages"`
  ([electron#45560](https://github.com/electron/electron/issues/45560)).
- **Kırık pencere: Electron 34.0.2 – 34.2.x** (Chromium 132–133).
  **Düzeltildi: Electron 34.3.0+** (backport) veya 35+ (Chromium 134+).
  **Sağlam: Electron ≤ 33.3.2** (regresyon öncesi).
  ([Vesktop#1125](https://github.com/Vencord/Vesktop/issues/1125),
  [vscode#242742](https://github.com/microsoft/vscode/issues/242742)).

---

## Sınıf A — native aarch64 AppImage (ldso_predicate.py ile tara)

Bunlar appimagetool/patchelf ile paketlenir; bundled .so'ları 4K-hizalı kalmış
olabilir. **ldso_predicate.py tam da bunları yakalar.**

| Proje | aarch64 AppImage? | Neden aday | Bilinen 16K issue? |
|---|---|---|---|
| **FreeCAD** | ✅ DOĞRULANDI (`FreeCAD_*-Linux-aarch64.AppImage`, FreeCAD-Bundle) | Qt + çok sayıda bundled .so, appimagetool | tarancak |
| **Kdenlive** | ❓ kontrol et (KDE binary-factory çoğunlukla x86_64) | KDE/Qt, çok bundled lib | tarancak |
| **Krita** | ❓ kontrol et (tarihsel x86_64-only) | Qt, bundled lib | tarancak |
| **Audacity** | ❓ kontrol et | wxWidgets + bundled lib | tarancak |
| **darktable** | ❓ kontrol et | çok bundled lib | tarancak |
| **OpenSCAD** | ❓ kontrol et | Qt, bundled lib | tarancak |

**En sağlam başlangıç: FreeCAD** — aarch64 AppImage'ı kesin var, ve MuseScore
gibi çok sayıda bundled .so taşıyor.

**Tara (Sınıf A):**
```
# aarch64 AppImage'ı indir, çıkar:
./FreeCAD_*-Linux-aarch64.AppImage --appimage-extract
# ldso_predicate.py'ini squashfs-root ağacına doğrult:
python3 ldso_predicate.py squashfs-root
# 16K yordamını (p_vaddr − p_offset) mod 16384 ≠ 0 ihlal eden .so listesini al.
```

---

## Sınıf B — Electron aarch64 AppImage (SÜRÜM ile kontrol et, ldso ile DEĞİL)

Bunlar yüklenir sonra çöker; ELF taraması işe yaramaz. Sinyal bundled Electron
sürümü. **Kırık pencere: 34.0.2–34.2.x.**

| Uygulama | aarch64 AppImage? | Durum |
|---|---|---|
| Obsidian | ✅ | ZATEN RAPORLU (forum, Asahi/Pi5) — atla |
| VSCode | ✅ (arm64) | ZATEN RAPORLU (vscode#242742) — atla |
| Vesktop | ✅ | DÜZELTİLDİ (34.3.0) — atla |
| **Joplin** | ✅ (AppImage arm64) | **kontrol et** — bundled Electron sürümü kırık pencerede mi? |
| **Logseq** | ✅ | **kontrol et** |
| **Element Desktop** | ✅ | **kontrol et** |
| **Standard Notes / Notesnook** | ✅ | **kontrol et** |
| **Cryptomator** | ✅ | **kontrol et** |
| **Mattermost / Ferdium / Zettlr** | ✅ | **kontrol et** |

Raporlanmamış aday = kırık-pencere Electron bundle'layan + 16K issue'su olmayan.

**Kontrol et (Sınıf B):**
```
./App-*-arm64.AppImage --appimage-extract
# Bundled Chromium/Electron sürümünü oku:
strings squashfs-root/*ppRun* squashfs-root/**/electron 2>/dev/null | grep -oiE 'Chrome/[0-9]+' | sort -u
# ya da:
find squashfs-root -iname 'version' -exec cat {} \;
# Chrome/132 veya Chrome/133  → KIRIK pencere (Electron 34.0–34.2)
# Chrome/130 (ya da düşük)     → sağlam (regresyon öncesi)
# Chrome/134+                  → düzeltilmiş
```

---

## Her aday için (senin bulguların bana gelince)

```
Ben (buradan):
  1. RuntimeSkeptic verdict'i — o proje için bir sözleşme yazıp 16K profiline
     karşı koştururum (Sınıf A: ELF-hizası sözleşmesi, Sınıf B: sayfa-boyutu
     sözleşmesi + çalışma-zamanı failure_sink).
  2. Upstream'de 16K/asahi/aarch64 issue var mı taraması (raporlanmamış mı?).
  3. Raporlanmamışsa: bayt-düzeyi kanıt + kanıt zinciri + tekrar-üretme reçetesi.
```

## Dürüst sınırlar (yanmamak için)

- **Novelty = raporlanmamış.** Bilinen/düzeltilmiş bir şeyi upstream'e vermek
  değer katmaz. Her aday için önce issue taraması.
- **Doğrulama.** Statik kanıt (ELF, ya da bundled sürüm) kuvvetlidir ama
  canlı 16K çalıştırma değildir — o Asahi/Pi5/Apple-Silicon-Linux ister.
  Statik + eşleşen açık issue = rapor edilebilir; sadece tahmin = HYPOTHESIS,
  verilmez.
- **RuntimeSkeptic TAHMİN eder, TARAMAZ.** Binary'yi tarayan `ldso_predicate.py`
  (Sınıf A) ve sürüm kontrolü (Sınıf B); RuntimeSkeptic ise "bu gereksinim bu
  konakta ne yapar" sorusunu, katmanlar-arası kanıtla cevaplar. İkisi ayrı iş.

---

### Kaynaklar
- [electron#45560 — 16K page crash](https://github.com/electron/electron/issues/45560)
- [Vesktop#1125 — fixed in Electron 34.3.0](https://github.com/Vencord/Vesktop/issues/1125)
- [vscode#242742 — 16KB page crash from Electron 34](https://github.com/microsoft/vscode/issues/242742)
- [Obsidian forum — Asahi/Pi5 16K crash](https://forum.obsidian.md/t/crash-on-asahi-linux-fedora-remix-raspberry-pi-arm64-rendering-is-broken-on-aarch64-machines-with-16k-pages/99817)
- [FreeCAD aarch64 AppImage](https://github.com/FreeCAD/FreeCAD-Bundle/releases)
- [Asahi Linux — Broken Software](https://asahilinux.org/docs/sw/broken-software/)
