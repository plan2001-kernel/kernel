# T5b — NOTED OBJINVOKE-rajapintaan ja MPY:n note-ABI kuntoon

> **Historical report (T0–T6 phase).** Current direct-syscall state: [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md).

Tämä on **T5b:n** tulos. Tehtävä siirtää note-paluu (`NCONT`/`NDFLT`/
`NSAVE`/`NRSTR`) yhteen tyypitettyyn `OBJINVOKE`-operaatioon
(`ObjOpNoted=31`) ja korjaa amd64-kerneltin note-callbackin siirtymä GCC:n
SysV ABI:lle. `NOTIFY(28)` ja `NOTED(29)` poistettiin aktiivisesta
kernel-dispatchista ja jätettiin **RESERVED**-numeroiksi. Uutta
syscall-numeroa ei otettu käyttöön.

Kanoninen työpuu on `kuha:/home/lapere/Src/Plan2001-O`. Tehtävän
lähtösnapshot on
`bae3e60bc2c30cdbba75ec80372d70893e0a341cce5858d9d0976a176af3c5b5`.
Koneellinen data on tiedostossa
[SYSCALL-T5B-NOTED.json](SYSCALL-T5B-NOTED.json). T0–T5a:n raportteja ei
muutettu.

## 1. Tavoite ja rajaus

MicroPythonin note-polku on nyt kokonaan OBJ-rajapinnan varassa:

- `micropython/main.c` kutsuu edelleen `atnotify(onnote, 1)`;
- `runtime/libc/port/atnotify.c` kutsuu edelleen `notify(notifier)` ja
  `noted(NCONT/NDFLT)`;
- `notify` on `runtime/libc/9sys/notify.c`:n libc-funktio
  `OBJINVOKE(ObjOpNotifyRegister=30)`:n yli;
- `noted` on uusi `runtime/libc/9sys/noted.c`:n libc-funktio
  `OBJINVOKE(ObjOpNoted=31)`:n yli;
- kumpikin käyttää kutsujan omaa note-tilaa; `notify` avaa oman
  `ObjOpenProcessSelf`-kahvan (`Vcontrol`), `noted` käyttää handlea 0,
  koska operaatio voi kohdistua vain kutsujaan itseensä, kuten
  `ObjOpErrstrExchange`;
- kernelin `notecont()` on entinen `sysnoted()`-ydin sellaisenaan;
- amd64 `notify()` rakentaa nyt SysV-kutsukehyksen ja `syscall()` palaa
  `noteret`-polkua, joka palauttaa RDI/RSI:n Ureg-kehyksestä.

T5b ei muuta `ALARM(6)`-kutsua, tick/HZ-laskentaa, alarm-palvelun
käynnistystä, GE/O-formaattia, tallennusta, bootloaderia, verkkoa,
prosessien elinkaarta eikä muita syscalleja. Muita käyttäjätilaohjelmia ei
oteta yhteensopivuusperusteeksi.

## 2. Toteutus

### 2.1 Uusi OBJINVOKE-operaatio

- `runtime/abi/obj.h` — `ObjOpNoted=31` ja tyypitetty sopimus.
- `kernel/core/sysobj.c` — `OBJINVOKE`-dispatch: `handle` on 0
  (kutsuja-rajattu) tai kutsujan oma `VkindProcessSelf`-kahva, jolla on
  `Vcontrol`; `in/out` ja niiden pituudet nollia, `arg1==0`. Kutsuu
  `vobjnoted(handle, arg0)`.
- `kernel/core/sysvobj.c` / `vobjint.h` — `vobjnoted()`: handle 0 = vain
  kutsuja; ei-nolla kahva tarkistetaan `vobjholdobj(handle, Vcontrol)` ja
  `VkindProcessSelf`-rajalla. Kutsuu jaettua `notecont()`-ydintä.
- `kernel/core/sysproc.c` — `sysnoted()`-runko siirrettiin nimelle
  `notecont(int)`. `sysnotify()` ja `sysnoted()` dispatch-kääreet
  poistettiin; jaettu `setnotify()`, `noted()` ja `notecont()` säilyvät
  OBJ-käytössä.
- `tools/host/build-kernel.py` — `sysnotify`/`sysnoted` lisättiin
  eksplisiittiseen RESERVED-joukkoon. Generoitu systab on
  `[NOTIFY] nil`, `[NOTED] nil`, ja `sysreserved[NOTIFY/NOTED]=1`.

`NCONT/NDFLT/NSAVE/NRSTR`-semantiikka on täsmälleen entinen:
`notecont()` tekee saman `up->notified`-tarkistuksen, `fpunoted()`in,
`noteureg`-ketjun validoinnin ja `switch`-haaran. `NDFLT` johtaa edelleen
`pexit()`iin; virheellisestä tilasta ei tule onnistunutta paluuta.

### 2.2 Erityinen note-paluu

`ObjOpNoted` ei saa käyttäytyä tavallisena OBJINVOKE-paluuarvona.
Toteutus:

1. `vobjnoted()` ajaa `notecont()`in. Vasta onnistumisen jälkeen
   `sysobjinvoke()` asettaa yksityisen paluumarkkerin
   `OBJINVOKE_NOTED_RET` (`kernel/core/vobjint.h`).
2. `dosyscall()` tunnistaa `scallnr == OBJINVOKE && ret ==
   OBJINVOKE_NOTED_RET` ja palauttaa `1` kirjoittamatta tavallista
   `*retp`:tä. Virheellinen tai validoimaton operaatio ei koskaan aseta
   markkeria; se etenee normaaliin virhepolkuun.
3. `kernel/amd64/trap.c:n syscall()` palauttaa nyt `int`-arvon:
   `1` tarkoittaa, että paluu on tehtävä `noteret`-polkua.
4. `kernel/amd64/gcc/lentry.S` haarautuu `call syscall`in jälkeen
   `noteret`-etikettiin, kun paluuarvo on ei-nolla.

Vanha `((void**)&ureg)[-1] = noteret` oli Plan 9 6c -ABI:n oivallus,
joka ei GCC-kehyksessä osu paluuosoitteeseen. Se ei siirtänyt paluuta
`noteret`iin, vaan tavallinen `forkret`/`sysretq` nollasi RDI:n ja RSI:n.
Siksi T5a:n diagnostinen callback joutui lukemaan Plan 9 -pinokehyksen.
Nyt paluu kulkee oikeasti `noteret`in kautta, joka palauttaa kaikki
Ureg-rekisterit ja tekee `iretq`:n.

### 2.3 Note-callbackin SysV ABI

`kernel/amd64/trap.c:n notify()`:

- kopioi Uregin ja jättää `[nureg-BY2WD]`-paikan `noteureg`-ketjulle;
- kopioi note-tekstin (`ERRMAX`) alemmas käyttäjäpinoon;
- rakentaa SysV-kutsukehyksen: `RSP % 16 == 8` funktioon tultaessa,
  `[RSP] = 0` turvallisena paluuosoitteena;
- asettaa `RDI = Ureg*`, `RSI = note-teksti`, `RBP = 0`,
  `CS/SS` käyttäjäsegmentteihin.

Jos callback palaa kutsumatta `noted()`ia, paluu 0:aan aiheuttaa
käyttäjätilan faultin eikä jatka kernel-kontekstissa. Normaali
`atnotify`-notifier ei palaa tätä kautta: se kutsuu `noted(NCONT/NDFLT)`.

`runtime/libc/9sys/noted.c` tekee `noted(int)`-kääreen OBJ-operaation yli.
`tools/host/build-mpy-gcc.py` jättää sekä `notify`- että `noted`-stubit
generoimatta ja lisää `9sys/noted.c`:n libc-lähteisiin. `main.c::onnote`
ja `atnotify.c::notifier` säilyvät normaaleina callback-reitteinä.
Testidiagnostiikka `modplan9.c:ssä` käyttää nyt normaaleja SysV-parametreja
(`ureg`, `msg`), ei Plan 9 -pinokehyksen lukua.

## 3. Mittaus ennen ja jälkeen

T0-mittari ajettiin T5a:n tapaan:

```sh
python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t5b/t0-after.json --summary
```

**Ennen** on T5a:n julkaistu mittaus
(`build/syscall-t5a/t0-after.json`); **jälkeen** on
`build/syscall-t5b/t0-after.json`. Kaikki viisi validaatiokohtaa
(instrumentoitu boot, instrumentoitu persistenssi, normaali savutesti,
normaali persistenssi, normaalin artifactin uudelleenrakennus) olivat
PASS, ja kaikki QEMU-ajot käyttivät KVM:ää.

| kutsu | ennen (T5a) | jälkeen (T5b) | huom |
|---|---:|---:|---|
| `NOTIFY(28)` | 0 (ACTIVE) | **0 (RESERVED)** | ei enää dispatchiä |
| `NOTED(29)` | 0 (ACTIVE) | **0 (RESERVED)** | ei enää dispatchiä |
| `OBJOPEN(137)` | 85 | 85 | ennallaan |
| `OBJCLOSE(138)` | 75 | 75 | ennallaan |
| `OBJINVOKE(140)` | 669 | 669 | ennallaan |
| `_NSEC(53)`, `SLEEP(17)`, `BRK_(24)`, `ERRSTR(41)`, `_ERRSTR(1)` | 0 | 0 | ennallaan |

`any_reserved_or_unknown_traps=false`. T1–T5a:n nollatulokset säilyvät.

## 4. Instrumentoitu note-testi

Erillinen trace-kernel ja MicroPython rakennettiin lipuilla
`PLAN2001_SYSCALL_TRACE=1`. Ajo
`build/syscall-t5b/inputs/repl-t5b-trace-min2.py` rekisteröi
testicallbackin `ObjOpNotifyRegister`illa ja laukaisi kaksi aitoa
kernel-notee epäkelvolla käyttäjäosoitteella. Checkpointit:

| checkpoint | label | records | dropped |
|---|---|---:|---:|
| 1 | boot-loader | 3 | 0 |
| 2 | snapshot | 0 | 0 |
| 3 | snapshot | 69 | 0 |
| 4 | snapshot | 1262 | 0 |

Checkpoint 3 (käynnistys ennen REPL-komentoja):

| mittari | arvo |
|---|---:|
| `ObjOpNotifyRegister(30)` | **1** |
| `NOTIFY(28)`-syscallit | **0** |
| `NOTED(29)`-syscallit | **0** |
| dropped | 0 |

Checkpoint 4 (kahden noten jälkeen):

| mittari | arvo |
|---|---:|
| `ObjOpNotifyRegister(30)` | **1** (käsin tehty rekisteröinti) |
| `ObjOpNoted(31)` | **2** |
| `NOTIFY(28)`-syscallit | **0** |
| `NOTED(29)`-syscallit | **0** |
| dropped | 0 |

Lokissa on 1243 `OBJINVOKE(140)`-riviä eikä yhtään `nr=28`- tai
`nr=29`-riviä.

## 5. Testit

| testi | tulos | evidenssi |
|---|---|---|
| kanoninen kernel-build | PASS | `build/syscall-t5b/logs/`; `tools/build-gcc.sh --kernel` |
| kanoninen MicroPython-build | PASS | 249 unit, 0 FAIL, ei undefined-symboleita; `tools/build-mpy-gcc.sh` |
| EFI- ja host-build | PASS | `tools/build-efi-gcc.sh`, `tools/build.sh` |
| O-boot -> REPL -> `6*7`=42 | PASS | `build/syscall-t5b/boot-canonical-final/report.txt`; `T5B-MATH 42` |
| kahden bootin GE/O-persistenssi | PASS | `build/syscall-t5b/persist-final/report.txt`; `T3P-BOOT1 OK`, `T3P-BOOT2 OK` |
| T0-instrumentointi ennen/jälkeen | PASS | `build/syscall-t5b/t0-before.json`, `build/syscall-t5b/t0-after.json` |
| normaalin käynnistyksen `ObjOpNotifyRegister` | PASS | trace checkpoint 3: `op=30 count=1`, `NOTIFY=0`, `NOTED=0` |
| kaksi perättäistä notea uudella paluulla | PASS | `build/syscall-t5b/boot-probe-final/boot-boot.log`: `T5B-PROBE-COUNT 1`, `T5B-PROBE-COUNT2 2`, `T5B-PROBE-LAST2 sys: bad address in syscall pc=0x24ae7e`, `T5B-PROBE-ALIVE`, commit, `T5B-PROBE-DONE` |
| note-teksti SysV RSI:ssä | PASS | `T5B-PROBE-LAST` ja `LAST2` sisältävät oikean kernel-noten tekstin |
| `Ureg*` SysV RDI:ssä | PASS | `T5B-PROBE-FRAME` neljäs arvo = 140737488349192 (käyttäjän Ureg-osoite) |
| NCONT uuden `ObjOpNoted`in kautta | PASS | molemmat probenotet palasivat ja REPL committoi |
| oikea MPY `main.c::onnote` -> `atnotify.c::notifier` | PASS | `build/syscall-t5b/boot-onnote-final/boot-boot.log`: `T5B-ON2-EXC interrupted`, `T5B-ON2-MS 10`, `T5B-ON2-ALIVE`, `T5B-ON2-DONE` |
| rekisteröinnin poisto ja `nil` | PASS | `T5B-BND-REG-NIL 0`, `T5B-BND-REG-NIL2 0` |
| osoitteen validointi | PASS | `T5B-BND-REG-BADADDR bad arg in system call` |
| oikeuspuute | PASS | `T5B-BND-REG-NORIGHT permission denied`, `T5B-BND-NOTED-NORIGHT permission denied` |
| väärä kahva / op | PASS | `T5B-BND-REG-BADHANDLE`, `T5B-BND-REG-HANDLE0`, `T5B-BND-REG-BADOP obj: operation not supported` |
| note-paluu kun note ei ole aktiivinen | PASS | `T5B-BND-NOTED-NOACT-0/-H bad arg in system call` |
| `NSAVE`/`NDFLT` ilman aktiivista notea | PASS | `T5B-BND-NOTED-NSAVE-NOACT`, `T5B-BND-NOTED-NDFLT-NOACT` = `bad arg in system call` |
| `arg1!=0` hylkäys | PASS | `T5B-BND-NOTED-ARG1 bad arg in system call` |
| varatut raakanumerot | PASS | `T5B-BND-RAW-NOTIFY`, `T5B-BND-RAW-NOTED` = `bad arg in system call` |
| kontrolloitu NDFLT erillisessä child-prosessissa | PASS | `build/syscall-t5b/boot-ndflt-final/boot-boot.log`: `T5B-NDF3-NDFLTWAIT (11, 'micropython 11: sys: bad address in syscall pc=0x24ae7e')`, parent `ALIVE` |
| NRSTR ilman aktiivista notea child-prosessissa | PASS | `build/syscall-t5b/boot-nrstr-final2/boot-boot.log`: `T5B-NRSTR-WAIT (10, 'micropython 10: Suicide')`, parent `ALIVE` |
| ei kernel panic -tapauksia testeissä | PASS | kaikki yllä olevat ajot päätyivät normaaliin promptiin tai kontrolloituun child-exitiin |
| staattinen: MPY ei käytä NOTED-stubia | PASS | `nm`/`objdump`: ei `rbp=28`/`rbp=29`-stubia; `noted` 0x24e150 kutsuu `obj_invoke`ia |
| staattinen: bootloader ei käytä NOTIFY/NOTED | PASS | `build/gcc/gen/obootoid.elf`: ei symboleita eikä stub-rivejä |
| kernelin 28/29 RESERVED | PASS | `build/gcc/gen/systab-gcc.h`: `[NOTIFY] nil`, `[NOTED] nil`; `sysreserved` = 1 |
| `interrupt`/`KeyboardInterrupt` | NOT RUN | tässä QEMU-ympäristössä ei interrupt-noten lähdettä |
| NCONT/NSAVE/NRSTR täysi käyttösykli aktiivisella notella | NOT RUN | vain NCONT ja NDFLT testattiin oikealla toimituksella; NRSTR testattiin vain ilman aktiivista notea |
| fyysinen rauta, TCG, laaja regressio | NOT RUN | tehtävän rajaus |

### 5.1 Rajoitettu NDFLT-ajo

Koska boot-prosessin NDFLT lopettaa koko käynnistysohjelman, NDFLT
testattiin `rfork(16)`-childissä. Parent sai täsmälleen odotetun
exit-viestin eikä kernel panic -tilaa tullut:

```
T5B-NDF3-OKWAIT (10, 'micropython 10: CHILDOK')
T5B-NDF3-NDFLTWAIT (11, 'micropython 11: sys: bad address in syscall pc=0x24ae7e')
T5B-NDF3-ALIVE
T5B-NDF3-DONE
```

## 6. Artefaktit

Normaalit, testatut artifactit:

- `build/gcc/9pc64.elf` — 1 154 448 B, SHA-256
  `22c4df47306706f08bfd58c2267f0bca7bafe27b6ea7cb7239a889872b8260fc`
- `build/gcc/micropython.elf` — 543 256 B, SHA-256
  `40f185e45a308e7be16d42dc07d43253a492096c1ac69bcf8ac5390e054235cb`
- `build/gcc/gen/obootoid.elf` — 5 456 B, SHA-256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`
- `build/gcc/bootx64.efi` — 22 016 B, SHA-256
  `3e924e73a0b0fcb5446e51f6e8400968f840581ba6a7166f782ee69f25900afd`

Mitatut testibinaryt:

- alarm/onnote-testi: kernel
  `c37bafb121d5f40a65cda183996ee31235dbb3f3382dd2db7ab5a2d0f7f2e24f`;
  MicroPython `40f185e45a308e7be16d42dc07d43253a492096c1ac69bcf8ac5390e054235cb`.
- trace-testi: kernel
  `c45af86effdeb9bbdb8cd3d7dd6fa588e76ab9177b4c2e0f7912f370a92d70e2`;
  MicroPython `0247e4c4cf55074bc5884bea5dfd63f2e4e515461562f51ba982b42fc840477b`.
- Normaalin kernelin SHA vaihtelee build-aikaleiman mukana; MicroPython-SHA
  on deterministinen.

## 7. Toistettavat komennot

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
tools/build-efi-gcc.sh -j 8
tools/build.sh

bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5b/boot-canonical-final --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5b/inputs/repl-t5b-smoke.py \
  --expect "T5B-MATH 42"

QOB_INI_BOOTEXEC=0x4f45584543000003 bash tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t5b/persist-final --timeout 360 --repl \
  --boot1-input build/syscall-t3/inputs/repl-t3-persist-boot1.py \
  --boot2-input build/syscall-t3/inputs/repl-t3-persist-boot2.py \
  --boot1-expect "T3P-BOOT1 OK" --boot2-expect "T3P-BOOT2 OK"

python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t5b/t0-after.json --summary

bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5b/boot-probe-final --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5b/inputs/repl-t5b-probe.py \
  --expect "T5B-PROBE-DONE"

bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5b/boot-boundary-final2 --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5b/inputs/repl-t5b-boundary.py \
  --expect "T5B-BND-DONE"

bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5b/boot-ndflt-final --timeout 120 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5b/inputs/repl-t5b-ndflt3.py \
  --expect "T5B-NDF3-DONE"

bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5b/boot-nrstr-final2 --timeout 120 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5b/inputs/repl-t5b-nrstr-child.py \
  --expect "T5B-NRSTR-DONE"

PLAN2001_ALARM_NOTETEST=1 tools/build-gcc.sh --kernel -j 8
bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5b/boot-onnote-final --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5b/inputs/repl-t5b-onnote2.py \
  --expect "T5B-ON2-DONE"

PLAN2001_SYSCALL_TRACE=1 tools/build-gcc.sh --kernel -j 8
PLAN2001_SYSCALL_TRACE=1 tools/build-mpy-gcc.sh -j 8
bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5b/trace-probe4 --timeout 600 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5b/inputs/repl-t5b-trace-min2.py \
  --expect "P2K-OBJOP checkpoint=4 op=31 count=2"

tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
```

## 8. Rajoitukset ja jäljelle jäävät riippuvuudet

- Vain amd64 PoC `kuha`/QEMU-KVM:lla; ei fyysistä rautaa eikä TCG:tä.
- `NOTIFY(28)` ja `NOTED(29)` ovat RESERVED eivätkä enää dispatchaa.
  `syscallfmt` säilyttää vielä niiden nimet diagnostiikkaa varten.
- Jaetut `setnotify()`, `noted()` ja `notecont()` säilyvät, koska OBJ
  käyttää niitä.
- Normaali kernel ei käynnistä `alarmkproc()`ia. Onnote-testi käyttää
  `PLAN2001_ALARM_NOTETEST=1`-testibuildia, joka ei muuta `alarm.c`:tä
  eikä `ALARM`-syscallia. Normaalin ALARMin ei väitetä muuttuneen.
- Tunnettu erillinen ongelma: `alarm(200)` laukeaa tick/HZ-muunnoksen
  vuoksi huomattavasti pyydettyä aikaisemmin (tässä ajossa noin 10 ms).
  T5b ei korjaa tätä eikä `ALARM`-polkua.
- `interrupt`/`KeyboardInterrupt` ja NCONT/NSAVE/NRSTR:n täysi
  aktiivisen noten sykli ovat NOT RUN; niitä ei väitetä testatuiksi.
- Muiden käyttäjätilojen ABI-yhteensopivuutta ei testattu eikä väitetä.
- Kernelin SHA vaihtelee build-aikaleiman mukana; MicroPython-SHA ja
  lähteet ovat deterministisiä.
