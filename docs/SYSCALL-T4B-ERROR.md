# T4b — MicroPythonin ERRSTR/rerrstr/werrstr OBJINVOKE-rajapintaan

Tama on **T4b:n** tulos. Tehtava siirtaa MicroPythonin ja sen mukana
linkitetyn libc:n virhetekstipolun (`errstr`/`rerrstr`/`werrstr`/`%r`)
`ERRSTR(41)`-trapista yhteen tyypitettyyn `OBJINVOKE`-operaatioon
(`ObjOpErrstrExchange=29`, `handle=0`). Uutta syscall-numeroa ei otettu
kayttoon. `ERRSTR(41)` ja `_ERRSTR(1)` pysyvat aktiivina kernelin
dispatch-taulussa.

Kanoninen tyopuu on `kuha:/home/lapere/Src/Plan2001-O`. Lahtosnapshot on
`ece5a15363c71fa3ca382e8add7b5740ac188bbe985a78e66672ca6d9f82eaaa`
(T4a:n tulos). Koneellinen data on tiedostossa
[SYSCALL-T4B-ERROR.json](SYSCALL-T4B-ERROR.json). T0-, T1-, T2-, T3- ja
T4a-raportteja ei muutettu.

## 1. Tavoite ja rajaus

MicroPythonin virhetekstien kasittely ei enaa tee `ERRSTR(41)`-trappia.
Polku on:

- `errstr(buf,n)` libc-funktio tayttaa `ObjCall`-rakenteen ja kutsuu
  `obj_invoke(0, &c, sizeof c)`;
- kernelin `OBJINVOKE`-dispatch kasittelee `ObjOpErrstrExchange=29`;
- operaatio validoi yhden kayttajatilan IN/OUT-puskurin ja tekee saman
  vaihdon kuin alkuperainen `generrstr()`.

Operaatio koskee vain kutsuvan prosessin omaa virhetilaa. Se ei avaa,
hae eika kuluta objektikahvaa, joten juuri kahvataulun tayttyminen ei voi
estaa virhetekstin lukemista. T4b ei muuta GE/O:n transaktioita,
muistinhallintaa, aikaa, konsolia, prosessin note-kasittelya, bootloaderia,
verkkoa eika muita syscall-kutsuja.

## 2. Toteutus

Muutetut lahteet:

- `runtime/abi/obj.h` — `ObjOpErrstrExchange=29`, `OBJ_ERRSTR_MAX` ja
  tyypitetty sopimus. Ei uutta syscall-numeroa.
- `kernel/core/sysobj.c` — `OBJINVOKE`-dispatch: `handle=0`, yksi IN/OUT
  puskuri, pituus `[1,ERRMAX]`, varatut kentat nollina; kutsuu
  `generrstr()`ia.
- `kernel/core/sysproc.c` — `generrstr()` on nyt ei-static (sama toteutus);
  mittauskohtainen `objoptrace()`-histogrammi vain
  `PLAN2001_SYSCALL_TRACE`-buildissa.
- `kernel/core/portfns.h` — `generrstr()`- ja `objoptrace()`-julistukset.
- `runtime/libc/9sys/errstr.c` — **uusi** ei-syscallinen
  `errstr(buf,n)`-kaare, joka kutsuu `obj_invoke(0, ObjOpErrstrExchange)`.
  `rerrstr.c`, `werrstr.c` ja `fmt/errfmt.c` sailyvat muuttumattomina ja
  kayttavat tata samaa `errstr()`ia.
- `tools/host/build-mpy-gcc.py` — stub-generaattori jattaa generoimatta
  **vain** `errstr`-symbolin vanhan `ERRSTR(41)`-stubin; `9sys/errstr.c`
  lisattiin `LIBC_SOURCES`-luetteloon. Ei yleista stubien poistoa eika
  duplikaattisymboleja.
- `micropython/modplan9.c` — testikohtaiset `plan9.errstr()`,
  `plan9.werrstr()` ja `plan9.errstr_exchange()` -diagnostiikkafunktiot
  seka `OBJ_OP_ERRSTR_EXCHANGE`-vakio. MicroPythonin varsinaiset
  error-adapterit sailyvat ennallaan.

### Toimintasopimus

- `handle == 0`; operaation onnistuminen ei avaa eika kuluta `Handle`ia.
- `c.out` on ainoa IN/OUT-kayttajapuskuri; `c.outlen` on `[1,ERRMAX]`
  (`ERRMAX=128`).
- `c.in == NULL`, `c.inlen == 0`, `c.arg0 == 0`, `c.arg1 == 0`;
  `flags/rights/reserved == 0`.
- Kernel validoi kayttajaosoitteen ja tekee alkuperaisen `generrstr()`in
  vaihdon: kopioi kutsujan uuden tekstin aiempaan virhealueeseen, palauttaa
  `up->syserrstr`:n puskurissa ja vaihtaa `up->errstr` / `up->syserrstr`
  -osoittimet. Paluuarvo on 0.
- `rerrstr()`n kaksi vaihtoa eivat tyhjenna alkuperaista virhetta;
  `werrstr()`n muotoilu sailyy.
- Onnistunut virheenhaku ei nosta uutta virhetta eika riipu kahvataulun
  koosta. Virhepolut: pituus 0 tai yli `ERRMAX` on `Ebadarg`; virheellinen
  kayttajaosoite torjutaan `generrstr()`in `validaddr`-tarkistuksella.
- `ERRSTR(41)` ja `_ERRSTR(1)` sailyvat aktiivina muita legacy-kayttajia
  varten.

## 3. Mittaus ennen ja jälkeen

Mittaus ajettiin T0:n omalla instrumentoinnilla:

```sh
python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t4b/t0-after.json --summary
```

Kaikki `validation`-kohdat ovat PASS: instrumentoitu boot, instrumentoitu
kahden bootin persistenssi, normaali savutesti, normaali persistenssi ja
normaalin artifactin uudelleenrakennus. Kaikki QEMU-ajot kayttivat KVM:aa ja
kertakayttoista O-kuvaa.

**Ennen**-mittaus on T4a:n julkaistu mittaus samalta pohjalta
(`build/syscall-t4a/t0-after.json`); uutta ennen-ajoa ei tehty, koska
lahtosnapshot on T4a:n tulos. **Jalkeen**-mittaus on
`build/syscall-t4b/t0-after.json`.

| kutsu | ennen (T4a) | jälkeen (T4b) | ero |
|---|---:|---:|---:|
| `_NSEC(53)` | 0 | 0 | 0 |
| `SLEEP(17)` | 0 | 0 | 0 |
| `BRK_(24)` | 0 | 0 | 0 |
| `OSPACEGET(56)` | 0 | 0 | 0 |
| `OSPACEPUT(57)` | 0 | 0 | 0 |
| `OSPACECOMMIT(58)` | 0 | 0 | 0 |
| `OSPACECREATE(61)` | 0 | 0 | 0 |
| `ERRSTR(41)` | 0 | 0 | 0 |
| `_ERRSTR(1)` | 0 | 0 | 0 |
| `OBJOPEN(137)` | 82 | 82 | 0 |
| `OBJINVOKE(140)` | 666 | 666 | 0 |
| `OBJCLOSE(138)` | 72 | 72 | 0 |

T0:n tavallinen MicroPython-ajo onnistuu eika aiheuta virheita, joten
`ERRSTR=0` tavallisessa ajossa **ei yksin todista** siirtoa. Laskurien
ennallaan pysyminen osoittaa, ettei T4b lisaa OBJ-kutsuja tavalliseen
polkuun eika riko T1-T4a:n nollia. Siirron todentaa erillinen
instrumentoitu virhetesti (luku 4).

## 4. Erillinen instrumentoitu virhetesti

Instrumentoitu kernel ja MicroPython rakennettiin
`PLAN2001_SYSCALL_TRACE=1`-lipulla. REPL-ajo
(`build/syscall-t4b/inputs/repl-t4b-trace.py`) teki 10 kertaa
epaonnistuvan `OBJINVOKE(ObjOpHeapBreak=26)`-kutsun virheellisella
kahvalla ja 10 kertaa `plan9.errstr()`in. Checkpointit:

| checkpoint | label | records | dropped |
|---|---|---:|---:|
| 1 | boot-loader | 3 | 0 |
| 2 | snapshot | 0 | 0 |
| 3 | snapshot | 67 | 0 |
| 4 | snapshot | 1176 | 0 |

Luettuja rekistereita yhteensa 1246, `dropped=0`. Kaikki syscall-numerot:

| syscall | maara (kaikki) | maara (micropython) |
|---|---:|---:|
| `NOTIFY(28)` | 1 | 1 |
| `OVOLOPEN(54)` | 2 | 1 |
| `OSPACEOPEN(55)` | 2 | 1 |
| `OEXECUTE(60)` | 1 | 0 |
| `OBJOPEN(137)` | 35 | 35 |
| `OBJCLOSE(138)` | 31 | 31 |
| `OBJINVOKE(140)` | 1174 | 1174 |
| `ERRSTR(41)` | **0** | **0** |
| `_ERRSTR(1)` | **0** | **0** |

ObjOp-koodit (kaikki checkpointit yhteensa):

| ObjOp | merkitys | maara |
|---|---|---:|
| 2 | Read | 586 |
| 3 | Write | 500 |
| 13 | Commit | 5 |
| 25 | PutOid | 15 |
| 26 | HeapBreak (epaonnistuva testikutsu) | 16 |
| 27 | TimeNow | 12 |
| **29** | **ErrstrExchange** | **40** |

`ERRSTR(41)=0` ja `_ERRSTR(1)=0`, mutta `ObjOpErrstrExchange(29)`
suorittui 40 kertaa. Virheenhaku toteutui siis tyypitettyna
OBJ-operaationa, ei vanhana trappina. Myos toistuvat epaonnistuvat
syscall-kutsut kirjautuivat (`ObjOpHeapBreak`, 10 kertaa checkpointissa 4).

## 5. Testit

| testi | tulos | evidenssi |
|---|---|---|
| kanoninen kernel-build | PASS | `build/syscall-t4b/logs/build-kernel-normal.log` |
| kanoninen MicroPython-build | PASS | `build/syscall-t4b/logs/build-mpy-normal.log`; 247 unit, 0 FAIL, ei undefined-symboleita |
| EFI- ja host-build | PASS | `build/syscall-t4b/logs/build-efi-normal.log`, `build/syscall-t4b/logs/build-host-normal.log` |
| O-boot -> REPL -> `6*7`=42 | PASS | `build/syscall-t4b/boot-final/report.txt`; `T4B-MATH 42` |
| kahden bootin GE/O-persistenssi | PASS | `build/syscall-t4b/persist/report.txt`; `T3P-BOOT1 OK`, `T3P-BOOT2 OK`, `t3fun(2)=202` |
| kaksi eri todellista kernelvirhetta | PASS | `T4B-E1 bad arg in system call`; `T4B-E2 permission denied` |
| rerrstr kahden swapin protokolla | PASS | `T4B-R1` = `T4B-R2` = `bad arg in system call` |
| werrstr muotoilee, rerrstr näyttää | PASS | `T4B-W1` = `T4B-W2` = `oma virhe` |
| uusi syscall-virhe palauttaa uuden virheen | PASS | `T4B-R3 permission denied` |
| tyhja merkkijono | PASS | `T4B-EMPTY ''` |
| rajattu puskuri | PASS | `T4B-BOUNDED b'0123456'` (n=8 -> 7 merkkiä + NUL) |
| pitka/UTF-8-teksti | PASS | `T4B-UTF8 oma äö` |
| virheenhaku ei vaadi objektikahvaa | PASS | `T4B-HANDLES 92` (taulu taynna); `T4B-EXHAUST vobj: too many handles` luettiin onnistuneesti; sovellus ei kaatunut |
| ei korvaa alkuperaista virhetta | PASS | kaksi peräkkäistä lukua samat; uusi virhe vasta uuden syscallin jälkeen |
| T0-instrumentointi ennen/jälkeen | PASS | `build/syscall-t4a/t0-after.json`, `build/syscall-t4b/t0-after.json` |
| erillinen instrumentoitu virhetesti | PASS | `build/syscall-t4b/trace/report.txt`, `build/syscall-t4b/trace-summary.json`; `ERRSTR=0`, `_ERRSTR=0`, `ObjOpErrstrExchange=40`, `dropped=0` |
| staattinen: ei kutsuja vanhaan errstr-stubiin | PASS | `build/syscall-t4b/logs/micropython.disasm.txt` |
| `ERRSTR(41)`, `_ERRSTR(1)` aktiivina | PASS | `build/gcc/gen/systab-gcc.h` |
| ennen/jälkeen vertailu T4a-toteutukseen | PASS | `/tmp/t4a-base/build/syscall-t4b-base/boot/boot-boot.log` |
| muiden kayttajatilojen ABI-yhteensopivuus | NOT RUN | tehtava kieltaa vaittamasta sita ilman testia |
| fyysinen rauta, TCG, laaja regressio | NOT RUN | tehtavan rajauksen ulkopuolella |

## 6. Ennen/jälkeen vertailu T4a-toteutukseen

Lahtosnapshotista `ece5a15…` rakennettiin erillinen T4a-vertailupuu
`/tmp/t4a-base` (ei kanoninen tyopuu). Sen MicroPython-SHA on
`fb2325804bce4817796d46cf41b10d425c1f0d62f5a126bdf5e070eb6fed99a5`,
joka vastaa T4a:n julkaistua artifacttia. Sama virhetesti ajettiin seka
T4a- etta T4b-toteutuksella:

| virhe | T4a (ERRSTR(41)) | T4b (ObjOpErrstrExchange) |
|---|---|---|
| virheellinen kahva | `bad arg in system call` | `bad arg in system call` |
| oikeuspuute (`VMETA` + Sleep) | `permission denied` | `permission denied` |

Todelliset kernelin virhetekstit ovat identtiset. Ero on vain
kuljetusrajapinnassa: T4a käytti `ERRSTR(41)`-trappia, T4b käyttää
`OBJINVOKE(ObjOpErrstrExchange=29)`ia saman `generrstr()`-toteutuksen
yli.

## 7. Staattinen linkitys

`build/gcc/micropython.elf`:

- `errstr` on libc-funktio `0x24dd20`; sen runko kutsuu `obj_invoke`ia
  eika tee suoraa syscall-trapia.
- `rbp`-valittomat syscall-numerot disassembloinnissa:
  `0x29` (`ERRSTR=41`) **0 kappaletta**; `0x8c` (`OBJINVOKE=140`)
  1 kappale; `0x1` (`_ERRSTR=1`) 1 kappale.
- Vanhaa `errstr`-stubia ei ole lainkaan. Jaljelle jääva `_errstr`-stubi
  (syscall 1) on linkitetty legacy-symboli, jolla on **0** `call`- ja
  **0** tail-`jmp`-kutsupaikkaa.
- `errstr`-kutsupaikkoja on 4: MicroPythonin error-adapteri `0x209f0a`,
  `rerrstr`:n kaksi vaihtoa `0x24e02b`/`0x24e048` ja `werrstr` `0x24e207`.
- `build/gcc/gen/systab-gcc.h` sisaltaa edelleen `[_ERRSTR] sys_errstr` ja
  `[ERRSTR] syserrstr` -dispatchn.

## 8. Jäljelle jäävät legacy-kutsujat

- `ERRSTR(41)` ja `_ERRSTR(1)` pysyvat aktiivina kernelin dispatchissa ja
  generoidussa `systab-gcc.h`:ssa.
- `_errstr`-stubi generoituu edelleen, koska tehtava kielsi vain `errstr`:n
  vanhan stubin poistamisen. Silla ei ole kutsupaikkoja.
- `runtime/libc/9sys/rerrstr.c`, `werrstr.c` ja `fmt/errfmt.c` sailyvat
  lahteina muuttumattomina; ne kayttavat uutta `errstr()`ia.
- Muut kayttajatilat eivat kuulu taman tehtavan piiriin eika niiden
  ABI-yhteensopivuutta väitetä.

## 9. Artefaktit

Normaalit, testatut artifactit:

- `build/gcc/9pc64.elf` — 1 154 376 B, SHA-256
  `d6d4c748e8a240ab9865f91a96323e48caa3fbb2c5ca01aaa12cc8e94bca40c9`
- `build/gcc/micropython.elf` — 538 440 B, SHA-256
  `d8aa497b95bc91d99f4a017d3352346b91ab2928179f8587a30eaee9ac4b1852`
- `build/gcc/gen/obootoid.elf` — 5 456 B, SHA-256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`
- `build/gcc/bootx64.efi` — 22 016 B, SHA-256
  `56dabb4ff0e74c633fc940db1a8430a77aa6f83cd32a844be333bef5078f5687`

Mitatut (instrumentoidut) binaryt:

- trace-ajo: kernel
  `ff3fc764bd6610580bc9b14e72962dea7c4cacdf9fa43e8179cb1479eb76d84a`
  (1 154 816 B), MicroPython
  `320178d3a60903159aef5ca213a5221ab9def21df005a330347c6ed12a19e58c`
  (538 440 B), loader
  `8092cff2a361ba68984b183ecab72270acb5d4afb774ca1309749951b907a4d9`
- T0-mittarin instrumentoitu MicroPython vastaa samaa
  `320178d3…`-SHA:ta.

Normaalin kernelin merkkijonoista ei loydy `P2K-SYSCALL`-tekstia.

## 10. Toistettavat komennot

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
tools/build-efi-gcc.sh -j 8
tools/build.sh

tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t4b/boot-final --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t4b/inputs/repl-t4b.py \
  --expect "T4B-OK"

QOB_INI_BOOTEXEC=0x4f45584543000003 tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t4b/persist --timeout 360 --repl \
  --boot1-input build/syscall-t3/inputs/repl-t3-persist-boot1.py \
  --boot2-input build/syscall-t3/inputs/repl-t3-persist-boot2.py \
  --boot1-expect "T3P-BOOT1 OK" --boot2-expect "T3P-BOOT2 OK"

python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t4b/t0-after.json --summary

PLAN2001_SYSCALL_TRACE=1 tools/build-gcc.sh --kernel -j 8
PLAN2001_SYSCALL_TRACE=1 tools/build-mpy-gcc.sh -j 8
tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t4b/trace --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t4b/inputs/repl-t4b-trace.py \
  --expect "T4B-TRACE-DONE"

tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
```

## 11. Rajoitteet

- Vain amd64 PoC `kuha`/QEMU-KVM:lla; ei fyysista rautaa eika TCG:ta.
- T0:n tavallinen MicroPython-ajo ei aiheuta virheita, joten `ERRSTR=0` ei
  yksin todista siirtoa. Siirto todennettiin erillisella instrumentoidulla
  virhetestilla, jossa `ObjOpErrstrExchange=40`.
- `_ERRSTR(1)`-stubi generoituu ja on linkitetty, mutta silla ei ole
  kutsupaikkoja. `ERRSTR(41)`-stubia ei generoida lainkaan.
- `ERRSTR(41)` ja `_ERRSTR(1)` sailyvat tarkoituksella aktiivina.
- Muiden kayttajatilojen ABI-yhteensopivuutta ei testattu eika väitetä.
- Kernelin SHA vaihtelee build-aikaleiman mukana; lahteet ja
  MicroPython-SHA ovat deterministisia.
