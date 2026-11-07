# T5a — MicroPythonin NOTIFY-rekisteröinti OBJINVOKE-rajapintaan

> **Historical report (T0–T6 phase).** Current direct-syscall state: [SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md).

Tämä on **T5a:n** tulos. Tehtävä siirtää MicroPythonin `NOTIFY(28)`-note-
callbackin rekisteröinnin yhteen tyypitettyyn `OBJINVOKE`-operaatioon
(`ObjOpNotifyRegister=30`, `handle=ProcessSelf` + `Vcontrol`). Uutta
syscall-numeroa ei otettu käyttöön. Vanha `NOTIFY(28)` jää aktiiviksi
kernelin dispatch-taulussa muita legacy-kutsujia varten. `NOTED(29)`,
`ALARM(17)` ja note-ABI säilyvät muuttumattomina.

Kanoninen työpuu on `kuha:/home/lapere/Src/Plan2001-O`. Tehtävän
lähtösnapshot on
`86795d7c572b3359d892136681bbe6e9858fb0f5fea96faccac505a09996a5c7`.
Snapshotissa olivat jo T5a:n ydinlähdemuutokset edellisestä keskeneräisestä
kierroksesta; tällä kierroksella ne verifioitiin, testattiin ja
dokumentoitiin sekä täydennettiin testidiagnostiikkaa. Koneellinen data on
tiedostossa [SYSCALL-T5A-NOTIFY.json](SYSCALL-T5A-NOTIFY.json). T0–T4b:n
raportteja ei muutettu.

## 1. Tavoite ja rajaus

MicroPythonin käynnistys ei enää tee `NOTIFY(28)`-trappia. Polku on:

- `micropython/main.c` kutsuu edelleen `atnotify(onnote, 1)`;
- `runtime/libc/port/atnotify.c` kutsuu edelleen `notify(notifier)`;
- `notify` on nyt `runtime/libc/9sys/notify.c`:n oikea libc-funktio, joka
  avaa kutsujan oman `ObjOpenProcessSelf`-kahvan (`Vcontrol`), kutsuu
  `OBJINVOKE(ObjOpNotifyRegister=30)`ia ja sulkee kahvan;
- kernelin `kernel/core/sysobj.c` validoi operaation ja kutsuu
  `vobjnotifyregister()`ia;
- `vobjnotifyregister()` tarkistaa `VkindProcessSelf`-kahvan ja
  `Vcontrol`-oikeuden ja kutsuu jaettua `setnotify()`-apufunktiota;
- `setnotify()` on sama validointi ja `up->notify`-päivitys kuin vanhassa
  `sysnotify()`ssa: `nil` poistaa, muuten osoitteen on oltava luettavaa
  käyttäjämuistia.

T5a ei muuta `NOTED(29)`-paluupolkua, `ALARM`-syscallia, note-jonoa,
`ureg`-käsittelyä, prosessien luontia/execia/exitia, GE/O-formaattia,
tallennusta, polku-/FD-abstraktiota eikä muita syscalleja. Muiden
käyttäjätilojen ABI-yhteensopivuutta ei testattu eikä väitetä.

## 2. Toteutus

### 2.1 Snapshotissa jo olleet T5a-lähdemuutokset

- `runtime/abi/obj.h` — `ObjOpNotifyRegister=30` ja tyypitetty sopimus.
  Ei uutta syscall-numeroa.
- `kernel/core/sysobj.c` — `OBJINVOKE`-dispatch: `handle` on ProcessSelf,
  `in/out` ja niiden pituudet nollia, `arg1==0`; kutsuu
  `vobjnotifyregister(handle, arg0)`.
- `kernel/core/sysproc.c` — `setnotify()` erotettu yhteiseksi
  `sysnotify()`n kanssa; `generrstr()` ja `objoptrace()` säilyvät.
- `kernel/core/portfns.h` — `setnotify()`-julistus.
- `kernel/core/sysvobj.c`, `kernel/core/vobjint.h` —
  `vobjnotifyregister()`: `vobjholdobj(handle, Vcontrol)`,
  `VkindProcessSelf`-tarkistus, `setnotify()`, `vobjput()`.
- `runtime/libc/9sys/notify.c` — **uusi** ei-syscallinen
  `notify(void (*)(void*,char*))` yli `OBJINVOKE`n.
- `tools/host/build-mpy-gcc.py` — `gen_stubs()` jättää generoimatta vain
  `notify`-symbolin vanhan `NOTIFY(28)`-stubin ja lisää
  `9sys/notify.c`n `LIBC_SOURCES`-luetteloon.
- `micropython/modplan9.c` — testikohtainen `notify_probe_addr()` ja
  `OBJ_OP_NOTIFY_REGISTER`-vakio.

`runtime/libc/port/atnotify.c` ja `micropython/main.c` pysyvät muuttumattomina.

### 2.2 Tällä kierroksella lisätty testidiagnostiikka

- `micropython/modplan9.c` — `notify_probe_count()`,
  `notify_probe_reset()`, `notify_probe_lastnote()` ja
  `notify_probe_framevals()`; `sysraw()` hyväksyy nyt valinnaiset
  argumentit, jotta testi voi tehdä aidon virheellisen käyttäjäosoitteen
  syscallin ja saada oikean kernelin noten. Diagnostiikka ei lisää
  kernelin entry pointia eikä muuta normaalia callbackia.
- `kernel/amd64/ouser.c` — `#ifdef PLAN2001_ALARM_NOTETEST` käynnistää
  olemassa olevan `alarmkproc()`in testibuildissa. Normaali build ei
  määritä makroa; `alarm.c` ja `ALARM`-syscall ovat muuttumattomia.
- `tools/host/build-kernel.py` — hyväksyy
  `PLAN2001_ALARM_NOTETEST=1`-ympäristömuuttujan testibuildia varten.

### 2.3 Toimintasopimus

- `handle` on kutsujan oma `VkindProcessSelf`-kahva, jolla on `Vcontrol`.
  `handle==0` ei kelpaa, koska operaatio tarvitsee provider-rajan.
- `arg0` on käyttäjän callback-osoite tai `0`. Nollasta poikkeava osoite
  validoidaan `setnotify()`ssa kuten `NOTIFY(28)`ssa.
- `in==NULL`, `inlen==0`, `out==NULL`, `outlen==0`, `arg1==0`.
- Kernel asettaa vain `up->notify`n. Note-jonoa, `ureg`ia ja `NOTED`-
  paluupolkua ei kosketa.
- Paluuarvo on `0`. `nil` poistaa callbackin. Vanha `NOTIFY(28)` toimii
  edelleen ja johtaa samaan `setnotify()`-tilaan.

## 3. Mittaus ennen ja jälkeen

Mittaus ajettiin T0:n omalla instrumentoinnilla:

```sh
python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t5a/t0-after.json --summary
```

Kaikki `validation`-kohdat ovat PASS: instrumentoitu boot, instrumentoitu
kahden bootin persistenssi, normaali savutesti, normaali persistenssi ja
normaalin artifactin uudelleenrakennus. Kaikki QEMU-ajot käyttivät KVM:ää.

**Ennen**-mittaus on T4b:n julkaistu mittaus
(`build/syscall-t4b/t0-after.json`). **Jälkeen**-mittaus on
`build/syscall-t5a/t0-after.json`.

| kutsu | ennen (T4b) | jälkeen (T5a) | ero |
|---|---:|---:|---:|
| `NOTIFY(28)` | 3 | **0** | −3 |
| `NOTED(29)` | 0 | 0 | 0 |
| `OBJOPEN(137)` | 82 | 85 | +3 |
| `OBJCLOSE(138)` | 72 | 75 | +3 |
| `OBJINVOKE(140)` | 666 | 669 | +3 |
| `_NSEC(53)`, `SLEEP(17)`, `BRK_(24)`, `OSPACE*(56–61)`, `ERRSTR(41)`, `_ERRSTR(1)` | 0 | 0 | 0 |

Ero on täsmälleen yksi `OBJOPEN` + `OBJINVOKE` + `OBJCLOSE` per boot:
käynnistyksen `atnotify()` rekisteröi nyt callbackin OBJ-polulla eikä
`NOTIFY(28)`-trapilla. `NOTED(29)` pysyy legacy-kutsuna eikä näy
normaalissa käynnistyksessä.

## 4. Erillinen instrumentoitu note-testi

Instrumentoitu kernel ja MicroPython rakennettiin lipuilla
`PLAN2001_SYSCALL_TRACE=1 PLAN2001_ALARM_NOTETEST=1`. REPL-ajo
(`build/syscall-t5a/inputs/repl-t5a-trace2.py`) rekisteröi testicallbackin
uudella operaatiolla, otti `alarm`-noten ja palasi `NCONT`-polulla.
Checkpointit:

| checkpoint | label | records | dropped |
|---|---|---:|---:|
| 1 | boot-loader | 3 | 0 |
| 2 | snapshot | 0 | 0 |
| 3 | snapshot | 69 | 0 |
| 4 | snapshot | 2332 | 0 |

Checkpoint 3 (käynnistys, ennen REPL-komentoja) osoittaa, että
`ObjOpNotifyRegister` suoritettiin kerran eikä `NOTIFY(28)`a kutsuttu:

| mittari | arvo |
|---|---:|
| `ObjOpNotifyRegister(30)` | **1** |
| `NOTIFY(28)`-syscallit | **0** |
| `NOTED(29)`-syscallit | 0 |
| dropped | 0 |

Checkpoint 4 (REPL-testin jälkeen) osoittaa sekä käsin tehdyn
rekisteröinnin että alarm-noten toimituksen:

| mittari | arvo |
|---|---:|
| `ObjOpNotifyRegister(30)` | **1** |
| `ObjOpSleep(28)` | 1 |
| `NOTED(29)` | **1** |
| `NOTIFY(28)`-syscallit | **0** |
| dropped | 0 |

`NOTED(29)` saa näkyä note-tilanteen jäljessä; T5a ei väitä `NOTED=0`
yleisesti. Koko ajon `NOTIFY(28)`-määrä on 0.

## 5. Testit

| testi | tulos | evidenssi |
|---|---|---|
| kanoninen kernel-build | PASS | `build/syscall-t5a/logs/build-kernel-normal-final.log` |
| kanoninen MicroPython-build | PASS | `build/syscall-t5a/logs/build-mpy-normal-final.log`; 248 unit, 0 FAIL, ei undefined-symboleita |
| EFI- ja host-build | PASS | `build/syscall-t5a/logs/build-efi-normal-final.log`, `build-host-normal-final.log` |
| O-boot -> REPL -> `6*7`=42 | PASS | `build/syscall-t5a/boot-canonical-final/report.txt`; `T5A-MATH 42` |
| kahden bootin GE/O-persistenssi | PASS | `build/syscall-t5a/persist-final/report.txt`; `T3P-BOOT1 OK`, `T3P-BOOT2 OK`, `t3fun(2)=202` |
| T1–T4b:n vanhat kutsut 0:ssa | PASS | `build/syscall-t5a/t0-after.json` |
| T0-instrumentointi ennen/jälkeen | PASS | `build/syscall-t4b/t0-after.json`, `build/syscall-t5a/t0-after.json` |
| rekisteröinti uudella operaatiolla | PASS | `T5A-REG-SET 0`, `T5A-REG-SET2 0` |
| poisto ja `nil` | PASS | `T5A-REG-CLEAR 0`, `T5A-REG-CLEAR2 0` |
| osoitteen validointi | PASS | `T5A-REG-BADADDR bad arg in system call` |
| oikeuspuute | PASS | `T5A-REG-NORIGHT permission denied` |
| väärä handle | PASS | `T5A-REG-BADHANDLE bad arg in system call` |
| väärä op | PASS | `T5A-REG-BADOP obj: operation not supported` |
| vanha `NOTIFY(28)` johtaa samaan tilaan | PASS | `T5A-REG-LEGACY-SET 0`, `T5A-REG-LEGACY-NOTE bad arg in system call`, `T5A-REG-LEGACY-COUNT 1`, `T5A-REG-LEGACY-LAST sys: bad address in syscall`, `T5A-REG-LEGACY-CLEAR 0` |
| uusi operaatio toimittaa noten | PASS | `T5A-REG-SYSNOTE bad arg in system call`, `T5A-REG-COUNT 2`, `T5A-REG-LAST sys: bad address in syscall` |
| alarm-noten todellinen toimitus + NCONT | PASS | `build/syscall-t5a/boot-alarm-final/boot-boot.log`: `T5A-ALARM-EXC interrupted`, `T5A-ALARM-COUNT 1`, `T5A-ALARM-LAST alarm`, `T5A-ALARM-ALIVE`, `T5A-ALARM-DONE` |
| REPL elossa ja tallennus noten jälkeen | PASS | `T5A-ALARM-DONE`; `plan9.system()` ja `space: O: committed ...` samassa ajossa |
| erillinen instrumentoitu note-testi | PASS | `build/syscall-t5a/trace4/report.txt`, `build/syscall-t5a/trace4/boot-boot.log`; `ObjOpNotifyRegister=1+1`, `NOTED=1`, `NOTIFY=0`, `dropped=0` |
| staattinen: `notify` ei käytä legacy-stubia | PASS | `build/syscall-t5a/logs/micropython.disasm.txt`; `notify` 0x253030 kutsuu `obj_open`/`obj_invoke`/`obj_close`, ei `rbp=28`-stubia |
| `NOTIFY(28)` ja `NOTED(29)` aktiivina kernelissä | PASS | `build/gcc/gen/systab-gcc.h`; `[NOTIFY] sysnotify`, `[NOTED] sysnoted` |
| MicroPythonin oma `onnote`-argumenttien käsittely | NOT RUN | pre-existing GCC/Plan9-note-argumentti-ABI; ks. rajoitukset |
| `interrupt`/`KeyboardInterrupt` | NOT RUN | ei interrupt-noten lähdettä tässä QEMU-ympäristössä |
| muiden käyttäjätilojen ABI-yhteensopivuus | NOT RUN | tehtävän rajaus |
| fyysinen rauta, TCG, laaja regressio | NOT RUN | tehtävän rajaus |

### 5.1 Rekisteröintitestin havainnot

```
T5A-REG-SET 0
T5A-REG-CLEAR 0
T5A-REG-SET2 0
T5A-REG-BADADDR bad arg in system call
T5A-REG-NORIGHT permission denied
T5A-REG-BADHANDLE bad arg in system call
T5A-REG-BADOP obj: operation not supported
T5A-REG-SYSNOTE bad arg in system call
T5A-REG-COUNT 2
T5A-REG-LAST sys: bad address in syscall pc=0x24aef1
T5A-REG-LEGACY-SET 0
T5A-REG-LEGACY-NOTE bad arg in system call
T5A-REG-LEGACY-COUNT 1
T5A-REG-LEGACY-LAST sys: bad address in syscall pc=0x24aef1
T5A-REG-LEGACY-CLEAR 0
T5A-REG-CLEAR2 0
T5A-REG-ALIVE
```

`COUNT 2` tulee kahdesta aidosta kernelin notesta: virheellinen
rekisteröintiosoite ja erikseen laukaistu virheellinen syscall-osoite.
`LEGACY-COUNT 1` osoittaa, että vanha `NOTIFY(28)`-rekisteröinti tuottaa
saman toimituksen. Testicallback lukee noten viestin kernelin Plan 9
-stack-kehyksestä ja kutsuu `noted(NCONT)`, joten prosessi jatkaa eikä
kaadu.

### 5.2 Alarm-testin havainnot

```
T5A-ALARM-REG 0
T5A-ALARM-EXC interrupted
T5A-ALARM-MS 8
T5A-ALARM-COUNT 1
T5A-ALARM-LAST alarm
T5A-ALARM-ALIVE
T5A-ALARM-DONE
```

Alarm-note toimitettiin käyttäjätilaan, callback kutsuttiin, `noted(NCONT)`
palautti unen ja REPL sekä GE/O-commit jatkoivat. `alarm(200)` keskeytyi
noin 8 ms:ssa; syy on olemassa oleva tick-/HZ-muunnoksen epätarkkuus, ei
T5a:n muutos. T5a ei muuta `ALARM`-ajoitusta eikä korjaa sitä.

## 6. Staattinen linkitys

`build/gcc/micropython.elf`:

- `notify` on libc-funktio `0x253030`; se avaa providerin 14
  (`ObjOpenProcessSelf`) oikeuksin `0x40` (`Vcontrol`), kutsuu
  `obj_invoke`ia ja sulkee kahvan. Se ei tee `NOTIFY(28)`-trappia.
- `atnotify`-funktiolla on täsmälleen kaksi `call <notify>`-kohtaa:
  `0x24e8b0` (rekisteröinti) ja `0x24e8c5` (poisto).
- Disassembloinnissa ei ole `rbp=28`-stubia eikä `notified`-symbolia.
- `noted`-symboli on `0x2499e0` ja `notifier` `0x24e740`; ne säilyvät
  legacy-`NOTED`-kutsua varten.
- `build/gcc/gen/systab-gcc.h` sisältää edelleen
  `[NOTIFY] sysnotify` ja `[NOTED] sysnoted`.

## 7. Artefaktit

Normaalit, testatut artifactit:

- `build/gcc/9pc64.elf` — 1 154 448 B, SHA-256
  `7e26603e22b11adac2182cfd2f9ff6af7cec25c0b0521f451c4613c0b44b5b0d`
- `build/gcc/micropython.elf` — 543 224 B, SHA-256
  `875110dd2a13482c461f920d5d24b4b4281849607d331a12db3e78601e47c269`
- `build/gcc/gen/obootoid.elf` — 5 456 B, SHA-256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`
- `build/gcc/bootx64.efi` — 22 016 B, SHA-256
  `05e82ad73636a45e620cfa7d3fe5d89523da9ddebaa878476d60403a688f0ada`

Mitatut testibinaryt:

- trace+alarm-testi: kernel
  `39e7f6ff3e44a5fb2e1cc0de8d1bdbe8f8e81454d99caf2cfd929e9de29be47b`;
  MicroPython `858354737d53d0016840c0f5d9c1bc671dde122273ca931dc1fdd9ce6899f058`.
- alarm-testi ilman tracea: kernel
  `d3937705d23d14e48ee3c99156bc60837194edbaceea6d1b4cbfd5f404c37454`;
  MicroPython `875110dd2a13482c461f920d5d24b4b4281849607d331a12db3e78601e47c269`.
- T0-mittarin instrumentoitu MicroPython vastaa `858354…`-SHA:ta.

Normaalin kernelin merkkijonoista ei löydy `P2K-SYSCALL`-tekstiä. Kernelin
SHA vaihtelee build-aikaleiman mukana; lähteet ja MicroPython-SHA ovat
deterministisiä.

## 8. Toistettavat komennot

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
tools/build-efi-gcc.sh -j 8
tools/build.sh

bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5a/boot-canonical-final --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5a/inputs/repl-t5a-smoke.py \
  --expect "T5A-MATH 42"

QOB_INI_BOOTEXEC=0x4f45584543000003 bash tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t5a/persist-final --timeout 360 --repl \
  --boot1-input build/syscall-t3/inputs/repl-t3-persist-boot1.py \
  --boot2-input build/syscall-t3/inputs/repl-t3-persist-boot2.py \
  --boot1-expect "T3P-BOOT1 OK" --boot2-expect "T3P-BOOT2 OK"

python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t5a/t0-after.json --summary

bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5a/boot-reg-final2 --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5a/inputs/repl-t5a-reg.py \
  --expect "T5A-REG-DONE"

PLAN2001_ALARM_NOTETEST=1 tools/build-gcc.sh --kernel -j 8
bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5a/boot-alarm-final --timeout 360 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5a/inputs/repl-t5a-alarm.py \
  --expect "T5A-ALARM-DONE"

PLAN2001_SYSCALL_TRACE=1 PLAN2001_ALARM_NOTETEST=1 tools/build-gcc.sh --kernel -j 8
PLAN2001_SYSCALL_TRACE=1 tools/build-mpy-gcc.sh -j 8
bash tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t5a/trace4 --timeout 600 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t5a/inputs/repl-t5a-trace2.py \
  --expect "T5A-TRACE-DONE"

tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
```

## 9. Rajoitukset ja jäljelle jäävät legacy-kutsujat

- Vain amd64 PoC `kuha`/QEMU-KVM:lla; ei fyysistä rautaa eikä TCG:tä.
- `NOTIFY(28)` ja `NOTED(29)` pysyvät tarkoituksella aktiivina kernelin
  dispatch-taulussa. T5a ei väitä `NOTED=0` yleisesti.
- Normaali kernel ei käynnistä `alarmkproc()`ia, joten alarm-noteen
  perustuva testi vaatii `PLAN2001_ALARM_NOTETEST=1`-testibuildin. Makro
  ei muuta `alarm.c`:tä eikä `ALARM`-syscallia; normaali build ei määritä
  sitä. Tämä on testin käynnistysapu, ei ALARM-toiminnon uudelleen-
  suunnittelu.
- Olemassa oleva kernelin note-käsittelijän siirtymä välittää argumentit
  Plan 9 -stack-kehyksessä, kun taas GCC-käyttäjätila odottaa SysV-
  rekistereitä. Testicallback lukee siksi viestin kehyksestä. Tätä
  note-argumentti-ABI:a ei muutettu T5a:ssa, koska tehtävä kieltää
  kernelin note-ABI:n uudelleensuunnittelun. MicroPythonin oman
  `onnote`-callbackin argumenttien käsittely ja `interrupt`/
  `KeyboardInterrupt`-polku ovat siksi **NOT RUN** eikä niitä väitetä
  testatuiksi.
- `alarm(200)` keskeytyi noin 8 ms:ssa olemassa olevan tick-/HZ-
  muunnoksen vuoksi. T5a ei muuta eikä korjaa ALARM-ajoitusta.
- Muiden käyttäjätilojen ABI-yhteensopivuutta ei testattu eikä väitetä.
- Kernelin SHA vaihtelee build-aikaleiman mukana; lähteet ja
  MicroPython-SHA ovat deterministisiä.
