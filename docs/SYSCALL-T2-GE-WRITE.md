# T2 — MicroPythonin pysyvän GE/O-tilan kirjoitus ja commit OBJ-rajapintaan

Tämä on **kirjoitus- ja commit-siirron T2** tulos. Tehtävä ei muuta viiden
OBJ-syscallin numeroita, ei lisää kuudetta syscallia eikä poista vanhoja
numeroita kernelin dispatchistä. Kanoninen työpuu on
`kuha:/home/lapere/Src/Plan2001-O`; pohjasnapshot
`9952e8ef96e9f5a4837cecb386f4ba170184b253bea7fc45970be0b7eb7ece60`.

Koneellinen data on tiedostossa
[SYSCALL-T2-GE-WRITE.json](SYSCALL-T2-GE-WRITE.json). T0:n alkuperäinen
raportti [SYSCALL-T0-MEASUREMENT.md](SYSCALL-T0-MEASUREMENT.md) ja T1:n
raportti [SYSCALL-T1-GE-READ.md](SYSCALL-T1-GE-READ.md) on säilytetty
muuttumattomina. T0-vertailulokien kopio ennen T2-mittausta on
`build/syscall-t2/t0-baseline-before/`.

## 1. Tavoite ja rajaus

MicroPythonin **normaali** REPL- ja pysyvän tilan tallennus ei enää tee
`OSPACEPUT(57)`, `OSPACECREATE(61)` eikä `OSPACECOMMIT(58)` -trappia.
Kirjoitus, luonti ja commit kulkevat olemassa olevan OBJ-rajapinnan kautta:

- nimetyn OID:n korvaus: `OBJINVOKE(ObjOpPutOid=25)` Space-handlella;
- uusi pysyvä OID + sisältö + synkronointi:
  `OBJINVOKE(ObjOpCreatePersist=12)` Space-handlella;
- commit: `OBJINVOKE(ObjOpCommit=13)` Space-handlella.

Kaikki kolme käyttävät T1:ssä avattua auktorisoitua `VkindSpace`-handlea
(`p9space.c`:n `spread`), eivät pelkkää OID:tä. Kernelin entinen GE/O-moottori
(`ge/storage/obj.c`) säilyy sellaisenaan.

T2 **ei** muuta boot-loaderia, prosessi-/muistinhallintaa, verkkopinoa, USB:tä
eikä hakemistopolkuja. Legacy `OSPACEOPEN(55)`/`OVOLOPEN(54)` ja eksplisiittisen
vanhan `Ohand`-parametrin API (`space.hput/hcommit/hcreate`) säilyvät; ne ovat
seuraavan T2b-tehtävän kohteita. T2 ei väitä kaikkia käyttäjätilan kirjoituksia
siirretyiksi.

## 2. Toteutus

Muutetut lähteet:

- `runtime/abi/obj.h` — yksi uusi tyypitetty INVOKE-operaatio
  `ObjOpPutOid=25` (ei syscall-numero), raja `OBJ_PUT_OID_MAX` (64 MiB) ja
  libc-adapterien prototyypit.
- `runtime/libc/port/oobject.c` — `obj_create_persist()`, `obj_put_oid()`,
  `obj_commit()` INVOKE-kääreinä.
- `kernel/core/sysobj.c` — `ObjOpPutOid`-dispatch; validoi pituuden ja
  osoitteen ja välittää OID:n `arg0`:ssa sekä rajatun sisällön `in/inlen`:ssä.
- `kernel/core/sysvobj.c` — `vobjputoid()`: vaatii `Vwrite`-oikeuden
  `VkindSpace`-handlesta ja kutsuu `oswritehand()`-polkua. Ei allokoi eikä
  committaa.
- `kernel/core/vobjint.h` — `vobjputoid()`-julistus.
- `micropython/p9space.c` — `objputoid_space()` / `objcommit_space()` /
  `objcreate_space()` -apurit sekä niiden käyttö funktioissa `ocommit()`,
  `space_oput()`, `space_oregister()` ja `p9_space_create_fun_code()`.
- `micropython/modplan9.c` — `OBJ_OP_PUT_OID`-vakio testikäyttöön.

### Kirjoitussopimus

- `ObjOpPutOid` korvaa yhden **nimetyn** OID:n (OID_GRAPH, OID_ENV,
  ohjelmakoodi tai REPL-funktion koodi) myös ensimmäisessä bootissa, vaikka
  OID:tä ei olisi vielä julkaistu. Tämä vastaa entistä `OSPACEPUT`-polkua.
- Operaatio **ei committaa**. Saman commitin kaikki kirjoitukset jäävät saman
  Space-transaktion sisään ja julkaistaan yhdellä `ObjOpCommit`-kutsulla.
  Tämä on välttämätöntä, koska jokaisella `OBJOPEN`-kutsulla avattu GE-kahva
  avaa oman `Ospace`-transaktionsa; siksi kirjoitus ja commit tehdään samalla
  T1:n Space-handlella.
- Pituusrajat: `ObjOpPutOid` sallii 64 MiB (sama kuin `OSPACEPUT` ja
  `Objtxmax`), jotta koko OID_GRAPH säilyttää entisen maksimikokonsa.
  `ObjOpCreatePersist` säilyttää 1 MiB suoran Python-tavuobjektin PoC-rajan.
- `ObjOpCreatePersist` on edelleen synkronoiva luontitoiminto; sitä ei korvattu
  tavallisella OID_GRAPH-kirjoituksella eikä piilocommitteja lisätty.
- Virhe ei muutu näennäiseksi onnistumiseksi: jokainen uusi apuri kutsuu
  `raise_err()`-polkua, ja Space-kahva suljetaan edelleen `ospace_open`:n
  virhepolussa. Tallennusformaattiin ei koskettu.

### Ensimmäisen bootin ja ennalta määrätyn OID:n semantiikka

`ObjOpPutOid` käyttää `oswritehand()` → `onativeput()` → `ospaceput()`-polkua.
Se hyväksyy eksplisiittisen OID:n, mukaan lukien vielä julkaisemattoman
OID_GRAPH:n ensimmäisessä bootissa. `ObjOpCreatePersist` käyttää
`oscreatehand()` → `onativecreate()` → `ospacecreate()`-polkua, joka allokoi
ainutkertaisen OID:n ja synkronoi vanhalla atomisella semantiikalla.

## 3. Jäljelle jäävät legacy-kutsujat

Normaalin `micropython.elf`:n staattinen analyysi (objdump, suorat
`call`-kohteet):

| symboli | nr | kutsupaikat |
|---|---:|---|
| `ospaceput` | 57 | `space_hput` |
| `ospacecreate` | 61 | `space_hcreate` |
| `ospacecommit` | 58 | `space_hcommit` |
| `ospaceopen` | 55 | `space_open_handle`, `ospace_open.isra.0` |
| `ovolopen` | 54 | `p9_space_o` |
| `ospaceget` | 56 | ei yhtään |

Eli `OSPACE*`-kirjoitus/luonti/commit-stubit ovat jäljellä vain eksplisiittisen
`space.h*`-API:n takia (T2b). Automaattinen `space.oput`/`oregister` ja
`ocommit` eivät enää kutsu niitä. `obootoid.elf`:n omat trapit ovat
boot-lataimen T10-työtä eivätkä kuulu T2:een; mittauksessa boot-loader-vaiheen
`OSPACEOPEN` on 1 eikä `OSPACEGET/PUT/COMMIT/CREATE` esiinny.

## 4. Mittaus: T0 vs T2

Mittaus ajettiin T0:n omalla instrumentoinnilla ja työkalulla:

```sh
python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t2/t0-tool-measurement.json --summary
```

Kaikki `validation`-kohdat ovat PASS. Taulukko on identtisellä
`repl-t0.py`-syötteellä, joten commit-määrät ovat suoraan vertailukelpoisia.

| ajo / vaihe | OSPACEPUT T0→T2 | OSPACECOMMIT T0→T2 | OSPACECREATE T0→T2 | OSPACEGET T0→T2 | obj_put_oid / obj_commit T2 |
|---|---:|---:|---:|---:|---:|
| boot / init-and-commit | 3 → **0** | 1 → **0** | 0 → **0** | 9 → **0** | 3 / 1 |
| boot / REPL | 18 → **0** | 6 → **0** | 0 → **0** | 24 → **0** | 18 / 6 |
| persist-boot1 / init-and-commit | 3 → **0** | 1 → **0** | 0 → **0** | 9 → **0** | 3 / 1 |
| persist-boot2 / init-and-commit | 3 → **0** | 1 → **0** | 0 → **0** | 10 → **0** | 3 / 1 |
| boot-loader (kaikki ajot) | 0 → 0 | 0 → 0 | 0 → 0 | 0 → 0 | 0 / 0 |
| **yhteensä** | **27 → 0** | **9 → 0** | **0 → 0** | **52 → 0** | **27 / 9** |

Commit-operaatioiden määrä ei kasvanut: T0:n `OSPACECOMMIT` 1+6+1+1 = 9
vastaa täsmälleen T2:n `obj_commit` 1+6+1+1 = 9. Samoin kirjoitusten määrä
27 vastaa täsmälleen `obj_put_oid`-kutsuja 27. Uusi `ObjOpPutOid` ei siis lisää
committeja eikä kirjoituksia.

Normaalin kahden bootin persistenssiajon versiosemantiikka säilyi:
`build/syscall-t0/normal-persist/report.txt` raportoi
`boot1_final_commit_version=1`, `boot2_restore_version=1`,
`boot2_final_commit_version=2` ja `boot2_restored_version_matches=PASS`.

## 5. Testit

Kaikki QEMU-ajot käyttivät vain kahta sallittua työkalua, tuoretta
kertakäyttöistä O-kuvaa ja KVM:ää.

| testi | tulos | evidenssi |
|---|---|---|
| kanoninen kernel-build | PASS | `build/syscall-t2/logs/build-kernel.log` |
| kanoninen MicroPython-build | PASS | 246 unit, 0 FAIL, ei undefined-symboleita; `build/syscall-t2/logs/build-mpy.log` |
| EFI- ja host-build | PASS | `build/syscall-t2/logs/build-efi.log`, `build-efi-gcc: PASS`; `build/syscall-t2/logs/build-host.log` |
| O-boot -> REPL -> `6*7`=42 + `T0-OBJ-OK` | PASS | `build/syscall-t2/instrumented/normal-smoke/serial-boot.log`, `report.txt` |
| init -> commit -> REPL (normaali) | PASS | `build/syscall-t2/instrumented/normal-smoke/report.txt` |
| kahden bootin persistence/restore (norepl) | PASS | `build/syscall-t2/instrumented/normal-persist/report.txt`; versio 1 -> restore 1 -> commit 2, Counter n 1 -> n 2 |
| REPL:n kahden bootin persistence | PASS | `build/syscall-t2/persist-repl-final/report.txt`; `g1`, `t2fun(2)=102`, `t2oid=65` sisältö `T2-REPLACED`, `t2eoid=66` tyhjä; restore 33 -> commit 49 |
| persistentin byte-objektin luonti + OID + tavut | PASS | `T2-BOOT1-OID 65`, `T2-BOOT2-PERSIST 65 66` |
| `space.oput`-korvaus + uusi luku + uudelleenkäynnistys | PASS | `T2-BOOT1-OPUT 65`, boot2 `t2oid` sisältö säilyi |
| tyhjä payload (`space.oput(oid, b"")`) | PASS | `T2-BOOT1-EMPTY 66`; boot2 `space.oget(t2eoid) == b""` |
| virheellinen OID (`space.oput(1, ...)`) | PASS | `ValueError: space.oput: OID is reserved or is REPL function code` |
| puuttuva OID luku (`space.oget(0x12345678)`) | PASS | `OSError: space.oget: no such O object` |
| oikeuksien esto uudella operaatiolla | PASS | read-only Space-handle + `obj_invoke(..., OBJ_OP_PUT_OID, oid)` -> `OSError: permission denied` |
| T0-instrumentointimittaus uudella koodilla | PASS | `build/syscall-t2/t0-tool-measurement.json`; kaikki `validation` PASS, `OSPACE*`-kirjoitukset 0 |
| staattinen kutsupaikka-analyysi | PASS | ks. luku 3; `objdump` suorat `call`-kohteet |
| >1 MiB suora Python-tavuobjekti | NOT RUN | 1 MiB PoC-raja säilytetty; ylitys antaa virheen |
| fyysinen rauta, TCG, laaja regressio | NOT RUN | tehtävän rajauksen ulkopuolella |
| `space.h*`-API:n siirto | NOT RUN | T2b-kohde; legacy-stubit jäljellä |

REPL-syötteet ovat `build/syscall-t2/inputs/repl-t2-boot1.py` ja
`repl-t2-boot2.py`. Boot1:n viimeisen rivin PS/2-injektiossa havaittiin
ajoittainen merkkihäiriö (viimeinen `print`-rivi vääntyi), mutta kaikki
väitteet ja commitit oli jo suoritettu ja testi raportoi PASS; tämä on
injektorin artefakti, ei guest-koodin virhe.

## 6. Artefaktit

Normaalit, testatut artifactit:

- `build/gcc/9pc64.elf` — 1 154 192 B, SHA-256
  `484c2f1e8749f82805d5630c689f757623048c9481e49aabc64621f9a33bb23f`
- `build/gcc/micropython.elf` — 538 168 B, SHA-256
  `1f06da54f713f36de88dae50266241cc91e9fd9f132d6a0d98d4bea6e98b25e6`
- `build/gcc/gen/obootoid.elf` — 5 456 B, SHA-256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`
- `build/gcc/bootx64.efi` — 22 016 B, SHA-256
  `6a33df3a8386f17e3bc70d0d94295ae71629f8607d53d10f29bf34caa2dddc1e`

Mitatut (instrumentoidut) binaryt:

- kernel `c8662c591e5304f981be418f22e8d7b385b3f89ad787b9728ec7dace170608c9`
  (1 154 568 B)
- MicroPython `fb1e1dd4f0b6652c9f8df6ffb187492f5f0564c5e113def52e898fdf343f8ee1`
  (538 168 B)
- obootoid `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`

Normaalin kernelin merkkijonoista ei löydy `P2K-SYSCALL`-tekstiä.

## 7. Toistettavat komennot

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-mpy-gcc.sh -j 8
tools/build-efi-gcc.sh -j 8
tools/build.sh

# yksi boot + T2-REPL-testi
tools/qemu-o-boot.sh --no-build \
  --output-dir build/syscall-t2/boot --timeout 240 \
  --obootexec 0x4f45584543000003 \
  --repl-input build/syscall-t2/inputs/repl-t2-boot1.py \
  --expect "T2-BOOT1 OK"

# kaksi boottia samalla kertakäyttöisellä levyllä
QOB_INI_BOOTEXEC=0x4f45584543000003 tools/qemu-o-persist.sh --no-build \
  --output-dir build/syscall-t2/persist-repl-final --timeout 300 --repl \
  --boot1-input build/syscall-t2/inputs/repl-t2-boot1.py \
  --boot2-input build/syscall-t2/inputs/repl-t2-boot2.py \
  --boot1-expect "T2-BOOT1 OK" --boot2-expect "T2-BOOT2 OK"

python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t2/t0-tool-measurement.json --summary
```

## 8. Rajoitukset ja seuraava vaihe

- `space.hput/hcommit/hcreate` ja `space.open_handle` käyttävät edelleen
  vanhoja syscalleja; niiden siirto on T2b. Kaikki käyttäjätilan kirjoitukset
  eivät siis ole vielä OBJ-radalla.
- `OSPACEOPEN(55)`/`OVOLOPEN(54)` ja boot-lataimen omat trapit säilyvät;
  OSPAWN/OEXECUTE/namespace/boot-loader ovat tulevia tehtäviä.
- Mittaus ja testit koskevat vain tätä amd64/PoC/QEMU-KVM-konfiguraatiota.
  Fyysinen rauta, TCG ja laaja regressio ovat NOT RUN.
- Tämä raportti ei väitä T2b:tä, T10:tä eikä koko viiden kutsun
  kokonaistavoitetta valmiiksi, vaikka viiden OBJ-kutsun GE/O-kirjoitus- ja
  commit-polku on nyt normaalilla MicroPython-reitillä katettu.
