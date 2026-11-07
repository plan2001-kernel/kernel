# T3 — MicroPythonin heap break OBJ-rajapintaan

> **Historical report (T0–T6 phase).** Current direct-syscall state: [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md).

Tämä on **heap break -siirron T3** tulos. Tehtävä siirtää libc:n `sbrk`/`brk`
-polut vanhasta `BRK_(24)`-systeemikutsusta viiden OBJ-systeemikutsun
rajapintaan. Uutta syscall-numeroa ei otettu käyttöön. `BRK_(24)` pysyy
aktiivisena kernelin dispatchissä.

Kanoninen työpuu on `kuha:/home/lapere/Src/Plan2001-O`. Tehtävässä ilmoitettu
pohjasnapshot on
`651c0883b5cc59c8fa0b97c4af7465b919075a5dcf5d1b9e2418019bc37d255a`.
Koneellinen data on tiedostossa
[SYSCALL-T3-HEAP.json](SYSCALL-T3-HEAP.json). T0-, T1- ja T2-raportteja ei
muutettu.

## 1. Tavoite ja rajaus

MicroPythonin libc-heapin kasvatus ei enää tee `BRK_(24)`-trappia. Polku on:

- avaa prosessin oma heap-handle: `OBJOPEN(ObjOpenProcessSelf=14)`;
- aseta break: `OBJINVOKE(ObjOpHeapBreak=26, arg0=osoite)`;
- vapauta handle: `OBJCLOSE`.

Kernelissä operaation toteuttaa edelleen olemassa oleva
`ibrk(addr, BSEG)` (`kernel/core/segment.c`). T3 ei sisällä uutta
segmenttihallintaa, uutta syscall-numeroa, GE/O-formaattimuutosta,
boot-lataimen muutosta, verkkomuutosta eikä muiden OBJ-providerien muutosta.
Myöskään `malloc.c`:hen tai MicroPythonin GC:hen ei koskettu.

## 2. Toteutus

Muutetut lähteet:

- `runtime/abi/obj.h` — `ObjOpenProcessSelf=14` ja `ObjOpHeapBreak=26`
  (ei syscall-numeroita).
- `runtime/abi/vobj.h` — `VkindProcessSelf=12`.
- `kernel/core/sysobj.c` — `OBJOPEN`-dispatch uudelle providerille ja
  `OBJINVOKE`-dispatch `ObjOpHeapBreak`-operaatiolle. `in`/`out` ja niiden
  pituudet on pakko jättää nolliksi.
- `kernel/core/sysvobj.c` — rajattu provider `vobjprocessselfopen()` ja
  `vobjheapbreak()`. Jälkimmäinen vaatii `Vcontrol`-oikeuden ja kutsuu
  `ibrk((uintptr)arg0, BSEG)`.
- `kernel/core/vobjint.h` — providerin julistukset.
- `runtime/libc/9sys/sbrk.c` — `sbrk` ja `brk` käyttävät
  `obj_open`/`obj_invoke`/`obj_close`-kääreitä. Jokainen break-pyyntö avaa ja
  sulkee oman kahvan; tilaan ei jää stale-kahvaa.
- `micropython/modplan9.c` — `obj_open_process_self()`-adapteri ja vakiot
  `OBJ_OPEN_PROCESS_SELF` / `OBJ_OP_HEAP_BREAK` testikäyttöön. Tämä ei lisää
  kernelin entry pointia.

### Toimintasopimus

- `brk` säilyttää 8 tavun pyöristyksen, palauttaa onnistuessa `0` ja
  virheessä `-1`.
- `sbrk` säilyttää 8 tavun pyöristyksen, palauttaa aiemman breakin ja
  virheessä `(void*)-1`.
- Virheessä `bloc` ei muutu. Kernelin `ibrk` validoi edelleen ylivuodon,
  osoiteavaruuden rajat, päällekkäisyydet ja jaetun segmentin
  (`Enovmem`/`Esoverlap`/`Einuse`).
- Kahva edustaa vain kutsujan omaa `BSEG`-segmenttiä. Se saa vain
  `Vcontrol`-oikeuden eikä `Vdelegate`-oikeutta. Sillä ei voi nimetä toista
  prosessia eikä muuta muistia. `OBJCLOSE` vapauttaa kahvan.
- OBJ-kutsujen avaaminen, kutsu ja sulkeminen eivät käytä käyttäjätilan
  `malloc`ia eivätkä synnytä rekursiota libc-heapin alkupäässä.
- `BRK_(24)` säilyy kernelissä aktiivisena muita mahdollisia käyttäjiä
  varten. Sitä ei poistettu dispatchistä.

## 3. Mittaus ennen ja jälkeen

Mittaus ajettiin T0:n omalla instrumentoinnilla ja työkalulla:

```sh
python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t3/t0-tool-measurement.json --summary
```

Kaikki `validation`-kohdat ovat PASS: instrumentoitu boot, instrumentoitu
kahden bootin persistenssi, normaali savutesti, normaali persistenssi ja
normaalin artifactin uudelleenrakennus. Kaikki neljä QEMU-ajoa käyttivät
KVM:ää ja kertakäyttöistä O-kuvaa.

Vertailu tehdään välittömään T2-jälkeiseen mittaukseen
(`build/syscall-t2/t0-tool-measurement.json`, 2026-11-05T20:41:39Z), jotta
T1/T2:n jo tekemät OBJ-siirrot eivät vääristä eroa. T0:n alkuperäinen
`BRK_`-määrä oli myös 19.

| ajo | `BRK_` ennen | `BRK_` jälkeen | `OBJOPEN` Δ | `OBJINVOKE` Δ | `OBJCLOSE` Δ |
|---|---:|---:|---:|---:|---:|
| boot | 6 | **0** | +6 | +6 | +6 |
| persist-boot1 | 6 | **0** | +6 | +6 | +6 |
| persist-boot2 | 7 | **0** | +7 | +7 | +7 |
| **yhteensä** | **19** | **0** | **+19** | **+19** | **+19** |

Absoluuttiset kokonaismäärät samalla mittarilla:

| kutsu | ennen T3 | jälkeen T3 |
|---|---:|---:|
| `BRK_(24)` | 19 | **0** |
| `OBJOPEN(137)` | 38 | 57 |
| `OBJINVOKE(140)` | 622 | 641 |
| `OBJCLOSE(138)` | 28 | 47 |

Jokainen poistunut `BRK_` vastaa täsmälleen yhtä uutta
`OBJOPEN`/`OBJINVOKE`/`OBJCLOSE`-kolmikkoa. Vaiheittain `BRK_` oli ennen
19 kertaa vaiheessa `micropython-init-and-commit` ja 0 kertaa REPLissä;
jälkeen `BRK_` on 0 kaikissa vaiheissa. REPL-vaiheen `OBJOPEN` (14),
`OBJINVOKE` (558) ja `OBJCLOSE` (13) eivät kasvaneet, eli muutos ei lisännyt
ylimääräistä OBJ-liikennettä REPLissä.

Boot-lataimen `obootoid.elf` `BRK_`-määrä oli 0 sekä ennen että jälkeen.
Erillisiä tarkoituksellisia raw-testikutuja ei ajettu tässä mittauksessa.

## 4. Testit

| testi | tulos | evidenssi |
|---|---|---|
| kanoninen kernel-build | PASS | `build/syscall-t3/logs/build-kernel.log` |
| kanoninen MicroPython-build | PASS | 246 unit, 0 FAIL, ei undefined-symboleita; `build/syscall-t3/logs/build-mpy.log` |
| EFI- ja host-build | PASS | `build/syscall-t3/logs/build-efi.log`, `build/syscall-t3/logs/build-host.log` |
| O-boot -> REPL -> `6*7`=42 + useita allokaatioita + GC | PASS | `build/syscall-t3/boot-a/report.txt`; 1 KiB, 16 KiB ja 64 KiB `bytearray`, `T3-GC OK` |
| prosessin oman heap-handlen avaus ja break-kysely | PASS | `build/syscall-t3/boot-heap/report.txt`; `T3-PSELF-HANDLE 4613`, `T3-BASE 2543616` |
| liian suuri/virheellinen break-pyyntö | PASS | `T3-BREAK-FAIL OK virtual memory allocation failed`; `T3-BASE-AFTER 2543616` = base ennallaan |
| riittämättömät oikeudet providerin avauksessa | PASS | `T3-RIGHTS OK permission denied` (`Vread`-yritys) |
| virheen jälkeinen allokaatio | PASS | `T3-POST-ALLOC 4096` |
| GC-heapin kasvu libc-heapin kautta (16 MiB) | PASS | `build/syscall-t3/boot-bigalloc/report.txt`; `T3-GROW OK` |
| hallittu liian suuri allokaatio (300 MiB) | PASS | `T3-BIG-FAIL MemoryError`; `T3-BIGTEST OK` |
| kahden bootin GE/O-persistenssi | PASS | `build/syscall-t3/persist/report.txt`; `result=PASS` |
| REPL-muuttujan ja funktion palautus boottien yli | PASS | boot1 commit v9 -> boot2 restore v9 -> commit v21; `g3`, `t3fun(2)=202`, `t3oid` sisältö `T3-HEAP` |
| T0-instrumentointimittaus uudella koodilla | PASS | `build/syscall-t3/t0-tool-measurement.json`; kaikki `validation` PASS, `BRK_` 0 |
| staattinen tarkistus: ei `brk_`-kutsupaikkaa | PASS | `objdump -d build/gcc/micropython.elf`; `sbrk -> heapbreak -> obj_open/obj_invoke/obj_close` |
| `BRK_(24)` pysyy aktiivisena kernelissä | PASS | mittarin taulukossa `BRK_` on `ACTIVE`, mutta sillä ei ole kutsujaa mitatussa käyttäjätilassa |
| erillinen natiivi C-yksikkötesti `brk()`-funktiolle | NOT RUN | `brk` ei ole linkitettynä `micropython.elf`:ään, koska sillä ei ole kutsujaa; `sbrk`:n onnistuminen ja virhe testattiin libc-mallocin kautta |
| fyysinen rauta, TCG, laaja regressio | NOT RUN | tehtävän rajauksen ulkopuolella |

Huomio QEMU-injektorista: pitkät moniriviset `try`-lohkot kärsivät ajoittain
PS/2-merkkihäiriöstä. Siksi virhepolut ajettiin `exec`-funktioilla
(`chr(10)`-yhdistetyt rivit), kuten T2:ssa. Kaikki yllä olevat PASS-rivit
ovat peräisin ajetuista sarjalokeista, eivät pelkästä skriptistä.

## 5. Jäljelle jäävät legacy-kutsujat

- `runtime/libc/9sys/sbrk.c` ei enää viittaa `brk_`-symboliin.
- `build/gcc/micropython.elf`:ssä ei ole yhtään `call`- tai tail-`jmp`-
  kohdetta `brk_`-stubiin. `sbrk` kutsuu yksityistä `heapbreak`-apuria, joka
  avaa, kutsuu ja sulkee OBJ-kahvan.
- `BRK_(24)` on edelleen aktiivinen kernelin dispatchissä ja generoidussa
  `systab-gcc.h`:ssä. Sitä ei poistettu, koska muita mahdollisia käyttäjiä
  voi olla kernelin ulkopuolella.
- Muita `brk_`-kutsujia ei löytynyt kanonisesta amd64-puusta. Boot-lataimen
  omat trapit eivät sisällä `BRK_`-kutsua.

## 6. Artefaktit

Normaalit, testatut artifactit:

- `build/gcc/9pc64.elf` — 1 154 272 B, SHA-256
  `83fd2568da50b7040716c92011732284fd64a33521d102113bf19e2fcf9998f4`
- `build/gcc/micropython.elf` — 538 296 B, SHA-256
  `6a9ece2c67b03272c6578c63e690397d82e24f9d9ab0eed8addf1eb8d5480c96`
- `build/gcc/gen/obootoid.elf` — 5 456 B, SHA-256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`
- `build/gcc/bootx64.efi` — 22 016 B, SHA-256
  `5560f2e7147d8009688cacb16002f6a633d31059801b152ce06ac5cb599ff9b8`

Mitatut (instrumentoidut) binaryt:

- kernel `f8a95065f79e69836943d1e445df0416a53d7395d808955656f698a5784b7008`
  (1 154 648 B)
- MicroPython `83471366a2a8154ca513f6ac6e84c32b3edb726fe0ed4e17eeff55d1639458c8`
  (538 296 B)
- obootoid `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`

Normaalin kernelin merkkijonoista ei löydy `P2K-SYSCALL`-tekstiä.

## 7. Toistettavat komennot

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
tools/build-efi-gcc.sh -j 8
tools/build.sh

tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t3/boot-a --timeout 300 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t3/inputs/repl-t3-boot1.py \
  --expect "T3-BOOT1 OK"

tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t3/boot-heap --timeout 300 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t3/inputs/repl-t3-heap.py \
  --expect "T3-HEAPTEST OK"

tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t3/boot-bigalloc --timeout 300 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t3/inputs/repl-t3-bigalloc.py \
  --expect "T3-BIGTEST OK"

QOB_INI_BOOTEXEC=0x4f45584543000003 tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t3/persist --timeout 300 --repl \
  --boot1-input build/syscall-t3/inputs/repl-t3-persist-boot1.py \
  --boot2-input build/syscall-t3/inputs/repl-t3-persist-boot2.py \
  --boot1-expect "T3P-BOOT1 OK" --boot2-expect "T3P-BOOT2 OK"

python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t3/t0-tool-measurement.json --summary
```

## 8. Rajoitukset

- Mittaus ja testit koskevat vain tätä amd64/PoC/QEMU-KVM-konfiguraatiota.
- `BRK_(24)` on tarkoituksella yhä olemassa. T3 ei väitä, että kaikki
  mahdolliset kernelin ulkopuoliset legacy-binäärit olisi siirretty.
- Erillistä natiivia C-yksikkötestiä `brk()`-funktiolle ei ajettu, koska
  `brk` ei ole linkitettynä `micropython.elf`:ään. C-tason `sbrk`-polku
  testattiin libc-mallocin kautta sekä onnistuvalla 16 MiB:n että hallitusti
  epäonnistuvalla 300 MiB:n allokaatiolla.
- Tämä raportti ei kata T4:ää, T2b:tä, fyysistä rautaa eikä laajaa
  regressiokampanjaa.
