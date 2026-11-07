# UJ1: Ainon ohjelmointityöistunto

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

UJ1 ajaa saman todellisen amd64-kernelin, MicroPythonin, GE/O-taltion,
natiivin editorin ja TinyCC:n yhdessä QEMU-järjestelmässä. Ensimmäinen
käynnistys suorittaa ohjelmointityön; toinen avaa saman taltion, rakentaa
uudet ajonaikaiset grantit ja tarkistaa palautuneen julkaisun. Host ei käännä
Ainon HTTP-ohjelmaa eikä palvele HTTP-vastauksia.

## Ajurit ja tulokset

```sh
CC=gcc-14 bash tools/build-gcc.sh --kernel -j3
CC=gcc-14 bash tools/build-mpy-gcc.sh -j3
bash tools/qemu-user-journey.sh
```

Kattava ajo, mukaan lukien vanhat regressiot:

```sh
bash tools/test-user-journey.sh
```

Normaalit QEMU/OVMF-, mtools- ja GCC 14 -riippuvuudet tarvitaan. Testi käyttää
TCG:tä ja neljää virtuaaliprosessoria. Ajuri alustaa vain uuden tavallisen
levykuvatiedoston ja käynnistää sen kahdesti. Se ei kirjoita fyysisiä osioita,
isäntäkoneen ESP:tä tai käynnistyslataajaa. Olemassa oleva EFI-ohjelma vain
kopioidaan väliaikaiseen ESP-kuvaan; bootloaderin lähteitä ei muutettu.

Tulokset tallennetaan `docs/UJ1-VERIFICATION.json`-tiedostoon. Jokaisen
UC01–UC26-rivin tila on PASS, FAIL, BLOCKED tai NOT_RUN. PASS edellyttää
suoritettua tarkistusta; lähdekoodin olemassaolo ei riitä. Puuttuva ominaisuus
on BLOCKED, toteutetun toiminnon epäonnistuminen FAIL ja ajamatta jäänyt
käyttötapaus NOT_RUN. Nykyisessä toteutuksessa kaikki toiminnot on toteutettu,
joten puuttuvaa käyttöketjun osaa ei peitetä BLOCKED-merkinnällä.

Ajuri kirjoittaa myös epäonnistuneen ajon raportin. Välivaiheiden START-rivit
kohdistavat epäonnistumisen toiminnolle. Neljän CPU:n kernel-diagnostiikka voi
lomittua sarjakonsolin käyttäjätulosteisiin; siksi onnistuneet toiminnot
kirjataan lisäksi tavalliseen GE-objektiin `UJ1RESULTS1`-testisisältönä.
Tämä ei ole uusi GE-formaatti tai tallennusmoottori. Host lukee tämän objektin
levykuvasta ja tarkistaa lisäksi alkuperäiset objektit ja ELF:t erikseen.

`UJ1 COMPLETE PASS` syntyy vasta toisessa käynnistyksessä, kun ohjelma on
tarkistanut kaikkien 26 toiminnon valmistuneen. Host-ajuri hyväksyy sen vasta
normaalin GE/O commit -valmistumisen sekä riippumattoman sisältö- ja
binääritarkistuksen jälkeen. Epäonnistuneita kehitysajoja ei lasketa mukaan.

## V0: tarkistettu lähtötilanne

Lähtöversio oli paikallinen commit
`5c79d89388bf79bd6c9ecc7e9bb7ddde7a97b6e9` (CBOR/O-Envelope-valmistuminen).
Sillä oli jo GE/O, OID-suoritus, R1-relaatiot, NS2–NS4, Pipe-delegointi,
MicroPython ja Space 2:n TinyCC-demo. Sen aiemmat testiraportit ovat
historiallista näyttöä; niitä ei esitetä tässä tehtävässä uudelleen ajettuina.

Lähdekoodin tarkistuksessa puuttui:

- Sovellusten luettava CPU/muisti/tallennustilan yhteenvetorajapinta.
- Erillinen OID-tekstieditori.
- TinyCC-käynnistin, joka toimisi ilman bootstrapin Space-kahvaa.
- TCP-protokolla ja käyttäjäprosessien TCP6-kahvat: verkko tarjosi UDP6:n.
- Yhteinen julkaisu-, rollback- ja palautumistesti.

Nämä olivat alkuperäisen käyttäjäketjun esteet, eivät väitteitä epäonnistuneista
aiemmista regressioista. Työn lopussa vanhat TinyCC-, NS1-, N13-, NS2–NS4-,
CBOR- ja R1-testit ajetaan uudella kernelillä ja raportoidaan erikseen.

## V1: erillinen editori ja delegoitu TinyCC

`user/uj1/worker.c` on natiivi ELF-ohjelma. Editoritilassa se avaa `source`-roolin
OLOOKUP:lla ja lukee sen OID:n OSPACEGET:llä omaan 64 KiB:n puskuriin.
Pipe-ohjaus tarjoaa puskurin lukemisen, korvauksen, tallennuksen ja sulkemisen.
Versio 1:n vaihto versioon 2 tehdään editorissa aidolla tekstikorvauksella,
jonka jälkeen erillinen SAVE julkaisee uuden GE-version samalla OID:llä.
Editorin sulkeminen ja uuden prosessin avaaminen todentavat tallennuksen.

Ohjauskehyksessä on kaksi little-endian u32-kenttää: operaatio ja datan pituus.
Vastaus sisältää tilan 0 ja datan pituuden. Operaatiot ovat READ=1,
REPLACE-ALL-AND-SAVE=2, EXIT=3, REPLACE-ONE=4 ja SAVE=5. REPLACE-ONE sisältää
u32 vanhan/uuden pituuden sekä kummankin tavut ja edellyttää yksikäsitteistä
osumaa. Tämä on pieni skriptiohjattu byte-editori, ei GUI tai yleiskäyttöinen
interaktiivinen tekstieditori.

TinyCC:n uusi `--oid`-tila saa vain `source`-grantin (READ) ja `output`-grantin
(WRITE|COMMIT). Se kääntää lähteen tcc_compile_string:llä ja julkaisee ELF:n
etukäteen varattuun objektiin. Se ei avaa Spacea, varaa uusia OID:itä tai saa
bootstrap-kahvaa. `uj1-output` on vain kääntäjäadapterin sisäinen tulosavain,
ei kernelin polku. Alkuperäinen TinyCC:n Space 2 / OID-graafidemo säilyy.

Ainon luotettu MicroPython-käynnistin luo objektit ja delegoi grantit.
Tavallisten sovellusten yleistä objektien luontioikeutta ei lisätty. Editorin
oikeudet ovat READ|WRITE|COMMIT; HTTP-palvelin saa vain READ-oikeuden sivuun.
OSPAWN COPY valitsee sidonnat, oletus EMPTY ja tavallinen RFORK-perintä säilyvät.

## V2: native TCP6 ja HTTP/1.1

TCP-moottori sovitettiin olemassa olevaan Proto/Conv-IPv6-pinoon 9frontin
TCP-toteutuksesta, ei Chan/Qid/Dev-rajapinnasta. Toteutuksen MIT-lisenssi on `kernel/net/tcp-LICENSE`.
Sovituksessa päivitettiin Conv.hash, qlk ja Routehint/Route.tree, säilytettiin
alkuperäiset 32-bittiset sekvenssi- ja aikalaskurit u32int-tyypeillä sekä
poistettiin IPv4 NAT/proxy -kytkennät. Natiivirajapinta hylkää IPv4-mapped-
osoitteet; IPv4-ulostulo ei ole käytössä. Ei IPv4-palvelu- tai NAT-rajapintaa.

Uudet, aiemmin vapaat amd64-syscall-numerot:

| Numero | Nimi | Toiminto |
|---:|---|---|
| 107 | SYSSNAPSHOT | Read-only seitsemän u64-kentän yhteenveto |
| 108 | TCP6LISTEN | IPv6-osoite ja portti -> kuuntelukahva |
| 109 | TCP6CONNECT | IPv6-osoite ja portti -> yhteyskahva |
| 110 | TCP6ACCEPT | Kuuntelukahva, aikakatkaisu -> yhteyskahva |
| 111 | TCP6READ | Kahva, puskuri, pituus, aikakatkaisu |
| 112 | TCP6WRITE | Kahva, data, pituus |
| 113 | TCP6CLOSE | Sulje prosessin oma kahva |

Aiemmat syscall-numerot säilyivät. Julkiset tyypit ovat
`runtime/abi/tcp6.h`:ssa. SYSSNAPSHOT:n version 1 kentät ovat version,
CPU-määrä, fyysiset tavut, käyttäjämuistipoolin kapasiteetti, avatun O-osion
kapasiteetti, aktiiviset TCP-kahvat ja vapaat käyttäjämuistitavut.
Tallennustilan vapaata kapasiteettia ei arvioida kapasiteettikentästä.

Endpoint-taulukossa on 64 paikkaa, enintään 8 prosessia kohti. Kahvan
sukupolvitunnus, omistajan Proc ja PID tarkistetaan kernelissä. Vieraan tai
suljetun kahvan käyttö hylätään myös bootstrapilta. Prosessin exit/exec
vapauttaa sen kahvat, eikä tavallinen RFORK peri niitä. Endpointin viitteet
pinotaan syscallin ajaksi; taulukon spinlock ei kata I/O:ta. Native-verkon
stop huomioi myös TCP-endpointit. Uudet yhteydet tyhjentävät kierrätetyn
Convin edellisen Hangup-tilan. Connect odottaa enintään 5 s; accept/read
hyväksyy 0–30 s aikakatkaisun. READ-raja on 64 KiB ja WRITE-raja 4096 tavua.

Käyttö edellyttää olemassa olevaa eksplisiittistä UDP6-permitiä (net6permit). Se on karkea
verkkokäyttöoikeus, ei osoite- tai porttikohtainen palomuuri. HTTP-palvelin ja
asiakas saavat sen OSPAWN-delegoinnissa, turvallisuustestin muu lapsi ei saa.

Native IPv6 -bind ohitti aikaisemmin ipifcbindin loopback-alustuksen.
UJ1 korjaa tämän käyttämään olemassa olevaa loopbackmediumia. UDP6-selftest
kulkee samassa ajossa, jotta korjausta ei testata vain TCP:n kautta.
TCP käyttää oikeaa IPv6-paikallisreittiä ja pinon omaa mahdollista paikallisen
TCP-yhteyden splice-optimointia. Pipe ei kuljeta HTTP-pyyntöä tai -vastausta
palvelimen ja asiakkaan välillä; sitä käytetään editorin ohjaukseen ja
asiakkaan havaintojen palauttamiseen testikäynnistimelle.

Ainon C-objekti `user/uj1/server.c.in`:stä sisältää käynnistys- ja
syscall-stubit. TinyCC kääntää tämän vasta guestissa. ELF:n check-tila
suorittaa ohjelman ja tulostaa käännetyn julkaisuversion. Palvelintila lukee
GE-sivun omalla READ-grantillaan ja vastaa HTTP/1.1-pyyntöön. Vastauksessa
on Content-Length, Connection: close ja käännetty versioprefiksi.
`/stop` on vain automaation hallintapääte. GET-pyyntöjen lisäksi ei toteuteta
yleistä HTTP-palvelinta, TLS:ää, keepalivea tai tuotantohallinnan autentikointia.
HTTP-URI ei tarkoita GE:n tiedostopolkua.

## V3: koko ketju ja palautuminen

| UC | Todellinen tarkistus |
|---|---|
| 01 | Kernelin käynnistys ja native GE/O |
| 02 | Neljä CPU:ta, RAM, vapaa käyttäjämuisti ja 129 MiB O-osio |
| 03 | Lähde, sivu, kaksi ELF-kohdetta, julkaisu ja lähdesnapshotit OID-objekteina |
| 04 | HTTP-lapsi ratkaisee page-grantin ja sen natiivi vieraan OID:n luku epäonnistuu |
| 05 | IPv6-link-local, UDP6-datagrammi, TCP-kahvarajat ja timeout/cleanup |
| 06–09 | Erillinen editori, OID-luku, puskurin tekstikorvaus, commit ja uuden prosessin reopen |
| 10–12 | Erillinen TinyCC, uusi ELF-objekti ja todellinen OID-suoritus |
| 13–14 | Käännetty HTTP-palvelin ja tuore native TCP6-asiakasprosessi |
| 15 | Editorimuutos, toinen ELF, julkaisu ja HTTP-versio 2 |
| 16 | Todellinen syntaksivirhe, TinyCC-diagnostiikka, korjaus ja onnistunut uudelleenkäännös |
| 17 | Aiemman ELF:n ja lähdesnapshotin palautus, HTTP-versio 1 |
| 18–19 | Relaatioiden AND-haku ja henkilökohtainen muistiinpano-objekti |
| 20 | Editori ja vanha HTTP-palvelin käynnissä yhtä aikaa |
| 21–22 | Kielletty GE-/native-luku ja grantin peruminen kopioidussa nimiavaruudessa |
| 23 | Sovellusten sulkeminen, wait ja uudelleenkäynnistys |
| 24–25 | Sama levy, uusi kernelboot, samat OID:t, lähde/ELF/note/julkaisu ja HTTP-vastaus |
| 26 | Kaikki sovellukset reaped, namespace-grantit poistettu, TCP-kahvoja 0 ja normaali commit |

Alkuperäinen lähde-OID säilyy muokkauksissa. Jokainen julkaistu ELF saa oman
objektin, eikä käännösvirhe julkaise keskeneräistä tulosta. Rollback valitsee
aiemman ELF:n ja tallennetun lähdesnapshotin; se ei väitä toteuttavansa
rajoittamatonta GE:n vanhojen versioiden selausta. Julkaisun valinta tallentuu
tavalliseen OID-objektiin. Uudelleenkäynnistyksessä nimiavaruuksia ei palauteta
levyltä: käynnistin delegoi uudet grantit samoihin pysyviin objekteihin.

Turvallisuustesti peruu yhden grantin, ei objektin identiteettiä. Kopioidusta
testinäkymästä poistetaan muut saman objektin itsenäiset grantit ennen testiä;
muuten toinen voimassa oleva READ-grantti oikeuttaisi edelleen lukuun.
Salattua levyä tai eri Unix-käyttäjätilejä ei väitetä toteutetuiksi.

## Mitä seuraavaksi tarvitaan

UJ1 todentaa ohjelmointityön primitivejä, ei valmista työpöytäkäyttöjärjestelmää.
Seuraavat erilliset työt ovat interaktiivinen editori/käyttöliittymä,
sovellusten turvallinen yleinen objektien luonti ja nimeäminen, hienojakoisempi
verkkodelegointi, TCP:n ulkoisen vertaisen yhteensopivuus-, hävikki- ja
virhepakettitestit sekä hallittu koneen virrankatkaisu. Istunnon sulkeminen ja
GE-commit toimivat tässä; ACPI S5 -virrankatkaisua ei merkitä testatuksi.
GE sallii edelleen yhden keskeneräisen kirjoitustransaktion Spacea kohti.
CBOR ja O-Envelope säilyvät valinnaisina; Aino-ketju käyttää raakaa C-koodia,
ELF:iä ja tekstitietoja.

KVM, fyysinen laite, ulkoinen TCP-vertaislaite, pakettihävikki, TCP-fuzz ja TLS
ovat NOT_RUN. Käyttötapausten PASS ei tarkoita näiden suorittamista.

## Toteutuneen ajon näyttö

UC01–UC26: **26 PASS**, ei FAIL/BLOCKED/NOT_RUN-käyttötapauksia.
Kernel, MicroPython, natiivi editori/asiakas ja TinyCC rakennettiin.
Hostin NS1/NS3-sanitizeritestit ja CBOR-testit sekä QEMU:n alkuperäinen
TinyCC-, NS1-, N13-, NS2–NS4-, CBOR- ja R1-regressio läpäistiin.
Kaikkien regressioiden ESP-kuvista tarkistettiin sama kernelin SHA-256.

Ajon hakemisto: `build/user-journey.fnd05b`. Raaka käyttäjäketjun sarjakonsoliloki
ja regressiotulosteet ovat [UJ1-TEST-OUTPUT.txt](UJ1-TEST-OUTPUT.txt):ssä;
OID-kohtaiset molempien käynnistysten SHA-256:t ja kaikki tilat ovat
[UJ1-VERIFICATION.json](UJ1-VERIFICATION.json):ssa.

Täyden ajurin ensimmäinen loppuraportointi pysähtyi raporttityökalun väärään
TinyCC-onnistumismerkkijonoon. Varsinainen QEMU-testi oli läpäissyt.
Merkkijono korjattiin ja raportointi suoritettiin uudelleen samoista
valmistuneista ajoista; VM-tuloksia ei muutettu.

| Ajettu binääri | SHA-256 |
|---|---|
| 9pc64.elf | `ffa00e9d4a2373072cef88e7012fb29a76701b028a8fb9d24c3a7831e47b1712` |
| micropython.elf | `19471faac01a6ddadef45f9cdacda31946ae2283d1892f8572fc71a7c919690f` |
| tcc.elf | `898f34c9334e8b7b83f37c8b8c9f39c08b0dc99ff3ee6ee4712368c27e6b6d59` |
| editori/TCP-asiakas | `ffa6ec187bd1a9531df551a1c915ea37fc5a3b7463b0167c91a4c20d730808d0` |
