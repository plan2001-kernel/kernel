# T0 — käyttäjätilan syscall-riippuvuuksien mittaus

Tämä on **mittaus- ja raportointitehtävän T0** tulos. Tehtävä ei muuta
syscall-ABI:a, poista tai siirrä syscalleja, lisää OBJ-operaatioita eikä muuta
prosessien, GE/O:n, USB:n tai verkon toteutusta. Mittaus koskee vain nykyistä
PoC-konetta `kuha` ja sen kanonista puuta `/home/lapere/Src/Plan2001-O`.

Lähtösnapshotin vertailupiste on
`1236e5b94c3e9b5e3323c4bfaac59e35ad69d20bc9c6eaa490deff87b032e172`
(5.11.2026). Paikallinen lähde on tämän työistunnon jälkeen kanoninen.

## 1. Ympäristö ja mittauksen kohteet

- Host: `kuha`, Linux, QEMU 10.0.13, KVM (`/dev/kvm`), yksi vCPU.
- Kernel: `build/gcc/9pc64.elf` (mitattu instrumentoitu build).
- Käyttäjätila: `build/gcc/micropython.elf` ja ensimmäisen käyttäjätilan
  lataaja `build/gcc/gen/obootoid.elf`.
- O-volume: tuore kertakäyttöinen 129 MiB GE/O-kuva, joka luodaan
  `tools/gcc-ovol-ream.sh`:lla ja viedään kertakäyttöiseen GPT-levyyn.
- REPL- ja objekti­testi ajettiin KVM:llä; TCG:tä ei käytetty.
- Testit käyttävät vain kahta sallittua QEMU-työkalua:
  `tools/qemu-o-boot.sh` ja `tools/qemu-o-persist.sh`.

Mittauksen kohteet:

1. ensimmäinen käyttäjätilan lataaja (`kernel/amd64/obootoid.c`), joka kutsuu
   `OVOLOPEN`/`OSPACEOPEN`/`OEXECUTE`/`EXITS`;
2. käynnistyvä MicroPython, sen linkitetty libc/startup ja REPL;
3. MicroPythonin käytettävissä olevat objektioperaatiot (konsoli, GE/O,
   KBD/USB/UART-adaptaattorit) siltä osin kuin testit ne todella ajoivat.

## 2. Menetelmä

### 2.1 Systab-snapshot

Aktiiviset ja varatut numerot luettiin lähteistä, ei käsin ylläpidetystä
listasta:

- numerot ja nimet: `runtime/abi/sys.h`;
- dispatch-tila: `tools/host/build-kernel.py::make_systab_gcc()` generoima
  `build/gcc/gen/systab-gcc.h` (`systab[]`, `sysreserved[]`, `nsyscall`);
- kutsusymbolit: `tools/host/build-mpy-gcc.py::gen_stubs()`.

Tulos: **135 määriteltyä nimeä**, joista **65 aktiivista** (ei-`nil`
dispatch) ja **70 varattua** (`nil` dispatch). Määrittelemättömät numeroaukot
välillä 0–140 ovat 48, 49, 94, 95, 114 ja 115. Ne eivät ole syscalleja eivätkä
varattuja numeroita.

Viiden OBJ-kutsun dispatch on suora:

| numero | nimi | kernel-handler |
|---:|---|---|
| 136 | `OBJCREATE` | `kernel/core/sysobj.c:35` |
| 137 | `OBJOPEN` | `kernel/core/sysobj.c:79` |
| 138 | `OBJCLOSE` | `kernel/core/sysobj.c:165` |
| 139 | `OBJBIND` | `kernel/core/sysobj.c:171` |
| 140 | `OBJINVOKE` | `kernel/core/sysobj.c:188` |

### 2.2 Ajonaikainen instrumentointi

Instrumentointi on **käännösaikainen** ja kytkeytyy vain, kun buildissa on
`PLAN2001_SYSCALL_TRACE`:

- `kernel/core/sysproc.c:2092` tallentaa jokaisen käyttäjätilan trapin
  rajoitettuun 32768 alkion puskuriin: järjestysnumero, kutsujan PID,
  ohjelmaluokka, syscall-numero, dispatch-tila, syscall-stubin PC ja
  paluuosoite (kutsujan PC).
- Puskuri tyhjennetään vain eksplisiittisissä checkpoint-kohdissa
  `SYSSNAPSHOT`-kutsulla (`kernel/core/sysinfo.c:130`,
  `kernel/core/sysproc.c:2137`). Tämä pitää normaalin sarjaportin
  kehotteet ja `tools/qemu-o-boot.sh`:n kehotetarkistuksen muuttumattomina.
- Checkpointit: `1` boot-loader (automaattinen ensimmäisellä MicroPython-
  trapilla), `2` MicroPython-startup `mp_init()`:n jälkeen
  (`micropython/main.c:180`), `3` init/commit/raportti ennen REPL:iä
  (`micropython/main.c:272`) ja `4` REPL-inputin `plan9.system()`
  (`build/syscall-t0/inputs/repl-t0.py`).
- `SYSSNAPSHOT` (107) on mittauksen ohjauskutsu, ei sovelluksen
  syscall-riippuvuus; se jätetään laskennan ulkopuolelle.
- Instrumentointi ei muuta argumentteja, paluuarvoja, virheitä,
  käyttöoikeuksia eikä dispatch-semantiikkaa. Normaalissa buildissa koodi,
  tila ja puskuri eivät ole mukana; normaalin kernelin merkkijonoista ei löydy
  `P2K-SYSCALL`-tekstiä.

Mittausbuild kytketään:

```sh
PLAN2001_SYSCALL_TRACE=1 tools/build-gcc.sh --kernel -j 8
PLAN2001_SYSCALL_TRACE=1 tools/build-mpy-gcc.sh -j 8
```

Kytkimet ovat `tools/host/build-kernel.py:60` ja
`tools/host/build-mpy-gcc.py:65`. Normaali build tehdään ilman ympäristö-
muuttujaa.

### 2.3 Staattinen analyysi

`tools/host/syscall-t0-measure.py` lukee mitatut ELF:t `objdump -d`:llä ja
`nm`:llä:

- suorat `call`- ja tail-`jmp`-kutsut syscall-stubiin;
- kutsujan funktio ja ketju (enintään kaksi askelta) MicroPythonin
  moduuliadapteista libc-kääreeseen ja stubiin;
- kahdeksan tavun funktio-osoittimet allokoiduista PROGBITS-osioista
  tukevana näyttönä (ei yksinään todiste suorituksesta).

Staattinen analyysi ei väitä, että pelkkä symbolin olemassaolo tai
osoitteenotto tarkoittaa suoritusta. Ajonaikainen evidenssi on aina erikseen.

### 2.4 Testiajot

Toistettava kokonaisajo:

```sh
python3 tools/host/syscall-t0-measure.py --run --summary
```

Skripti tekee tämän järjestyksessä:

```sh
PLAN2001_SYSCALL_TRACE=1 tools/build-gcc.sh --kernel -j 8
PLAN2001_SYSCALL_TRACE=1 tools/build-mpy-gcc.sh -j 8
tools/build-efi-gcc.sh -j 8
tools/build.sh

tools/qemu-o-boot.sh --no-build --output-dir build/syscall-t0/boot \
  --timeout 180 --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t0/inputs/repl-t0.py --expect T0-OBJ-OK

QOB_INI_BOOTEXEC=0x4f45584543000003 \
tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t0/persist --timeout 240

# Normaalit artifactit palautetaan ja savutestataan ilman tracea:
tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
tools/qemu-o-boot.sh --no-build --output-dir build/syscall-t0/normal-smoke \
  --timeout 180 --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t0/inputs/repl-t0.py --expect T0-OBJ-OK
QOB_INI_BOOTEXEC=0x4f45584543000003 \
tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t0/normal-persist --timeout 240
```

REPL-input oli:

```python
print(6*7)
import plan9
h = plan9.obj_open_space("init")
print("T0-OBJ-HANDLE", h)
plan9.obj_close(h)
print("T0-OBJ-OK")
plan9.system()
```

Tämä kattaa (i) tavallisen O-bootin interaktiiviseen REPL:iin ja laskun
`6*7`, (ii) pysyvän Space-tilan avauksen/luvun/commitin ja palautuksen kahden
buutin yli (odemo-Counter) sekä (iii) eksplisiittisen GE/O-objektin
`obj_open_space("init")` / `obj_close`-operaation. Konsolin kirjoitukset ja
KBD:n alustus käyttävät samalla `OBJOPEN`/`OBJINVOKE`/`OBJCLOSE`-rajapintaa.

## 3. Ajonaikaiset tulokset

Kaikissa ajoissa kaikki trapit tulivat PID:ltä 3. Lapsiprosesseja ei havaittu.
Varattuja tai tuntemattomia trap-numeroita ei havaittu. Pudotettuja trace-
alkioita ei ollut (`dropped=0`).

### 3.1 Boot: O-boot + REPL + objektitesti

| vaihe | trapit | kutsut |
|---|---:|---|
| boot-loader | 3 | `OVOLOPEN` 1, `OSPACEOPEN` 1, `OEXECUTE` 1 |
| MicroPython startup | 0 | — |
| MicroPython init + commit | 36 | `BRK_` 6, `NOTIFY` 1, `_NSEC` 4, `OVOLOPEN` 1, `OSPACEOPEN` 1, `OSPACEGET` 9, `OSPACEPUT` 3, `OSPACECOMMIT` 1, `OBJOPEN` 2, `OBJINVOKE` 8 |
| MicroPython REPL | 573 | `_NSEC` 12, `OSPACEGET` 24, `OSPACEPUT` 18, `OSPACECOMMIT` 6, `OBJOPEN` 2, `OBJCLOSE` 1, `OBJINVOKE` 510 |

Boot-loaderin kaikki kolme kutsua tulivat `obootoid.elf`-funktiosta `_main`
(paluuosoitteet `0x200050`, `0x20007c`, `0x20012b`). Onnistuneella polulla
`EXITS` ja `OSPACEGET`-fallback eivät suorittuneet, vaikka ne ovat linkitettynä.

Init/commit-vaiheen tärkeimmät kutsujat:

- `BRK_` ← `sbrk` (6)
- `NOTIFY` ← `atnotify` (1)
- `_NSEC` ← `ospace_open.isra.0` (2), `p9_image_commit` (1),
  `p9_space_commit` (1)
- `OVOLOPEN` ← `p9_space_o` (1)
- `OSPACEOPEN` ← `ospace_open.isra.0` (1)
- `OSPACEGET` ← `oreadobj.constprop.0` (9)
- `OSPACEPUT` ← `ocommit` (3)
- `OSPACECOMMIT` ← `ocommit` (1)
- `OBJOPEN` ← `p9_dprintf` (1), `p9_native_output` (1)
- `OBJINVOKE` ← `obj_write` (8)

REPL-vaiheen tärkeimmät kutsujat:

- `_NSEC` ← `p9_image_commit` (6), `p9_space_commit` (6)
- `OSPACEGET` ← `oreadobj.constprop.0` (24)
- `OSPACEPUT` ← `ocommit` (18)
- `OSPACECOMMIT` ← `ocommit` (6)
- `OBJOPEN` ← `p9_console_init_native` (1), `p9_objopenspace` (1)
- `OBJCLOSE` ← `p9_objclose` (1)
- `OBJINVOKE` ← `obj_read` (349), `obj_write` (161)

`obj_read`- ja `obj_write`-paluuosoitteet ovat libc-kääreen tasolla, koska
`obj_invoke`-kääre ketjuttaa kutsun; ne eivät ole erillisiä kernel-syscalleja.

### 3.2 Pysyvyys kahden buutin yli

Molemmat buutit käyttivät samaa kertakäyttöistä GPT-levyä ja samaa O-
partitiota. Boot 1 committasi version 1 ja boot 2 palautti sen sekä committasi
version 2; `tools/qemu-o-persist.sh` raportoi `result=PASS`.

| vaihe | boot 1 trapit | boot 2 trapit |
|---|---:|---:|
| boot-loader | 3 | 3 |
| MicroPython startup | 0 | 0 |
| MicroPython init + commit | 36 | 39 |

Boot 1 init/commit: `BRK_` 6, `NOTIFY` 1, `_NSEC` 4, `OVOLOPEN` 1,
`OSPACEOPEN` 1, `OSPACEGET` 9, `OSPACEPUT` 3, `OSPACECOMMIT` 1,
`OBJOPEN` 2, `OBJINVOKE` 8.

Boot 2 init/commit: `BRK_` 7, `NOTIFY` 1, `_NSEC` 5, `OVOLOPEN` 1,
`OSPACEOPEN` 1, `OSPACEGET` 10, `OSPACEPUT` 3, `OSPACECOMMIT` 1,
`OBJOPEN` 2, `OBJINVOKE` 8. Boot 2:lla `_NSEC`-kutsujissa näkyy lisäksi
`p9_image_restore_store` (1), mikä vastaa palautettua pysyvää tilaa.

### 3.3 Viisi OBJ-kutsua

| syscall | nr | ajossa | havaitut kerrat (boot / persist 1 / persist 2) | luokka |
|---|---:|---|---:|---|
| `OBJCREATE` | 136 | ei | 0 / 0 / 0 | B |
| `OBJOPEN` | 137 | kyllä | 4 / 2 / 2 | A |
| `OBJCLOSE` | 138 | kyllä | 1 / 0 / 0 | A |
| `OBJBIND` | 139 | ei | 0 / 0 / 0 | B |
| `OBJINVOKE` | 140 | kyllä | 518 / 8 / 8 | A |

`OBJOPEN`/`OBJCLOSE`/`OBJINVOKE` todettiin siis oikeasti ajossa. `OBJCREATE`
ja `OBJBIND` eivät suorittuneet näissä testeissä, mutta niillä on linkitetty
suora kutsuja MicroPythonin moduuliadapteissa; niitä ei voi tämän perusteella
merkitä kuolleeksi.

Staattiset kutsuketjut (lyhennetyt, täydelliset JSON:ssa):

- `OBJCREATE` ← `obj_create` ← `p9_objcreate`, `p9_objcreatege`,
  `p9_objfifocreate`, `p9_vobjcreate`, `p9_vobjfifocreate`;
  sekä ← `obj_pipe_create` ← `p9_objpipecreate`, `p9_pipenew`.
- `OBJOPEN` ← `obj_open` ← `p9_console_probe`, `p9_dprintf`,
  `p9_native_output`, `p9_objopen*`, `p9_space_open` ym.; sekä
  `obj_kbd_open` ← `p9_console_init_native`/`p9_kbdopen`.
- `OBJCLOSE` ← `obj_close` ← `p9_objclose`, `p9_console_restore`,
  `p9_kbdclose`, `p9_tcpclose`, `p9_uartclose`, `p9_pipeclose`,
  `p9_vobjclose`.
- `OBJBIND` ← `obj_bind` ← `p9_objbind`.
- `OBJINVOKE` ← 23 libc-käärettä (`obj_read`, `obj_write`, `obj_info`,
  `obj_list`, ...), joiden kutsujat ovat `p9_*`-moduuliadaptereita.

Riippuvuudet linkitettyyn MicroPythoniin tulevat siis MicroPythonin
`plan9`/`space`-moduulien adaptereista ja niiden libc-kääreistä, eivät
kernelin generoiduista stubeista itsestään.

## 4. Kaikki aktiiviset syscall-numerot

Luokat: **A** = ajossa havaittu; **B** = aktiivinen ja linkitetty suora
kutsuja, jota nämä testit eivät ajaneet; **C** = varattu numero, `nil`
dispatch; **D** = aktiivinen kernel-syscall, jolla ei ole suoraa kutsujaa
mitatussa käyttäjätilassa. D ei tarkoita, että syscall olisi tarpeeton.

| nr | nimi | dispatch | luokka | havaitut | ajo/vaihe |
|---:|---|---|---|---:|---|
| 0 | `SYSR1` | ACTIVE | D | 0 | — |
| 1 | `_ERRSTR` | ACTIVE | D | 0 | — |
| 6 | `ALARM` | ACTIVE | B | 0 | — |
| 8 | `EXITS` | ACTIVE | B | 0 | — |
| 9 | `_FSESSION` | ACTIVE | D | 0 | — |
| 10 | `FAUTH` | ACTIVE | D | 0 | — |
| 12 | `SEGBRK` | ACTIVE | D | 0 | — |
| 17 | `SLEEP` | ACTIVE | B | 0 | — |
| 19 | `RFORK` | ACTIVE | B | 0 | — |
| 24 | `BRK_` | ACTIVE | A | 19 | init+commit 19 |
| 28 | `NOTIFY` | ACTIVE | A | 3 | init+commit 3 |
| 29 | `NOTED` | ACTIVE | B | 0 | — |
| 30 | `SEGATTACH` | ACTIVE | D | 0 | — |
| 31 | `SEGDETACH` | ACTIVE | D | 0 | — |
| 32 | `SEGFREE` | ACTIVE | D | 0 | — |
| 33 | `SEGFLUSH` | ACTIVE | D | 0 | — |
| 34 | `RENDEZVOUS` | ACTIVE | D | 0 | — |
| 36 | `_WAIT` | ACTIVE | D | 0 | — |
| 37 | `SEMACQUIRE` | ACTIVE | D | 0 | — |
| 38 | `SEMRELEASE` | ACTIVE | D | 0 | — |
| 40 | `FVERSION` | ACTIVE | D | 0 | — |
| 41 | `ERRSTR` | ACTIVE | B | 0 | — |
| 47 | `AWAIT` | ACTIVE | B | 0 | — |
| 52 | `TSEMACQUIRE` | ACTIVE | D | 0 | — |
| 53 | `_NSEC` | ACTIVE | A | 25 | init+commit 13, REPL 12 |
| 54 | `OVOLOPEN` | ACTIVE | A | 6 | boot-loader 3, init+commit 3 |
| 55 | `OSPACEOPEN` | ACTIVE | A | 6 | boot-loader 3, init+commit 3 |
| 56 | `OSPACEGET` | ACTIVE | A | 52 | init+commit 28, REPL 24 |
| 57 | `OSPACEPUT` | ACTIVE | A | 27 | init+commit 9, REPL 18 |
| 58 | `OSPACECOMMIT` | ACTIVE | A | 9 | init+commit 3, REPL 6 |
| 59 | `OSPACECLOSE` | ACTIVE | B | 0 | — |
| 60 | `OEXECUTE` | ACTIVE | A | 3 | boot-loader 3 |
| 61 | `OSPACECREATE` | ACTIVE | B | 0 | — |
| 63 | `USBOPEN` | ACTIVE | B | 0 | — |
| 64 | `USBLIST` | ACTIVE | B | 0 | — |
| 65 | `USBEPOPEN` | ACTIVE | B | 0 | — |
| 66 | `USBREAD` | ACTIVE | D | 0 | — |
| 67 | `USBWRITE` | ACTIVE | D | 0 | — |
| 68 | `USBIOCTL` | ACTIVE | D | 0 | — |
| 69 | `USBCLOSE` | ACTIVE | B | 0 | — |
| 70 | `USBEPNAME` | ACTIVE | D | 0 | — |
| 86 | `RTCGET` | ACTIVE | D | 0 | — |
| 87 | `RTCSET` | ACTIVE | D | 0 | — |
| 88 | `ENVGET` | ACTIVE | B | 0 | — |
| 89 | `ENVSET` | ACTIVE | D | 0 | — |
| 90 | `ENVDELETE` | ACTIVE | D | 0 | — |
| 91 | `ENVLIST` | ACTIVE | D | 0 | — |
| 92 | `OSPAWN` | ACTIVE | B | 0 | — |
| 93 | `OTERMINATE` | ACTIVE | B | 0 | — |
| 96 | `OBIND` | ACTIVE | B | 0 | — |
| 97 | `OLOOKUP` | ACTIVE | B | 0 | — |
| 98 | `OUNBIND` | ACTIVE | B | 0 | — |
| 99 | `OREVOKE` | ACTIVE | B | 0 | — |
| 101 | `ONSVIEW` | ACTIVE | B | 0 | — |
| 102 | `ONMOUNT` | ACTIVE | B | 0 | — |
| 103 | `ONUNMOUNT` | ACTIVE | B | 0 | — |
| 104 | `OBINDX` | ACTIVE | B | 0 | — |
| 105 | `ONSVISIBLE` | ACTIVE | B | 0 | — |
| 106 | `OQUERY` | ACTIVE | B | 0 | — |
| 107 | `SYSSNAPSHOT` | ACTIVE | B | 0 | mittauksen ohjaus; ei lasketa |
| 136 | `OBJCREATE` | ACTIVE | B | 0 | — |
| 137 | `OBJOPEN` | ACTIVE | A | 8 | init+commit 6, REPL 2 |
| 138 | `OBJCLOSE` | ACTIVE | A | 1 | REPL 1 |
| 139 | `OBJBIND` | ACTIVE | B | 0 | — |
| 140 | `OBJINVOKE` | ACTIVE | A | 534 | init+commit 24, REPL 510 |

Luokkajakauma: **A 12, B 29, C 70, D 24** (yhteensä 135 määriteltyä nimeä;
65 aktiivista = A+B+D, 70 varattua = C).

Varatut numerot ovat `sys.h`:n kommenttien mukaisesti pysyvästi allokoituja,
mutta dispatch on `nil`. Ne eivät tavoita vanhaa toteutusta. Kahdeksalla
varatulla stubilla on silti linkitetty kutsuja (`CLOSE`, `DUP`, `OPEN`,
`PIPE`, `FD2PATH`, `SEEK`, `PREAD`, `PWRITE`); ne ovat kuolleita vain
suorituksen kannalta, koska dispatch hylkää numeron. Täydellinen lista on
JSON-tiedostossa.

## 5. Testien tila

| testi | tulos | evidenssi |
|---|---|---|
| instrumentoitu kernel + MicroPython build | PASS | `build/syscall-t0/logs/build-*-measure.log` |
| instrumentoitu O-boot → REPL + `6*7` + GE/O-objekti | PASS | `build/syscall-t0/boot/report.txt`, `serial-boot.log` |
| instrumentoitu pysyvyys, kaksi buuttia | PASS | `build/syscall-t0/persist/report.txt`, `serial-boot{1,2}.log` |
| normaalien artifactien uudelleenbuild | PASS | `build/syscall-t0/logs/build-*-normal.log`; normaali kernel ei sisällä trace-koodia |
| normaali O-boot savutesti | PASS | `build/syscall-t0/normal-smoke/report.txt` |
| normaali pysyvyystesti | PASS | `build/syscall-t0/normal-persist/report.txt` |

Normaalit artifactit palautettiin mittauksen jälkeen:

- `build/gcc/9pc64.elf` sha256
  `46c144b61fb407eaafe4dc39bcbbf3fdd58678033a6891e5727902c5032c3308`
- `build/gcc/micropython.elf` sha256
  `bd957a3853989cd28507169608e077384e25ad0a21f33c2a026675f124a54404`
- `build/gcc/gen/obootoid.elf` sha256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`

Mitatut (instrumentoidut) binaryt on säilytetty:

- `build/syscall-t0/binaries/9pc64.elf`
  sha256 `b229f0b855f150bc4d28c2d4fe1dfd03f76ce65e1fa6b4f9271a16411b2c1af5`
- `build/syscall-t0/binaries/micropython.elf`
  sha256 `8402bed108c8d2ec804ace09ab4091ccbbadc816cafc498f2555fc178355279b`
- `build/syscall-t0/binaries/obootoid.elf`
  sha256 `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`

## 6. Rajoitteet ja tulkinta

- Mittaus koskee vain tämänhetkistä amd64/PoC-polkaisua, yhden vCPU:n KVM-
  buuttia ja tuoretta kertakäyttöistä O-volumia. Sitä ei voi yleistää
  kaikkiin laitteistoihin, verkko-, USB- tai UART-konfiguraatioihin.
- Ajonaikainen evidenssi kattaa vain ne polut, jotka testit todella ajoivat.
  Erityisesti `OBJCREATE` ja `OBJBIND` jäivät ajamatta; ne eivät ole kuolleita
  ja ne on pidettävä luokassa B, kunnes lisätestit toisin näyttävät.
- `REPL`-vaiheen suuri `OBJINVOKE`-määrä johtuu konsolin `obj_read`- ja
  `obj_write`-kutsuista; se ei tarkoita 510 eri objektityyppiä.
- Staattinen kutsugraafi perustuu suoriin `call`/tail-`jmp`-kohtiin. Epäsuoria
  funktio-osoittimia ei ole tyhjentävästi todistettu; allokoitujen osioiden
  osoitehaku on vain tukevaa evidenssiä. Pelkkä symbolin olemassaolo ei ole
  suoritusnäyttö.
- `SYSSNAPSHOT` (107) on mittausbuildin checkpoint-kutsu. Normaalissa
  käytössä se on tavallinen aktiivinen syscall; tässä raportissa sitä ei
  lasketa sovelluksen riippuvuudeksi.
- Trace-puskuri on rajattu 32768 alkioon. Näissä ajoissa `dropped=0`.
- Raportti **ei** väitä, että viiden OBJ-kutsun tavoite olisi saavutettu. Se ei
  myöskään suosittele kaikkien ei-havaittujen kutsujen poistamista.

## 7. Artefaktit

- Analyysi ja koneellinen data:
  - `docs/SYSCALL-T0-MEASUREMENT.json`
  - `tools/host/syscall-t0-measure.py`
- Instrumentoidut binaryt:
  - `build/syscall-t0/binaries/9pc64.elf`
  - `build/syscall-t0/binaries/micropython.elf`
  - `build/syscall-t0/binaries/obootoid.elf`
  - `build/syscall-t0/binaries/9pc64.elf.sha256`
- Testilokit:
  - `build/syscall-t0/boot/serial-boot.log`
  - `build/syscall-t0/boot/report.txt`
  - `build/syscall-t0/persist/serial-boot1.log`
  - `build/syscall-t0/persist/serial-boot2.log`
  - `build/syscall-t0/persist/report.txt`
  - `build/syscall-t0/normal-smoke/report.txt`
  - `build/syscall-t0/normal-persist/report.txt`
  - `build/syscall-t0/logs/`
  - `build/syscall-t0/measure-run.out`

Koko mittauksen uusintakomento on `python3
tools/host/syscall-t0-measure.py --run --summary`. Ilman `--run`-valintaa
skripti vain analysoi olemassa olevat lokit uudelleen.
