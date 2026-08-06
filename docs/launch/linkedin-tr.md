# LinkedIn postu — Türkçe

İkinci açık kaynak projemi paylaşmak istiyorum: **RuntimeSkeptic v0.2.0**.

Bir programın platform hakkında sessizce yaptığı varsayımları, kullanıcıya
ulaşmadan önce görünür hale getirmek için geliştirdim.

RuntimeSkeptic; uygulamanın bildirdiği çalışma zamanı gereksinimlerini, hedef
makineden ölçülen gerçeklerle karşılaştırıyor ve sonucu kanıt zinciriyle
veriyor: destekleniyor, desteklenmiyor, koşullu veya bilinmiyor.

Örneğin:

- Apple Silicon'da 16 KiB sayfa boyutu,
- Windows'ta 64 KiB ayırma granülerliği,
- W^X ve çalıştırılabilir bellek kısıtları,
- sabit adres ve reserve/commit varsayımları.

v0.2 ile birlikte uyumluluk analizörü, kararlı C ABI'li runtime monitor,
sınırlı ve deterministik trace/replay, yeniden üretilebilir kanıt paketleri ve
MCP arayüzü tek bir bağımsız açık kaynak üründe buluşuyor.

Linux'ta GCC ve Clang, Apple Silicon'da AppleClang, Windows'ta MSVC ile CI
üzerinde derlenip test ediliyor. Proje Apache-2.0 lisanslı; CodeSkeptic'e veya
başka bir harici analizöre bağımlı değil.

C++20 ve CMake kuruluysa bugün klonlayıp kaynak koddan birkaç komutla
deneyebilirsiniz:

https://github.com/tanzercakir-commits/Runtime_CodeSkeptic

Geri bildirime, gerçek dünya uyumluluk vakalarına ve katkılara açığım.

#OpenSource #Cpp #CMake #SystemsProgramming #SoftwareEngineering #AppleSilicon #Windows
