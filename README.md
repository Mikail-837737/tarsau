# tarsau

Sistem Programlama 2025-2026 Bahar projesi için C dilinde yazılmış, sıkıştırma
yapmayan basit `.sau` arşivleme programı.

## Derleme

```bash
make
```

## Arşiv oluşturma

```bash
./tarsau -b t1 t2 t3 t4.txt t5.dat -o s1.sau
```

`-o` parametresi verilmezse çıktı dosyası varsayılan olarak `a.sau` olur.

## Arşiv açma

```bash
./tarsau -a s1.sau d1
```

Dizin parametresi verilmezse dosyalar geçerli dizine açılır.

## Format

Arşiv dosyasının ilk 10 baytı organizasyon bölümünün uzunluğunu ASCII sayı
olarak tutar. Devamında her dosya için şu kayıt yazılır:

```text
|dosya_adi,izinler,boyut|
```

Organizasyon bölümünden sonra dosya içerikleri ayırıcı kullanılmadan art arda
yazılır. Açma sırasında dosya boyutları kullanılarak içerikler ayrılır ve
izinler geri yüklenir.

## Hızlı test

```bash
printf "Merhaba\n" > t1
printf "Sistem Programlama\n" > t2
chmod 754 t2
make
./tarsau -b t1 t2 -o s1.sau
./tarsau -a s1.sau d1
diff t1 d1/t1
diff t2 d1/t2
stat -c "%a" t2 d1/t2
```
