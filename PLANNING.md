# Planning Implementasi Game — STM32 F401CCU6

**Mata Kuliah:** Sistem Berbasis Mikrokontroler  
**Hardware:** STM32 F401CCU6, 8 LED (PA1–PA7, PB0), 2 Push Button (PB12, PB13), 1 Potentiometer (PA0)  
**Total Fase:** 3 fase — satu fase per game

---

## Daftar Isi

1. [Fase 1 — Rhythm Tap](#fase-1--rhythm-tap)
2. [Fase 2 — Charge & Release](#fase-2--charge--release)
3. [Fase 3 — Whack-a-LED](#fase-3--whack-a-led)
4. [Catatan Lintas Fase](#catatan-lintas-fase)

---

## Fase 1 — Rhythm Tap

### Deskripsi Game

STM32 berperan sebagai "guru ritme" — menampilkan pola ketukan lewat LED, lalu menunggu pemain menirunya dengan menekan B1 dalam timing yang sama. Semakin presisi timingnya, semakin tinggi skor. Game ini murni tentang sinkronisasi manusia dengan mesin. CubeMonitor mengubah data deviasi timing menjadi grafik yang bisa dianalisis secara kuantitatif.

### Pemetaan Hardware

| Komponen      | Pin          | Fungsi dalam Game                                                  |
| ------------- | ------------ | ------------------------------------------------------------------ |
| B1            | PB12         | Satu-satunya input pemain — ketuk mengikuti ritme LED              |
| B2            | PB13         | Tekan singkat = ulangi demo pola. Tahan 2 detik = skip level       |
| Potentiometer | PA0          | Kontrol BPM (60–180 BPM). Putar kiri = lambat, putar kanan = cepat |
| 8 LED         | PA1–PA7, PB0 | (1) Tampilkan pola demo, (2) Feedback per ketukan, (3) Skor biner  |

### Timeline Pengerjaan

```
Step 1            Step 2           Step 3              Step 4            Step 5
Struktur data  →  Demo pola    →   Input & scoring  →  Feedback LED  →  CubeMonitor
```

---

### Step 1 — Definisi Struktur Data dan State Game

**Tujuan:** Menyiapkan fondasi data dan state machine sebelum logika apapun ditulis.

- [ ] **Struct `RhythmGame`** — menyimpan array `uint16_t` berisi durasi tiap ketukan dalam ms, panjang pola saat ini, level aktif, skor kumulatif, dan jumlah nyawa.
- [ ] **Enum `GameState`** — definisikan state: `IDLE`, `DEMO`, `READY`, `INPUT`, `FEEDBACK`, `LEVEL_UP`, `GAME_OVER`. State machine ini yang mengendalikan seluruh alur game.
- [ ] **Array pola per level** — siapkan minimal 5 pola hardcoded (level 1–5). Mulai dari 3 ketukan BPM 60, naik ke 6 ketukan BPM 120. Setiap pola disimpan sebagai array interval antar ketukan dalam milidetik.
- [ ] **Variabel timing** — deklarasikan `uint32_t tapTimestamps[10]` untuk menyimpan waktu setiap ketukan pemain, dan `int32_t tapErrors[10]` untuk deviasi dari target (negatif = terlalu cepat, positif = terlalu lambat).

---

### Step 2 — Implementasi Fase Demo Pola

**Tujuan:** STM32 menampilkan pola ritme ke pemain sebelum giliran input dimulai.

- [ ] **Fungsi `playPattern()`** — loop sebanyak panjang pola: nyalakan semua 8 LED selama durasi ketukan, padamkan, tunggu interval, lanjut ke ketukan berikutnya. Gunakan `HAL_Delay()` untuk timing demo (presisi tidak kritis di fase ini).
- [ ] **Animasi ready signal** — setelah demo selesai, semua LED berkedip 3x dengan interval 300ms sebagai aba-aba "sekarang giliranmu". Tambahkan jeda 500ms sebelum mulai merekam input.
- [ ] **Baca potentiometer untuk BPM** — ambil nilai ADC, map ke range 60–180 BPM, hitung ulang semua interval pola sebelum diputar. Formula: `interval_ms = 60000 / bpm`.
- [ ] **Tombol B2 ulangi demo** — deteksi tekan B2 saat state `INPUT`, kembali ke state `DEMO` tanpa reset skor. Tambahkan counter `hintUsed` yang mengurangi skor akhir sebesar 50%.

---

### Step 3 — Implementasi Input Pemain dan Pengukuran Timing

**Tujuan:** Merekam timestamp setiap ketukan pemain dengan presisi milidetik dan menghitung deviasi dari target.

> **Kunci implementasi:** Gunakan EXTI interrupt (bukan polling) untuk merekam timestamp. Polling di loop utama bisa melewatkan ketukan cepat jika ada delay lain sedang berjalan.

- [ ] **Rekam timestamp via EXTI** — konfigurasikan B1 (PB12) sebagai EXTI falling edge interrupt. Di dalam `HAL_GPIO_EXTI_Callback`, simpan `HAL_GetTick()` ke array `tapTimestamps` hanya saat state = `INPUT`.
- [ ] **Hitung deviasi per ketukan** — setelah semua ketukan masuk, bandingkan interval antar timestamp pemain dengan interval target. `error[i] = tapTimestamps[i] - expectedTimestamps[i]`. Simpan nilai absolut untuk keperluan scoring.
- [ ] **Window timeout** — jika pemain tidak menekan dalam 2× interval target setelah ketukan terakhir yang direkam, anggap fase input selesai dan masuk ke feedback. Mencegah game stuck menunggu selamanya.
- [ ] **Deteksi ketukan ekstra** — jika pemain menekan lebih banyak dari panjang pola, catat sebagai `extraTap` yang mengurangi skor. Mencegah pemain asal memukul terus.

---

### Step 4 — Sistem Feedback dan Scoring

**Tujuan:** Memberikan umpan balik visual per ketukan dan menghitung skor secara akumulatif.

- [ ] **Klasifikasi per ketukan** — PERFECT jika |error| ≤ 50ms (LED teal menyala), NEAR jika 50–150ms (LED amber berkedip), MISS jika >150ms (semua LED flash merah sekali). Tampilkan feedback segera setelah fase INPUT selesai, ketukan demi ketukan secara berurutan.
- [ ] **Formula skor per ketukan:**
  - PERFECT = 100 poin
  - NEAR = 60 − (|error| − 50) / 3 poin
  - MISS = 0 poin
  - Bonus: semua PERFECT dalam satu pola = +200 poin
  - Penggunaan hint (B2) memotong bonus sebesar 50%
- [ ] **Tampilkan skor di LED** — setelah semua feedback ditampilkan, nyalakan LED sesuai nilai biner skor (clamp di 255 untuk 8 LED). Tahan 1 detik agar pemain sempat membaca.
- [ ] **Level progression** — jika rata-rata akurasi ≥ 70% dan tidak ada MISS, lanjut ke level berikutnya (panjang pola +1 ketukan, BPM +10). Jika ada 3 MISS berturut, nyawa berkurang dan pola diulang dari awal.

---

### Step 5 — Output CubeMonitor

**Tujuan:** Mengirim data timing ke CubeMonitor untuk divisualisasikan sebagai grafik akurasi real-time.

- [ ] **Format string UART per pola** — kirim setiap akhir pola:
  ```
  RHYTHM,LEVEL:3,SCORE:840,ERR0:20,ERR1:-45,ERR2:88,AVG:51\r\n
  ```
  Error negatif = terlalu cepat, positif = terlalu lambat.
- [ ] **Line chart deviasi** — setiap nilai `ERRx` jadi satu titik di chart. Garis tengah (y=0) = timing sempurna. Pemain bisa melihat pola: apakah selalu lambat, selalu cepat, atau acak.
- [ ] **Gauge akurasi rata-rata** — nilai `AVG` dikonversi ke persentase akurasi (0ms = 100%, ≥150ms = 0%) dan dikirim sebagai nilai 0–100 untuk gauge di CubeMonitor.
- [ ] **Counter level dan skor kumulatif** — dikirim setiap awal level baru agar CubeMonitor dapat menampilkan grafik progression skor vs level.

---

### Deliverables Fase 1

| File                  | Deskripsi                                                                                     |
| --------------------- | --------------------------------------------------------------------------------------------- |
| `rhythm_game.c / .h`  | Modul game yang bisa di-include ke `main.c` tanpa konflik dengan mode 1/2/3 dosen             |
| `rhythm_monitor.json` | Node-RED flow: parse data `RHYTHM,` dari UART, tampilkan line chart deviasi dan gauge akurasi |
| Dokumentasi           | Tabel pola per level (BPM, jumlah ketukan, interval target) dan penjelasan formula scoring    |

**Hasil yang bisa didemonstrasikan:** STM32 menampilkan pola 4 ketukan, pemain menirunya dengan B1, CubeMonitor langsung menampilkan grafik deviasi timing setiap ketukan dalam milidetik — terlihat apakah pemain makin presisi atau makin panik seiring level naik.

---

## Fase 2 — Charge & Release

### Deskripsi Game

Tahan B1 untuk mengisi LED satu per satu seperti progress bar. Lepas di momen yang tepat sesuai target yang ditampilkan sebelumnya. Sederhana untuk dipahami, sangat sulit untuk dikuasai. Konsep identik dengan mekanisme "power shot" di game olahraga — tahan untuk charge, lepas di timing sempurna. Highscore disimpan permanen ke FLASH sehingga kompetisi antar pemain bisa berlanjut meski board dicabut.

### Pemetaan Hardware

| Komponen      | Pin          | Fungsi dalam Game                                                                         |
| ------------- | ------------ | ----------------------------------------------------------------------------------------- |
| B1            | PB12         | Tahan = mulai charge (LED menyala progresif). Lepas = shot dieksekusi                     |
| B2            | PB13         | Tekan saat `WAITING_CHARGE` = tampilkan ulang target. Tahan 2 detik = skip ronde (skor 0) |
| Potentiometer | PA0          | Kecepatan charge. Putar kiri = lambat (mudah). Putar kanan = sangat cepat (sulit)         |
| 8 LED         | PA1–PA7, PB0 | (1) Target berkedip sebelum charge, (2) Progres charge saat ditahan, (3) Feedback hasil   |

### Timeline Pengerjaan

```
Step 1           Step 2           Step 3             Step 4               Step 5
Target system →  Charge engine →  Release detect  →  Scoring & FLASH  →  CubeMonitor
```

---

### Step 1 — Sistem Target dan Tampilan Awal Ronde

**Tujuan:** Menentukan target secara acak dan menampilkannya ke pemain sebelum charge dimulai.

- [ ] **Generate target acak** — target adalah angka 1–8 (jumlah LED yang harus dicapai). Gunakan: `target = (HAL_GetTick() ^ ADC_read()) % 8 + 1`. Noise ADC memastikan urutan tidak pernah sama antar sesi.
- [ ] **Animasi tampil target** — LED ke-N (target) berkedip 5x cepat (100ms on/off), lalu menyala solid 500ms, lalu semua padam. Ini memberi tahu pemain harus berhenti di LED berapa.
- [ ] **Countdown visual** — setelah tampil target, tiga LED paling kiri menyala satu per satu dengan jeda 400ms (seperti hitungan 3… 2… 1…), lalu semua padam sebagai tanda boleh mulai charge.
- [ ] **State `WAITING_CHARGE`** — setelah countdown, game menunggu B1 ditekan. Jika dalam 5 detik tidak ada aksi, tampilkan ulang target tanpa penalti (anti-idle agar game tidak membingungkan).

---

### Step 2 — Engine Charge: LED Progresif Saat B1 Ditahan

**Tujuan:** LED menyala satu per satu dari kiri ke kanan selama B1 ditahan, dengan kecepatan dikontrol potentiometer.

> **Penting:** Jangan gunakan `HAL_Delay()` di dalam loop charge. Ini akan menghalangi deteksi kapan B1 dilepas. Seluruh loop harus non-blocking penuh.

- [ ] **Deteksi tekan B1** — gunakan polling di main loop (bukan interrupt, karena butuh durasi terus-menerus). Catat `chargeStartTick = HAL_GetTick()` saat B1 pertama kali LOW.
- [ ] **Hitung LED yang harus menyala** — setiap iterasi loop:
  ```c
  elapsed = HAL_GetTick() - chargeStartTick;
  currentLED = elapsed / chargeSpeed_ms;
  ```
  Nilai `chargeSpeed_ms` diambil dari ADC potentiometer: putar kanan = nilai makin kecil = charge makin cepat. Range yang disarankan: 80–400ms per LED.
- [ ] **Update LED langsung** — panggil `LED_WriteRaw((1 << currentLED) - 1)` setiap iterasi agar LED terasa menyala mulus tanpa step yang kasar atau tersendat.
- [ ] **Deteksi over-charge** — jika `currentLED >= 8` (semua LED menyala), masuk state `BUSTED`: semua LED berkedip cepat 5x, skor ronde = 0, lanjut ke ronde berikutnya secara otomatis.

---

### Step 3 — Deteksi Release dan Evaluasi Hasil

**Tujuan:** Mendeteksi kapan B1 dilepas dan mengevaluasi berapa LED yang berhasil dicapai dibanding target.

- [ ] **Deteksi B1 HIGH (dilepas)** — setiap iterasi loop charge, cek `HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_SET`. Saat kondisi ini `true`, catat `releasedLED = currentLED` dan keluar dari loop charge.
- [ ] **Freeze LED setelah release** — pertahankan tampilan LED saat dilepas selama 800ms agar pemain bisa melihat hasilnya sebelum feedback muncul.
- [ ] **Hitung delta** — `delta = releasedLED - target`. Delta = 0 adalah PERFECT. Simpan nilai ini untuk scoring dan pengiriman ke CubeMonitor.
- [ ] **Handle B2 saat charging** — jika B2 ditekan saat state `WAITING_CHARGE` (sebelum mulai charge), tampilkan ulang target. Jika ditekan saat sedang charge, abaikan sepenuhnya (anti-cheat: tidak bisa force-stop charge).

---

### Step 4 — Sistem Scoring dan Penyimpanan Highscore ke FLASH

**Tujuan:** Menghitung skor per ronde, memberikan feedback animasi, dan menyimpan highscore secara permanen ke FLASH internal STM32.

- [ ] **Formula skor per ronde:**
  - PERFECT (delta = 0) = 100 poin
  - NEAR (|delta| = 1) = 70 poin
  - CLOSE (|delta| = 2) = 40 poin
  - FAR (|delta| ≥ 3) = 10 poin
  - BUSTED = 0 poin
  - Bonus streak: 3 PERFECT berturut = +150 poin bonus
- [ ] **Feedback animasi per klasifikasi:**
  - PERFECT: semua LED menyala lalu padam waterfall (kanan ke kiri)
  - NEAR: LED target berkedip 2x amber
  - FAR: LED bergerak mundur (waterfall terbalik, kiri ke kanan)
  - BUSTED: flash merah cepat semua LED 5x
- [ ] **Simpan highscore ke FLASH** — gunakan HAL Flash API: unlock, erase satu page, write `uint32_t` skor ke alamat tetap (misalnya `0x0801F800`). Baca saat boot untuk menampilkan highscore sebelumnya di LED sebagai representasi biner.
- [ ] **Struktur satu game = 10 ronde** — setelah ronde ke-10, tampilkan skor total di LED biner selama 3 detik, kirim ke CubeMonitor, lalu bandingkan dengan highscore di FLASH. Jika lebih tinggi, update FLASH dan tampilkan animasi "new record" (semua LED menyala bergantian kiri-kanan 3x).

---

### Step 5 — Output CubeMonitor

**Tujuan:** Mengirim data hasil setiap ronde ke CubeMonitor untuk divisualisasikan sebagai scatter plot dan distribusi klasifikasi.

- [ ] **Format string UART per ronde** — kirim setiap akhir ronde:
  ```
  CHARGE,ROUND:4,TARGET:6,RESULT:5,DELTA:-1,SCORE:70,TOTAL:310\r\n
  ```
- [ ] **Scatter plot target vs hasil** — sumbu X = target (1–8), sumbu Y = hasil (1–8). Titik di garis y=x = sempurna. Terlihat apakah pemain secara sistematis selalu over-charge atau under-charge.
- [ ] **Bar chart klasifikasi 10 ronde** — berapa ronde yang PERFECT, NEAR, CLOSE, FAR, dan BUSTED dalam satu game. Profil kemampuan pemain terbaca dari distribusi ini.
- [ ] **Gauge skor total + highscore** — gauge menampilkan skor saat ini (0–1000). Text node di bawahnya menampilkan highscore dari FLASH yang tidak berubah sampai dipecahkan.

---

### Deliverables Fase 2

| File                   | Deskripsi                                                                                                     |
| ---------------------- | ------------------------------------------------------------------------------------------------------------- |
| `charge_game.c / .h`   | Modul independen dengan fungsi `init`, `run`, dan `reset`. Non-blocking loop dengan polling B1 presisi tinggi |
| `flash_storage.c / .h` | Fungsi `readHighscore()` dan `writeHighscore()` — dapat dipakai ulang oleh Fase 3                             |
| `charge_monitor.json`  | Node-RED flow: scatter plot target vs hasil dan bar chart klasifikasi per game                                |

**Hasil yang bisa didemonstrasikan:** Pemain menahan B1 sambil melihat LED menyala progresif, melepas tepat di LED target — CubeMonitor langsung menampilkan scatter plot akurasi dan menyimpan highscore yang bertahan meski board dicabut dari power.

---

## Fase 3 — Whack-a-LED

### Deskripsi Game

Adaptasi digital dari game arcade "Whack-a-Mole" klasik. Satu LED menyala acak di salah satu dari 8 posisi. Pemain harus menekan button yang tepat (B1 untuk zona kiri, B2 untuk zona kanan) sebelum LED padam sendiri. Makin lama bermain, makin cepat tempo dan makin sempit window waktu. Data reaction time yang dikumpulkan mengungkap profil psikomotor pemain secara kuantitatif.

### Pemetaan Hardware

| Komponen      | Pin          | Fungsi dalam Game                                                                |
| ------------- | ------------ | -------------------------------------------------------------------------------- |
| B1            | PB12         | Pukul LED di zona kiri (posisi 1–4). Juga bagian dari double hit                 |
| B2            | PB13         | Pukul LED di zona kanan (posisi 5–8). Juga bagian dari double hit                |
| Potentiometer | PA0          | Jumlah nyawa awal (1–5). Dikunci saat game berlangsung                           |
| 8 LED         | PA1–PA7, PB0 | Arena game. LED solid = target normal. LED berkedip amber = special double spawn |

### Timeline Pengerjaan

```
Step 1          Step 2           Step 3              Step 4               Step 5
Spawn engine →  Hit detection →  Window & timeout →  Difficulty scale →  CubeMonitor
```

---

### Step 1 — Spawn Engine: Kemunculan LED Acak

**Tujuan:** Memunculkan LED target secara acak, adil, dan tidak terprediksi setiap sesi.

- [ ] **PRNG dengan seed ADC** — implementasi Linear Congruential Generator sederhana. Seed diambil dari kombinasi ADC noise dan tick saat game start:
  ```c
  seed = HAL_GetTick() ^ (ADC_read() << 4);
  ```
  Ini memastikan urutan acak berbeda di setiap sesi meski hardware yang sama digunakan.
- [ ] **Anti-repeat** — simpan posisi LED sebelumnya. Jika PRNG menghasilkan posisi yang sama, generate ulang sekali. Mencegah LED muncul di tempat yang sama dua kali berturut (terlalu mudah ditebak).
- [ ] **Special event double spawn** — setiap 10 hit berhasil, set flag `doubleSpawn = true`. Generate dua posisi berbeda: satu di LED 1–4, satu di LED 5–8. Keduanya berkedip amber sebagai sinyal visual "ini event spesial — tekan dua button sekaligus".
- [ ] **Proximity warning temporal** — saat tersisa ≤ 30% window waktu, LED target mulai berkedip. Frekuensi kedip proporsional dengan sisa waktu: makin sedikit waktu, makin cepat kedipnya. Implementasikan dengan toggle LED di TIM interrupt berdasarkan threshold waktu.

---

### Step 2 — Hit Detection: Validasi Button yang Ditekan

**Tujuan:** Menentukan apakah pemain menekan button yang benar, merekam reaction time, dan menangani tiga kemungkinan hasil.

> **Kunci implementasi:** Gunakan EXTI interrupt untuk kedua button agar reaction time terekam tepat — perbedaan puluhan milidetik sangat terasa di game ini.

- [ ] **Mapping posisi ke button** — buat fungsi `getCorrectButton(uint8_t ledPos)`: posisi 0–3 return `BUTTON_1`, posisi 4–7 return `BUTTON_2`. Fungsi ini menjadi satu-satunya tempat aturan mapping sehingga mudah diubah jika diperlukan.
- [ ] **Rekam reaction time** — saat LED spawn, catat `spawnTick = HAL_GetTick()`. Saat button ditekan di EXTI callback, hitung `reactionTime = HAL_GetTick() - spawnTick`. Simpan ke array untuk dikirim ke CubeMonitor.
- [ ] **Tiga hasil yang mungkin:**
  - **HIT** — button benar dalam window: skor ditambah, reaction time dicatat, spawn berikutnya
  - **WRONG** — button salah: nyawa −1, LED flash merah, jeda 300ms, spawn berikutnya
  - **TIMEOUT** — window habis tanpa tekan: nyawa −1, LED padam cepat, jeda 300ms, spawn berikutnya
- [ ] **Double spawn hit logic** — keduanya harus ditekan dalam jendela 200ms satu sama lain. Jika hanya satu yang ditekan dan satunya timeout, dihitung sebagai PARTIAL: nyawa tidak berkurang tapi tidak mendapat bonus. Deteksi dengan menyimpan timestamp masing-masing button secara terpisah.

---

### Step 3 — Window Management: Waktu yang Makin Menyempit

**Tujuan:** Mengatur durasi window waktu dan mempersingkatnya secara otomatis seiring hit berhasil bertambah.

- [ ] **Variabel `windowMs`** — dimulai dari 900ms. Setiap 5 hit berhasil, berkurang 60ms. Minimum 200ms (batas fisiologis manusia ~150ms, jadi 200ms masih fair). Potentiometer mengatur nilai awal: putar kiri = mulai 1200ms, putar kanan = mulai 600ms.
- [ ] **Timer countdown via TIM3** — gunakan TIM3 dengan periode 1ms. Setiap spawn, reset counter ke `windowMs`. Saat counter mencapai 0, trigger event TIMEOUT. Lebih presisi daripada menggunakan polling `HAL_GetTick()` di loop utama.
- [ ] **Implementasi proximity warning** — di TIM3 callback, hitung `timeRemaining = windowMs - elapsedMs`. Jika `timeRemaining <= windowMs * 0.3`, toggle LED target setiap `timeRemaining / 5` ms. Semakin sedikit waktu, semakin sering toggle, semakin cepat kedip.
- [ ] **Jeda antar spawn** — setelah setiap hit/miss/timeout, jeda tetap 300ms sebelum spawn berikutnya. Ini memberi "nafas" kepada pemain dan mencegah spawn langsung saat pemain masih memproses hasil sebelumnya.

---

### Step 4 — Sistem Nyawa, Difficulty Scaling, dan Game Over

**Tujuan:** Mengelola nyawa, mengeskalasi kesulitan secara progresif, dan menangani kondisi game over dengan animasi yang berkesan.

- [ ] **Nyawa dari potentiometer** — baca ADC saat layar start, map ke 1–5 nyawa. Tampilkan jumlah nyawa dengan LED berkedip sejumlah nyawa yang dipilih (misalnya 3 nyawa = LED 1, 2, 3 berkedip bergantian). Nilai dikunci selama game berlangsung.
- [ ] **Tampilkan nyawa tersisa setelah miss** — setelah setiap WRONG atau TIMEOUT, N LED paling kiri menyala selama 500ms menunjukkan sisa nyawa, lalu padam dan spawn berikutnya dimulai. Feedback yang langsung dimengerti tanpa memerlukan angka.
- [ ] **Skor dan combo:**
  - Setiap HIT: `skor += 10 + (hitStreak × 5)`
  - Streak reset saat WRONG atau TIMEOUT
  - Double spawn berhasil: `skor += 50` + nyawa +1 (maksimal di nilai nyawa awal)
  - Ini menjadikan double spawn sebagai "kesempatan emas" yang ditunggu-tunggu
- [ ] **Animasi game over** — saat nyawa = 0: semua LED menyala, lalu padam satu per satu dari kanan ke kiri dengan delay 150ms per LED. Setelah semua padam, skor final ditampilkan sebagai biner selama 3 detik, lalu dikirim ke CubeMonitor. Bandingkan dengan highscore di FLASH (reuse `flash_storage.c` dari Fase 2).

---

### Step 5 — Output CubeMonitor

**Tujuan:** Mengirim reaction time setiap hit secara real-time dan statistik per game untuk divisualisasikan.

- [ ] **Format string UART per hit (real-time)** — kirim setiap HIT berhasil:
  ```
  WHACK,POS:3,BTN:1,RT:247,RESULT:HIT,SCORE:130\r\n
  ```
  `RT` = reaction time dalam milidetik, `POS` = posisi LED (0–7), `BTN` = button yang ditekan (1 atau 2).
- [ ] **Line chart reaction time** — setiap HIT berhasil jadi satu titik di chart. Grafik menunjukkan tren: warm-up (makin cepat di awal), plateau, lalu fatigue (makin lambat saat stress meningkat). Data psikomotor nyata yang dihasilkan hardware.
- [ ] **Bar chart akurasi per zona** — dua bar: hit rate zona kiri (B1) vs zona kanan (B2). Selisih yang konsisten mengungkap dominansi tangan pemain. Dikirim setiap game over dalam format: `WHACK_STAT,HITS_L:12,HITS_R:8,MISS_L:2,MISS_R:5\r\n`.
- [ ] **Gauge window aktif** — menampilkan nilai `windowMs` saat ini (200–900ms) sebagai gauge. Berubah setiap 5 hit berhasil, memvisualisasikan tingkat kesulitan yang sedang dihadapi pemain secara real-time.

---

### Deliverables Fase 3

| File                   | Deskripsi                                                                                           |
| ---------------------- | --------------------------------------------------------------------------------------------------- |
| `whack_game.c / .h`    | Modul dengan TIM3 untuk countdown window, EXTI untuk kedua button, dan PRNG untuk spawn acak        |
| `flash_storage.c / .h` | Reuse dari Fase 2 — highscore Whack-a-LED disimpan di alamat FLASH berbeda, tidak perlu tulis ulang |
| `whack_monitor.json`   | Node-RED flow: line chart reaction time real-time, bar chart akurasi per zona, gauge window aktif   |

**Hasil yang bisa didemonstrasikan:** LED muncul acak, pemain memukul button dengan cepat, CubeMonitor menampilkan reaction time setiap hit secara live — grafik memperlihatkan pola warm-up dan fatigue pemain secara nyata dan terukur.

---

## Catatan Lintas Fase

### Urutan Pengerjaan yang Disarankan

Meski dibagi per fase, urutan implementasi yang paling efisien adalah:

1. **Fase 2 lebih dahulu** — menghasilkan `flash_storage.c` yang langsung bisa dipakai ulang di Fase 3 tanpa menulis ulang
2. **Fase 3** — reuse `flash_storage.c`, tambahkan `whack_game.c`
3. **Fase 1 terakhir atau paralel** — paling mandiri, tidak bergantung pada output fase lain

### Aturan Non-Blocking — Wajib di Ketiga Game

> Jangan gunakan `HAL_Delay()` di dalam loop game manapun.

Semua timing harus menggunakan pola berikut:

```c
uint32_t lastTick = HAL_GetTick();
if (HAL_GetTick() - lastTick >= intervalMs) {
    lastTick = HAL_GetTick();
    // lakukan aksi
}
```

Begitu ada `HAL_Delay()` di dalam loop game, deteksi button menjadi tidak responsif dan game terasa lag. Satu-satunya pengecualian yang diizinkan adalah animasi singkat (feedback, countdown) yang sengaja memblokir input.

### Struktur Modul Kode

Setiap game dibuat sebagai modul `.c/.h` terpisah yang diaktifkan hanya saat mode game dipilih. Ini memastikan kode tugas utama dosen (Mode 1/2/3) tidak tersentuh sama sekali.

```c
// Di main.c — contoh integrasi
if (currentMode == MODE_GAME_RHYTHM)   RhythmGame_Run();
if (currentMode == MODE_GAME_CHARGE)   ChargeGame_Run();
if (currentMode == MODE_GAME_WHACK)    WhackGame_Run();
```

### Format UART untuk Node-RED

Ketiga game mengirim data dengan prefix berbeda sehingga satu Node-RED flow bisa mem-parse ketiganya sekaligus dan menampilkan panel yang sesuai secara otomatis:

| Prefix           | Game             | Kapan Dikirim                                 |
| ---------------- | ---------------- | --------------------------------------------- |
| `RHYTHM,...`     | Rhythm Tap       | Setiap akhir satu pola selesai dimainkan      |
| `CHARGE,...`     | Charge & Release | Setiap akhir satu ronde (release atau busted) |
| `WHACK,...`      | Whack-a-LED      | Setiap HIT berhasil (real-time)               |
| `WHACK_STAT,...` | Whack-a-LED      | Setiap game over (statistik keseluruhan)      |

### Shared Resources antar Fase

| Resource                 | Dibuat di | Dipakai oleh               |
| ------------------------ | --------- | -------------------------- |
| `flash_storage.c / .h`   | Fase 2    | Fase 2 dan Fase 3          |
| EXTI callback B1         | Fase 1    | Fase 1 (timestamp ketukan) |
| EXTI callback B1 + B2    | Fase 3    | Fase 3 (hit detection)     |
| ADC read untuk PRNG seed | Fase 2    | Fase 2 dan Fase 3          |
