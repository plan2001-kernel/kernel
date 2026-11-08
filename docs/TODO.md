# Plan 2001-kernel — TODO

Tämä tiedosto sisältää historiallisia ja myöhemmin päätettäviä kehitystehtäviä. **Ei OpenCode-käsky eikä lupa tehdä muutoksia.** Kulloinenkin toteutustehtävä tulee suoraan käyttäjältä tai kehitysympäristöstä.

## Buildin aikaleima GRUB-valikkoon ja kernelin bootin loppuun

**Tila:** DONE (N11.11/N11.HW3): kernel build stamp + GRUB title implemented,
QEMU-verified, and installed on the physical `kuha` ESP through the real
`--plan`/`--install`/`--verify` installer path (title
`Plan2001-O (2026-10-29 05:01 UTC)`, id `plan2001-o`, Debian still default).
**Residual:** the timestamped kernel has not yet been physically booted; that
is an operator observation. See docs/STATUS.md.

Tavoite: käynnistysvaiheessa on voitava nähdä selvästi, mikä **nimenomainen käännetty kernel** käynnistetään, jotta vanha ESP:lle jäänyt binääri ei erehdy uudeksi.

- GRUB-valikossa nykyinen ainoa Plan2001-merkintä esitetään muodossa `Plan2001-O (YYYY-MM-DD HH:MM)` tai vastaavalla yksiselitteisellä aikaleimalla. **Pysyvä GRUB entry ID on edelleen `plan2001-o`**, ja Debian on yhä oletusvalinta.
- Kernel tulostaa **bootin lopussa kerran** oman buildinsa saman päivämäärän ja kellonajan, esim. `Plan2001-O kernel build: YYYY-MM-DD HH:MM`. Näytölle tulostuva merkkijono kuvaa buildia, ei käynnistyshetken kellonaikaa.
- Varmista, että molemmat lukevat **saman kernel-buildin metatiedon**. Aikaleiman lähde ja aikavyöhyke on määriteltävä yksiselitteisesti (esim. UTC) ja formaatti pysyy samana GRUBissa ja kernelissä. Ei erillisiä isäntäkoneen asennushetken tai EFI:n ajan arvauksia.
- Toteutuksen yhteydessä korjaa `tools/host/gcc-o-install.sh`-asentimen `--plan`/`--install`/`--verify` ja GRUB-merkinnän tarkistukset ymmärtämään muuttuva otsake. Säilytä yksi entry, turvallinen rollback, Debianin GRUB-default, `plan9.ini`:n LF-lopetus ja nykyinen osioiden koskemattomuus.
- Ei pysyvää debug-tilaa, uusia GRUB-merkintöjä tai ajonaikaista RTC-riippuvuutta. Käytännöllinen lisävarmistus myöhemmässä vaiheessa on kernelin lähdetunniste, mutta se ei ole osa tätä aikaleimatehtävää.

**N11.7:n legacy-siivoamisinventaario on aiemmin käsitelty vanhassa OpenCode-tehtävänannossa.** Historiallinen TODO-merkintä ei ole uusi aktiivinen tehtävä; uusi tehtävä tulee käyttäjältä tai kehitysympäristöstä.

## Rename Plan 2001-kernel-owned MicroPython abbreviation MP to MPY

**Status:** DONE (N11.11): Plan 2001-kernel-owned MicroPython names use MPY;
kernel/MicroPython builds and one QEMU boot were verified. External/persistent
contracts intentionally kept as MP: `OID_MP_EXEC`/numeric OID
`0x4f45584543000003`, `MPEXEC`/`NOMPEXEC`, `seed/mp`/`seed/mpoid`, the
`mp-<version>-mpy<version>` Environment runtime string, and upstream
`micropython/py|extmod|lib|mpy-cross` symbols. **Not part of N11.7.**

The project currently uses **MP** for MicroPython in some locally owned identifiers and build artifacts. In kernel and boot code, **MP** can also mean *multiprocessor/multiprocessing*; an agent has already confused MicroPython startup with multiprocessor initialization.

- Use **MPY** consistently for **Plan 2001-kernel-owned** identifiers, local variables, build targets/scripts, documentation and logs that unambiguously refer to MicroPython (e.g. `MP_EXEC` → `MPY_EXEC`, `mp_sha` → `mpy_sha`, `build-mp-gcc.sh` → `build-mpy-gcc.sh`, where safe and applicable).
- Keep **MP/SMP/AP** for their established processor-related meanings. **Do not rename MicroPython upstream's own** `mp_*`, `MP_*`, `mp_obj_t`, `mp_init()`, `MP_DEFINE_CONST_FUN_OBJ`, etc.; those are upstream API names. Note that `.mpy` is also the MicroPython bytecode file extension, not a mandate to rewrite upstream symbols.
- Inventory each candidate first, including cross-language references, generated headers, scripts, docs, object-seeding and loader code. A symbol that looks like an internal abbreviation may be used in a persistent OID layout, an ELF entry contract, a syscall ABI or an external tool. **Preserve numeric OID values, serialized formats, syscall numbers and runtime semantics**; do not change public contracts merely to improve spelling.
- Carry out the rename as a separate, bounded change with updated call sites and build dependencies. Verify the canonical kernel and MicroPython builds and one normal O boot using the existing workflow. Do not reformat physical p5 or update ESP/GRUB as part of a naming-only change.
