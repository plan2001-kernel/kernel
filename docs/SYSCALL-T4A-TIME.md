# T4a — MicroPythonin ajanluku ja sleep OBJ-aikapalveluun

> **Historical report (T0–T6 phase).** Current direct-syscall state: [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md).

Tama on **T4a:n** tulos. Tehtava siirtaa MicroPythonin ajanluvun ja
sleep-polut viiden OBJ-systeemikutsun rajapintaan. Uutta syscall-numeroa ei
otettu kayttoon. `_NSEC(53)`, `SLEEP(17)` ja `BRK_(24)` pysyvat aktiivina
kernelin dispatch-taulussa.

Kanoninen tyopuu on `kuha:/home/lapere/Src/Plan2001-O`. Lahtosnapshot on
`0a17480bede657db0d18a600eb43f5dbf0efd07da8a633ae625525dd1b5402b4`
(T3:n tulos). Koneellinen data on tiedostossa
[SYSCALL-T4A-TIME.json](SYSCALL-T4A-TIME.json). T0-, T1-, T2- ja
T3-raportteja ei muutettu.

## 1. Tavoite ja rajaus

MicroPythonin ajanluku ei enaa tee `_NSEC(53)`-trappia eika `plan9.sleep`
`SLEEP(17)`-trappia. Polku on:

- avaa kutsujan oma prosessin kahva: `OBJOPEN(ObjOpenProcessSelf=14)`;
- lue aika: `OBJINVOKE(ObjOpTimeNow=27, out=8 tavua)`;
- nuku tai yieldaa: `OBJINVOKE(ObjOpSleep=28, arg0=ms)`;
- vapauta kahva: `OBJCLOSE`.

Aika tulee samasta kernelin `todget(nil,nil)`-lahteesta kuin `_NSEC`:
64-bittinen nanosekuntiluku, sama aikakanta ja tarkkuus. Sleep kayttaa samaa
`kernelsleep(long)`-toteutusta kuin `SLEEP(17)`: `ms<=0` yieldaa (saillyttaen
EDF-etusijan) ja positiivinen odottaa `tsleep`illa.

T4a ei muuta GE/O:n transaktioita, muistinhallintaa, konsolin toimintaa,
REPL:n ajosemantiikkaa, prosessi-/note-/alarm-toimintoja, bootloaderia, USB:ta
eika verkkoa. ERRSTR, NOTIFY, NOTED, ALARM ja prosessien hallinta eivat kuulu
tahan tehtavaan.

## 2. Toteutus

Muutetut lahteet:

- `runtime/abi/obj.h` — `ObjOpTimeNow=27` ja `ObjOpSleep=28`; kayttajatilan
  `obj_process_self_time()` / `obj_process_self_sleep()` -julistukset.
- `kernel/core/sysobj.c` — `OBJINVOKE`-dispatch molemmille operaatioille.
  TimeNow vaatii 8 tavun `out`-puskurin; Sleep ei hyvaksy puskureita.
- `kernel/core/sysvobj.c` — `vobjtimenow()` lukee `todget(nil,nil)` ja
  `vobjsleep()` kutsuu `kernelsleep()`ia. `vobjprocessselfopen()` sallii nyt
  `Vcontrol|Vmeta`; oletusoikeus sailyy T3:n `Vcontrol`ina.
- `kernel/core/vobjint.h` — providerin julistukset.
- `kernel/core/sysproc.c` — yhteinen `kernelsleep(long)`; `syssleep()` kutsuu
  samaa funktiota ilman semanttista muutosta.
- `kernel/core/portfns.h` — `kernelsleep`-julistus.
- `runtime/libc/port/oobject.c` — `obj_process_self_time()` ja
  `obj_process_self_sleep()` avaavat, kutsuvat ja sulkevat kahvan joka
  pyynnosta. Ei `malloc`ia eika polkua.
- `runtime/libc/9sys/nsec.c` — `nsec()` kayttaa ProcessSelf-OBJ-aikapalvelua.
  `/dev/bintime`, `open`, `pread` ja `fd2path` poistuivat tasta tiedostosta.
- `runtime/libc/port/lock.c` — lukon odotukset kayttavat
  `obj_process_self_sleep()`ia.
- `micropython/p9space.c` — `p9_now()` kutsuu `nsec()`ia ilman suoraa
  `_NSEC`-polkua.
- `micropython/modplan9.c` — `plan9.sleep()` kayttaa
  `obj_process_self_sleep()`ia; testausta varten lisattiin lukevavain
  `plan9.time_ns()`, joka kayttaa samaa palvelua.
- `micropython/main.c` — epaonnistuvan kaynnistyksen odotussilmukat kayttavat
  `obj_process_self_sleep(1000)`ia.

### Toimintasopimus

- Aika on etumerkiton 64-bittinen nanosekuntiluku 8 tavun puskurissa.
- `ObjOpTimeNow` vaatii `Vmeta`-oikeuden kutsujan omaan
  `VkindProcessSelf`-kahvaan; `ObjOpSleep` vaatii `Vcontrol`in.
- Kahva edustaa vain kutsujan omaa prosessia. Silla ei ole `Vdelegate`a eika
  se voi nimetä toista prosessia tai muuta muistia.
- Kahva avataan ja suljetaan joka kutsussa; tilaan ei jaa stale-kahvaa.
- `ms<=0` yieldaa ja positiivinen odottaa. Paluuarvo on 0; virheet nousevat
  normaalin kernelin virhepolun kautta.
- OBJ-polku ei kayta tiedostopolkua, `/dev`-laitetta, fd:ta eika uutta
  syscall-numeroa.

## 3. Mittaus ennen ja jälkeen

Mittaus ajettiin T0:n omalla instrumentoinnilla:

```sh
python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t4a/t0-after.json --summary
```

Kaikki `validation`-kohdat ovat PASS: instrumentoitu boot, instrumentoitu
kahden bootin persistenssi, normaali savutesti, normaali persistenssi ja
normaalin artifactin uudelleenrakennus. Kaikki nelja QEMU-ajoa kayttivat
KVM:aa ja kertakayttoista O-kuvaa.

**Ennen**-mittaus on T3:n julkaistu mittaus samalta pohjalta
(`build/syscall-t3/t0-tool-measurement.json`, 2026-11-05T21:13:24Z). Sita ei
ajettu uudelleen, koska lahtosnapshot on T3:n tulos. **Jalkeen**-mittaus on
`build/syscall-t4a/t0-after.json` (2026-11-06T05:29:13Z).

| kutsu | ennen | jälkeen | ero |
|---|---:|---:|---:|
| `_NSEC(53)` | 25 | **0** | −25 |
| `SLEEP(17)` | 0 | 0 | 0 |
| `BRK_(24)` | 0 | 0 | 0 |
| `OBJOPEN(137)` | 57 | 82 | +25 |
| `OBJINVOKE(140)` | 641 | 666 | +25 |
| `OBJCLOSE(138)` | 47 | 72 | +25 |

Vaiheittain `_NSEC` oli ennen 13 kertaa init/commit-vaiheessa ja 12 kertaa
REPLissa; jälkeen 0 kaikissa vaiheissa. Jokainen poistunut `_NSEC` vastaa
tasmailleen yhtä uutta `OBJOPEN`/`OBJINVOKE`/`OBJCLOSE`-kolmikkoa:

| ajo | `_NSEC` ennen | jälkeen | OBJOPEN Δ | OBJINVOKE Δ | OBJCLOSE Δ |
|---|---:|---:|---:|---:|---:|
| boot | 16 | **0** | +16 | +16 | +16 |
| persist-boot1 | 4 | **0** | +4 | +4 | +4 |
| persist-boot2 | 5 | **0** | +5 | +5 | +5 |
| **yhteensä** | **25** | **0** | **+25** | **+25** | **+25** |

`OSPACEGET`, `OSPACEPUT`, `OSPACECOMMIT`, `OSPACECREATE` ja `BRK_` pysyivat
nollassa normaaleissa MicroPython-vaiheissa. Boot-lataimen `OVOLOPEN` ja
`OSPACEOPEN` maarat eivat muuttuneet (6 + 6).

### Erillinen instrumentoitu sleep-testi

Sleep-polkua varten ajettiin lyhyt instrumentoitu REPL-ajo
(`build/syscall-t4a/inputs/repl-t4a-trace.py`) sallitulla
`tools/qemu-o-boot.sh`:lla. Checkpointit: 3 + 0 + 67 + 1448 rekisteria,
`dropped=0`. Yhteensa 1518 luettua rekisteria:

| kutsu | maara |
|---|---:|
| `_NSEC(53)` | **0** |
| `SLEEP(17)` | **0** |
| `BRK_(24)` | **0** |
| `OBJOPEN(137)` | 89 |
| `OBJINVOKE(140)` | 1338 |
| `OBJCLOSE(138)` | 85 |

Kutsujako REPL-vaiheessa: `obj_process_self_time` 37 x
`OBJOPEN`/`OBJINVOKE`/`OBJCLOSE` ja `obj_process_self_sleep` 3 x samat.
Toiminnallinen aika ja sleep toteutuivat siis OBJ-kutsuina, eivat
`_NSEC`/`SLEEP`-trappeina.

Pitka instrumentoitu ajo (`repl-t4a.py`) katkesi checkpoint 4:n suuren
trace-dumpin aikana harnessin 15 s prompt-odotukseen; lyhyempi ajo valmistui.
Molemmissa `SLEEP=0` ja `_NSEC=0`, eika kernelin trace-puskuri pudottanut
yhtaan rekisteria.

## 4. Testit

| testi | tulos | evidenssi |
|---|---|---|
| kanoninen kernel-build | PASS | `build/syscall-t4a/logs/build-kernel-normal.log` |
| kanoninen MicroPython-build | PASS | `build/syscall-t4a/logs/build-mpy-normal.log`; 246 unit, 0 FAIL, ei undefined-symboleita |
| EFI- ja host-build | PASS | `build/syscall-t4a/logs/build-efi-normal.log`, `build/syscall-t4a/logs/build-host-normal.log` |
| O-boot -> REPL -> `6*7`=42 | PASS | `build/syscall-t4a/boot-final/report.txt` |
| ajanluku positiivinen, 64-bittinen ja kasvava | PASS | `T4A-TIME0 1793943325092995330 1793943325093014789`; `T4A-TIME1 1793943325425101249`; `T4A-TIME-WIDE 417684979` |
| `plan9.sleep(0)` | PASS | kulunut 0 ms; `T4A-SLEEP0-MS 0` |
| `plan9.sleep(300)` | PASS | kulunut 301 ms; `T4A-SLEEP300-MS 301` |
| `plan9.sleep(-1)` yield | PASS | kulunut 0 ms; `T4A-SLEEPNEG-MS 0` |
| kahden bootin GE/O-persistenssi | PASS | `build/syscall-t4a/persist/report.txt`; boot1 v9 -> boot2 restore v9 -> commit v21; `g3`, `t3fun(2)=202`, `t3oid` `T3-HEAP` |
| T0-instrumentointi ennen/jalkeen | PASS | `build/syscall-t3/t0-tool-measurement.json`, `build/syscall-t4a/t0-after.json` |
| erillinen instrumentoitu sleep-testi | PASS | `build/syscall-t4a/sleep-trace/trace-summary.json`; `SLEEP=0`, `_NSEC=0`, `dropped=0` |
| staattinen tarkistus: ei `_nsec`/`sleep`-stubikutsuja | PASS | `build/syscall-t4a/logs/micropython.disasm.txt`; 0 call/jmp kohteisiin `_nsec`, `sleep`, `brk_`; symbolit sailyvat |
| `_NSEC(53)`, `SLEEP(17)`, `BRK_(24)` aktiivina | PASS | `build/gcc/gen/systab-gcc.h` |
| `OSPACE*`/`BRK_` nolla normaalissa ajossa | PASS | `build/syscall-t4a/t0-after.json` |
| `ticks_ms` saman palvelun kautta (staattinen) | PASS | `mp_hal_ticks_ms -> p9_console_msec -> nsec -> obj_process_self_time` |
| `ticks_ms` suora REPL-ajotesti | NOT RUN | O-Environmentissa ei ole importattavaa `time`-moduulia; 64-bittinen aika testattiin `plan9.time_ns`:lla |
| epaonnistuvan kaynnistyksen sleep-silmukat ajossa | NOT RUN | `main.c`:n silmukat on kytketty OBJ-kutsuun staattisesti, ei pakotettu ajoon |
| fyysinen rauta, TCG, laaja regressio | NOT RUN | tehtavan rajauksen ulkopuolella |

## 5. Jäljelle jäävät legacy-kutsujat

- `runtime/libc/9sys/nsec.c` ei enaa viittaa `/dev/bintime`en, `open`iin,
  `pread`iin eika `fd2path`iin.
- `build/gcc/micropython.elf`:ssa ei ole yhtaan `call`- tai tail-`jmp`-
  kohdetta `_nsec`-, `sleep`- eika `brk_`-stubiin. Stubien symbolit ovat
  linkitettyna.
- `_NSEC(53)`, `SLEEP(17)` ja `BRK_(24)` pysyvat aktiivina kernelin
  dispatchissa ja generoidussa `systab-gcc.h`:ssa muita mahdollisia
  legacy-kayttajia varten.
- `plan9.sleep` ja `main.c`:n odotussilmukat kayttavat OBJ-polkua. Myos
  libc:n lukon odotukset siirrettiin samaan polkuun.

## 6. Artefaktit

Normaalit, testatut artifactit:

- `build/gcc/9pc64.elf` — 1 154 376 B, SHA-256
  `48c771becae4fdab4153b33c1df520badad10aface64e4aee7f910501f4f41f8`
- `build/gcc/micropython.elf` — 538 176 B, SHA-256
  `fb2325804bce4817796d46cf41b10d425c1f0d62f5a126bdf5e070eb6fed99a5`
- `build/gcc/gen/obootoid.elf` — 5 456 B, SHA-256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`
- `build/gcc/bootx64.efi` — 22 016 B, SHA-256
  `bee65aa9ff32fc664a5d5b5b442d97e25f81ad2d52fb5734639412d1b177a332`

Mitatut (instrumentoidut) binaryt:

- kernel `f190023c7a87c68a7bd23262d0371a8bfc779bab8a44088d5d1729fcabdbf579`
  (1 154 752 B)
- MicroPython `28c7eda909433e587917266f9e00101772920b9fe0ef6ec99edd7d2d1061f036`
  (538 176 B)
- obootoid `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`

Normaalin kernelin merkkijonoista ei loydy `P2K-SYSCALL`-tekstia.

## 7. Toistettavat komennot

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
tools/build-efi-gcc.sh -j 8
tools/build.sh

tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t4a/boot-final --timeout 300 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t4a/inputs/repl-t4a.py \
  --expect "T4A-TIMETEST OK"

QOB_INI_BOOTEXEC=0x4f45584543000003 tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t4a/persist --timeout 300 --repl \
  --boot1-input build/syscall-t3/inputs/repl-t3-persist-boot1.py \
  --boot2-input build/syscall-t3/inputs/repl-t3-persist-boot2.py \
  --boot1-expect "T3P-BOOT1 OK" --boot2-expect "T3P-BOOT2 OK"

python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t4a/t0-after.json --summary

PLAN2001_SYSCALL_TRACE=1 tools/build-gcc.sh --kernel -j 8
PLAN2001_SYSCALL_TRACE=1 tools/build-mpy-gcc.sh -j 8
tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t4a/sleep-trace --timeout 300 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t4a/inputs/repl-t4a-trace.py \
  --expect "T4A-TRACEOK"

tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
```

## 8. Rajoitteet

- Vain amd64 PoC `kuha`/QEMU-KVM:lla; ei fyysista rautaa eika TCG:ta.
- **Ennen**-mittaus on T3:n julkaistu mittaus samalta pohjalta; uutta
  ennen-ajoa ei tehty.
- `time`-moduulia ei voi importata O-Environmentissa, joten `ticks_ms`:n
  suora REPL-testi on NOT RUN. Ketju `mp_hal_ticks_ms -> p9_console_msec ->
  nsec -> obj_process_self_time` on tarkistettu lahteista.
- `main.c`:n epaonnistuvan kaynnistyksen odotussilmukoita ei pakotettu ajoon.
- `_NSEC(53)`, `SLEEP(17)` ja `BRK_(24)` sailyvat tarkoituksella aktiivina.
