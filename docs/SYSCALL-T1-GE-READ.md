# T1 — MicroPythonin GE/O-lukupolut OBJ-rajapintaan

Tämä on **lukupolun siirtotehtävän T1** tulos. Tehtävä ei muuta syscall-ABI:n
viittä pääkutsua, ei lisää eikä numeroi uudelleen systeemikutsuja eikä tee
T2:n kirjoitus/commit-työtä tai T10:n boot-lataimen remonttia. Lähtöpiste on
T0:n jälkeinen julkaistu snapshot
`e47221317584895ea39e4bfefbab89b4711a6031ecba5898104e482b69112dc4`.
Kanoninen työpuu on `kuha:/home/lapere/Src/Plan2001-O`.

Koneellinen data on tiedostossa
[SYSCALL-T1-GE-READ.json](SYSCALL-T1-GE-READ.json). T0:n alkuperäinen
mittausraportti [SYSCALL-T0-MEASUREMENT.md](SYSCALL-T0-MEASUREMENT.md) ja
`.json` on säilytetty muuttumattomina; T0:n lähdelokien kopio on
`build/syscall-t1/t0-baseline/`.

## 1. Tavoite ja rajaus

Käynnistyvä MicroPython ja sen `micropython/p9space.c`:n GE/O-lukutoiminnot
eivät enää tee `OSPACEGET(56)`-trappia. Ne käyttävät kanonista rajaa:

- valtuutettu Space: `OBJOPEN(ObjOpenSpace)`;
- yksi OID: `OBJOPEN(ObjOpenGeSpace)`;
- sisältö: `OBJINVOKE(ObjOpRead)`;
- kahvan vapautus: `OBJCLOSE`.

T1 koskee vain lukemista. Vanhat `OSPACEPUT(57)`, `OSPACECOMMIT(58)`,
`OSPACEOPEN(55)`, `OVOLOPEN(54)`, `OSPACECREATE(61)`, `OEXECUTE(60)` ja
`OSPAWN(92)` kutsut ja niiden käyttäjät säilyvät. `OSPACEGET(56)` pysyy
aktiivisena ja dispatcherissä, koska `kernel/amd64/obootoid.c`:n virhepolku
käyttää sitä; sen siirto on T10:ssä.

## 2. Toteutus

Muutetut lähteet:

- `micropython/p9space.c` — `oreadobj`, `space.oget` ja `space.hget`
  käyttävät OBJ-polkua. `ospace_open` avaa lisäksi lukukahvan
  `OBJOPEN(ObjOpenSpace)`-kutsulla ja pitää vanhan `osp`-kahvan
  kirjoitukselle/commitille. Jokainen OID avataan ja suljetaan lukua varten.
- `micropython/modplan9.c` — `plan9.obj_read_version(h, n, timeout)` palauttaa
  `(bytes, version)`; se on ohut käyttäjätilan adapteri libc-kääreen päällä.
- `runtime/abi/obj.h`, `runtime/libc/port/oobject.c` — uusi tyypitetty
  `obj_read_version()`.
- `kernel/core/sysobj.c`, `kernel/core/sysvobj.c`,
  `kernel/core/vobjint.h` — `VkindGe`:n `ObjOpRead` välittää
  `osreadhand`-version ja asettaa sen `ObjCall.aux`:iin. Muiden providereiden
  `aux` pysyy nollana.
- `ge/storage/sysgefs.c` — `osreadhand`:n `ver` on nyt OBJ-providerin
  kernel-osoitin; legacy `OSPACEGET` validoi oman käyttäjäosoittimensa
  edelleen itse.

Lukusopimus:

- `out=NULL, outlen=0` on koon ja julkaistun version kysely, kuten ennen.
- `ObjCall.aux` = luettu O-versio `VkindGe`-kahvalle; muilla providerilla 0.
- Yhden OBJ-kutsun lukukoko on `Vobjpaymax` = 1 MiB. `p9space.c` tarkistaa
  rajan ennen lukua ja antaa virheen; se ei trunkoi eikä lue osittaista
  objektia. Nykyinen Python-pinta ei voi luoda yli 1 MiB:n suoraa objektia,
  joten raja säilyttää aiemman PoC-käytöksen.
- Puuttuva OID, tyhjä objekti, virheellinen/stale kahva ja
  oikeuksiltaan riittämätön kahva säilyttävät OSError-käytöksensä.
- `space.hget(h, oid)` muuntaa eksplisiittisen legacy-Space-kahvan
  `OBJOPEN(ObjOpenGe)`-providerilla. `oscopycap`/`oauthorize` säilyttävät
  täsmälleen saman Ohand-valtuusrajan; pelkkä OID ei avaa mitään.

## 3. Mittaus: OSPACEGET ennen ja jälkeen

Mittaus ajettiin T0:n omalla instrumentoinnilla ja työkalulla:

```sh
python3 tools/host/syscall-t0-measure.py --run \
  --out build/syscall-t1/t0-tool-measurement.json --summary
```

Työkalu ajaa instrumentoidun buildin, yhden O-boot/REPL-ajon, kahden bootin
persistenssitestin, palauttaa normaalit artifactit ja ajaa normaalin
savutestin. Kaikki neljä ajovaihetta raportoivat `PASS`.

| ajo / vaihe | OSPACEGET T0 | OSPACEGET T1 |
|---|---:|---:|
| boot / init-and-commit | 9 | **0** |
| boot / REPL | 24 | **0** |
| persist-boot1 / init-and-commit | 9 | **0** |
| persist-boot2 / init-and-commit | 10 | **0** |
| boot-loader (kaikki ajot) | 0 | 0 |
| **yhteensä** | **52** | **0** |

Staattinen tarkistus uudesta `micropython.elf`:stä: `ospaceget`-symboli on
olemassa vain generoituna stubbina, mutta sillä ei ole yhtään `call`- tai
tail-`jmp`-kutsupaikkaa. `obootoid.elf`:ssä `_main` kutsuu sitä edelleen
virhepolulla; se on hyväksytty T1:n rajauksessa ja siirretään T10:ssä.

OBJ-kutsut tekevät nyt työn. Esimerkiksi bootin init/commit-vaiheessa
`oreadobj.constprop.0` on `OBJOPEN`- ja `OBJCLOSE`-kutsujen tekijä, ja
`OBJINVOKE` jakaantuu `obj_read_version` (9) ja `obj_write` (8) kesken.
REPL-vaiheessa `obj_read_version` tekee 24 kutsua ja `obj_read` 349
(konsoli), `obj_write` 161. Täydellinen kutsu- ja vaihejako on JSON:ssa.

## 4. Testit

Kaikki ajot käyttivät vain kahta sallittua QEMU-työkalua, tuoretta
kertakäyttöistä O-kuvaa ja KVM:ää.

| testi | tulos | evidenssi |
|---|---|---|
| kanoninen kernel-build | PASS | `build/syscall-t1/logs/build-kernel-normal.log` |
| kanoninen MicroPython-build | PASS | 246 unit, 0 FAIL, ei undefined-symboleita; `build/syscall-t1/logs/build-mpy-normal.log` |
| O-boot -> REPL -> `6*7` + T1-lukutestit | PASS | `build/syscall-t1/repl-final/report.txt`, `build/syscall-t1/final/repl-serial.log` |
| kahden bootin persistence/restore | PASS | `build/syscall-t1/final/persist-report.txt`; `boot1_restored_or_fresh=PASS`, `boot2_restored_version_matches=PASS` |
| normaali O-boot savutesti | PASS | `build/syscall-t1/final/smoke-report.txt` |
| T0-instrumentointimittaus | PASS | `build/syscall-t1/measurement.json`; kaikki `validation`-kohdat PASS |
| puuttuva OID | PASS | `OSError: space.oget: no such O object` |
| tyhjä GE-objekti | PASS | `T1-EMPTY OK` |
| olemassa oleva GE-objekti | PASS | `T1-EXISTING OK` |
| virheellinen Space-kahva | PASS | `OSError: bad arg in system call` |
| riittämättömät oikeudet (Vwrite-only GE) | PASS | `OSError: permission denied` |
| versionumeron palautuminen | PASS | `T1-VERSION OK 5 35` |
| toistuvat avaus/luku/sulku-kierrokset | PASS | `T1-CYCLES OK 5 5` (100 kierrosta, ei kahvavuotoa) |
| ei-GE-objektin `aux` ennallaan | PASS | `T1-NONGE-AUX OK` (bytes-objektin versio 0) |
| >1 MiB objektin OBJ-luku | NOT RUN | ei saavutettavissa nykyisellä Python-pinnalla; 1 MiB:n raja säilytetty ja ylitys antaa virheen |
| fyysinen rauta, TCG, laaja regressio | NOT RUN | tehtävän rajauksen ulkopuolella |

REPL-testin kulku on tiedostossa
`build/syscall-t1/inputs/repl-t1.py`. Se luo olemassa olevan ja tyhjän
objektin legacy-allocatorilla (kirjoituspolku), lukee ne OBJ-polulla, testaa
puuttuvan OID:n, virheellisen kahvan, Vwrite-only-kahvan,
`space.hget`:n legacy-kahvalla, version 5 -> 35 kahden committin jälkeen ja
100 avaus/luku/sulku-kierrosta. Kahvamäärä `plan9.vobjlist()`:llä pysyi
arvossa 5 ennen ja jälkeen.

## 5. Artefaktit

Normaalit, testatut artifactit:

- `build/gcc/9pc64.elf` — 1 150 056 B, SHA-256
  `c7386e8e91e88bf9a8ebb1302c5b359586c1264ee6d4ff0b5b0a3de7c3359a98`
- `build/gcc/micropython.elf` — 538 048 B, SHA-256
  `264f67cbabc63ffcf7cae9fa76483745bc294be6d6dc326a59bdd96d0a524f59`
- `build/gcc/gen/obootoid.elf` — 5 456 B, SHA-256
  `10c63c33b13f31fe10c401a639aeca36a97af1700e3f1e4dac409cff48a13769`
- `build/gcc/bootx64.efi` — SHA-256
  `65409161f61f846c2ac9636029329153d03db47ab86ed1289984e5a71eb2ff89`

Mitatut (instrumentoidut) binaryt ovat
`build/syscall-t0/binaries/`:ssä: kernel `803f73be...` ja MicroPython
`bea8f507...`. Normaalin kernelin merkkijonoista ei löydy
`P2K-SYSCALL`-tekstiä.

## 6. Rajoitukset ja seuraava vaihe

- `OSPACEGET(56)` on edelleen aktiivinen ja linkitetty boot-lataimeen.
  MicroPythonin init/REPL/persistenssivaiheissa sitä ei kutsuta.
- Legacy `OSPACEOPEN`/`OVOLOPEN` suoritetaan edelleen kerran initissä, koska
  T2:n kirjoitus- ja commit-polku säilytetään. T1 ei yhdistä kirjoitus- ja
  lukukahvan elinkaarta sen enempää kuin lukupolku vaatii.
- Mittaus ja testit koskevat vain tätä amd64/PoC/QEMU-KVM-konfiguraatiota.
- Seuraava työ on T2 (kirjoitus ja commit). Tämä raportti ei väitä T2:ta,
  T10:tä eikä koko viiden kutsun tavoitetta valmiiksi.
