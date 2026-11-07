# Plan2001-O — kehitys- ja julkaisutyönkulku

Tämä dokumentti on **normatiivinen** kuvaus siitä, miten Plan2001-O:n
lähdekoodia muokataan ja julkaistaan. Se täydentää tiedostoa
[AGENTS.md](../AGENTS.md). Jos jokin vanhempi raportti, istuntoyhteenveto tai
README:n kohta on ristiriidassa tämän kanssa, tämä dokumentti ja omistajan
päätös ovat ensisijaisia. Historialliset raportit säilyvät sellaisinaan; niitä
ei kirjoiteta uudelleen, mutta niiden julkaisukäytäntöä koskevat lauseet eivät
enää ohjaa tulevaa työtä.

## Omistajan normatiivinen päätös (2026-11-04)

Seuraava päätös on kirjattu sellaisenaan ja se on sitova:

> "tuotantokerneliä ei ole. Kernel on PoC tason kernel joka sijaitsee kuha
> koneella. Kernelin koodi sijaitsee kuha koneella ja plan2001.com
> palvelimella. Muutokset tehdään kuha koneella. Kaikki muutokset julkaistaan
> välittömästi plan2001.com palvelimelle. ChatGPT antaa ohjeen opencode:lle mcp
> rajapinnan kautta. opencode lukee työn mcp rarapinnan kautta ja julkaisee
> muutoksen kokonaisuudessaan plan2001.com palvelimelle. Myös epäonnistunut
> istunto julkaistaan plan2001.com palvelimelle. Ohjelmasta on aina edellinen
> versio kokonaisuudessaan plan2001.com palvelimella. opencoden julkaisun
> jälkeen sekä kuhan että plan2001.com lähdekoodiversiot ovat samat. Jos tämä
> toiminta tapa vaatii muutoksia mcp rajapintaan, ne toteutetaan"

Täsmennys: julkaisua **ei** sidota testien läpäisemiseen. Epäonnistunut tai
puutteellinen työistunto julkaistaan omana, rehellisesti merkittynä
lähdevedoksenaan (`result=failed` tai `result=incomplete`). Aikaisempi
kokonainen vedos jää aina palautettavaksi. "Ei tuotantokerneliä" tarkoittaa,
että julkaisulla ei ole tuotantokernelin julkaisublokkia eikä sääntöä "älä
julkaise, koska testi epäonnistui". Julkaistava asia on **lähdekoodin
täydellinen vedos**, ei käyttöjärjestelmän asentaminen rautaan.

Rautaan ja tallennusmediaan liittyvät varotoimet säilyvät erillisinä: tämä
työnkulku ei oikeuta kirjoittamaan kuhan p5/ESP-tallennusmedialle, käynnistämään
laitteistoboottia tai muuttamaan GE/O-formaattia ilman erillistä lupaa.

## Mihin julkaistaan

| Rooli | Konepa | Tehtävä |
|---|---|---|
| Kehitys | `kuha` | Ainoa paikka, jossa lähdettä muokataan. Kanoninen työpuu on `/home/lapere/Src/Plan2001-O`. |
| Julkaisu | `plan2001.com` (Ubuntu Linux) | Jatkuvasti päivitetty, luettavissa oleva kokonainen lähdevedos ja versiohistoria. Palvelimen lähdettä ei muokata käsin itsenäiseksi versioksi. |

MCP-rajapinta on `https://plan2001.com/mcp` (lähde, read-only) ja
`https://plan2001.com/agent-mcp` (OpenCode-tehtäväjono, OAuth). Tehtävän
nouto tapahtuu `task_claim`-kutsulla ja raportointi `task_report`-kutsulla.
Julkaisu kuuluu työn suoritusketjuun riippumatta tuloksesta.

Myös palvelimen MCP-lähde on kanonisessa puussa hakemistossa
`tools/host/mcp-server/` (palvelin, työkaluskeema, OAuth-toteutus, systemd-unit
ja nginx-sijainti). Salaisuudet, `private.conf`, OAuth-tietokanta ja
lähdesnapshotit eivät ole puussa eivätkä julkaisussa; ne ovat vain palvelimella
rajoitetulla omistajuudella.

## Julkaisun sisältö

Julkaistava lähde on kuhan senhetkinen **koko kanoninen lähdepuu**, ei diffi,
satunnainen arkisto eikä agentin yhteenveto. Mukaan tulevat kaikki
lähdetiedostot, build- ja testiskriptit, dokumentit sekä agentin pysyvä
konteksti (`AGENTS.md`).

**Mukaan:** kaikki työpuun tavalliset tiedostot, jotka on lueteltu tiedostossa
`tools/host/mcp-public-files.txt`.

**Pois:** vain seuraavat määritellyt joukot:

- johdetut artefaktit ja välimuistit: `build/`, `__pycache__/`, `.cache/`,
  `*.pyc` sekä muut `SKIP_DIRS`-luettelon hakemistot;
- `tools/host/mcp-private-files.txt` itse (poissulkuluettelo);
- vanha agenttiohjeen kaksoiskappale, joka on merkitty yksityiseksi julkaisuluettelossa;
- salaisuudet ja tunnukset. Niitä ei koskaan lisätä työpuuhun eikä
  julkaisulistaan.

`tools/host/check-public-text.py` tarkistaa sekä työpuun että staging-puun
ennen julkaisua. Se estää vanhentuneet ulkoiseen koodi-isäntään viittaavat
termit, jotta julkaisuun ei vuoda poistettua käytäntöä.

Polut julkaisuarkistossa (`source/<polku>`) ovat julkaisuartefaktin teknisiä
nimiä. Ne eivät ole Plan2001-O:n kernelin olioabstraktio, eivätkä ne luo
GE/O-nimiavaruutta tai pakota tiedostohierarkiaa kernelin sisälle.

## Snapshotin identiteetti ja manifesti

Jokainen työistunto tuottaa:

- `snapshot_id`: deterministisen `source.tar.gz`-arkiston SHA-256. Sama
  lähdesisältö tuottaa saman tunnisteen (deduplikointi on sallittu).
- `manifest.json` ja `manifest.tsv`: jokaisen tiedoston polku, koko ja
  SHA-256.
- `snapshot.json`: arkiston koko ja hash, manifestin hash, tiedostomäärä,
  lähdekoko ja lähdepäivä.
- **istuntotietue** `sessions.jsonl`-tiedostossa: yksilöllinen `session_id`,
  `snapshot_id`, `task_id`, `result` (`completed` / `failed` /
  `incomplete` / `unknown`), build- ja testistatus, mahdolliset testitulokset,
  edellinen snapshot, `activated`-tila, `activation_status`, aikaleima ja
  hashit. Sama `session_id` ei koskaan kirjaudu kahdesti.

Myös istunto, jossa lähdekoodi ei muutu, kirjataan omana istuntotietueenaan;
se voi viitata jo olemassa olevaan `snapshot_id`:hen. Epäonnistunut istunto
saa näkyvän istuntotietueen, jonka `result` on `failed`; sitä ei koskaan
naamioida `completed`-tilaksi.

`latest` tarkoittaa aina todellista uusinta kuhan lähdevedosta, ei
pakotettua vanhaa "hyväksi todettua" versiota. Erillistä `latest-tested`
viitettä ei tarvita; testistatus on istuntotietueessa.

## Julkaisun kulku

Kanoninen komento on:

```sh
make pub
# tai suoraan:
python3 tools/host/mcp-publish.py --task-id <id> --result completed|failed|incomplete \
  --note "<lyhyt kuvaus>" --build-status <tila> --test-status <tila>
```

Työistunnon automaattiseen loppuun (myös virhetilanteessa) käytetään
`tools/host/session-publish.sh`-käärettä, joka julkaisee `EXIT`-ansassa:

```sh
tools/host/session-publish.sh --task-id <id> --result completed -- <työkomento>
```

Kulku:

1. **Lukko.** `build/mcp-publish/publish.lock` estää rinnakkaisen julkaisun.
2. **Validointi.** Julkinen ja yksityinen tiedostoluettelo tarkistetaan;
   luokittelemattomat, puuttuvat ja symlinkit hylätään.
3. **Tilannevedos.** Lähde kopioidaan eristettyyn staging-puuhun ja
   tarkistetaan uudelleen. Arkisto luodaan deterministisesti (kiinteä
   `mtime`, omistajuus ja järjestys).
4. **Siirto.** Snapshot ladataan SSH:lla palvelimen yksityiseen
   staging-hakemistoon (`/var/lib/plan2001-mcp/kernel-source/.stage.*`).
5. **Palvelinvarmennus.** Palvelimen `mcp-publish-remote.py` tarkistaa
   jokaisen tiedoston koon ja SHA-256:n, manifestin ja arkiston, sekä
   **edellisen** snapshotin eheyden ennen muutoksia.
6. **Aktivoinnin journalointi.** Valmis staging siirretään yhdellä
   atomisella `rename`-operaatiolla `snapshots/<snapshot_id>`-hakemistoon.
   Ennen `CURRENT`-muutosta kirjoitetaan `fsync`-varmennettu
   `.activation.json`-journali, jossa on snapshot, edellinen snapshot,
   perus-CURRENT ja koko istuntotietue. Vasta sen jälkeen `CURRENT` vaihdetaan
   atomisella `rename`-operaatiolla ja `.current.seq`-järjestysluku
   päivitetään. Istuntotietue lisätään `sessions.jsonl`-tiedostoon vasta, kun
   `CURRENT` on pysyvästi uusi. Lopuksi journali poistetaan.
7. **Toipuminen ja lukuvarmennus.** Jokainen julkaisu ja `--recover-only`
   aloittaa remote-`reconcile`-vaiheella. Jos journali on kesken:
   - `CURRENT` on jo uusi snapshot: istuntotietue lisätään (tai todetaan jo
     lisätyksi samalla `session_id`:llä) ja journali poistetaan;
   - `CURRENT` on vielä edellinen: aktivointi viimeistellään idempotentisti;
   - `CURRENT` on ehtinyt uudempaan snapshotiin: vanha vedos säilytetään
     historiassa `activated=false`-tietueella eikä `CURRENT`-osoitinta siirretä
     taaksepäin.
   Tämän jälkeen julkaisija vertaa kuhan snapshotin hashit palvelimen
   SSH-lukemiin ja MCP:n `kernel_status`/`kernel_manifest`/`kernel_read`/
   tarvittaessa `kernel_archive_chunk`-vastauksiin. Vasta hash-täsmäyksen
   jälkeen raportoidaan, että kuha ja palvelin ovat samat.

Jos vaihe 4–7 epäonnistuu ennen aktivointia, `CURRENT` jää ennalleen,
osittaista snapshotia ei koskaan tule näkyväksi ja julkaisija kirjoittaa
`build/mcp-publish/pending/<snapshot_id>.json`-uudelleenyritystietueen.
Jos palvelin ehti aktivoida snapshotin mutta jälkivarmennus epäonnistui,
julkaisija ei väitä `CURRENT`-osoitinta ennalleen: tietue merkitään tilaan
`activated-unverified`, ja seuraava ajo uudelleenvarmistaa saman aktivoinnin
samalla `session_id`:llä eikä julkaise sitä uutena istuntona. Jos vanha
pending-vedos on ehtinyt vanhentua uudemman julkaisun jälkeen, se tallennetaan
historiaan mutta sitä ei aktivoida. Pysyvässä verkkokatkoksessa tai
rikkoutuneella koneella täydellistä takuuta ei ole; sellainen poikkeama
raportoidaan avoimesti.

## Säilytys ja palautus

Palvelin säilyttää **kaikki** aktivoidut snapshotit. Automaattista poistoa ei
ole. Vähimmäisvaatimus on, että edellinen ja nykyinen kokonainen vedos ovat
aina luettavissa. Palautus tehdään kirjoittamalla `CURRENT` takaisin haluttuun
`snapshot_id`:hen; snapshotit ovat muuttumattomia ja hash-varmennettuja.
Vanhaa ainoaa kokonaista versiota ei koskaan korvata eikä poisteta.

## MCP-lukurajapinta

Kaikki lähdetyökalut ovat edelleen read-only eivätkä vaadi kirjoitustunnusta.
Uusi `kernel_history` näyttää istunnot uusimmasta vanhimpaan ja listaa
saatavilla olevat snapshotit:

| Työkalu | Merkitys |
|---|---|
| `kernel_status` | Nykyinen `snapshot_id`, metadata, uusin istuntotietue, nykyiseen snapshotiin liittyvä istunto, `activation_consistent`, mahdollinen `pending_activation` ja saatavilla olevat snapshotit. |
| `kernel_pop` | Sama nykyinen snapshot; ei muuta mitään. |
| `kernel_history` | Julkaisuistunnot (`session_id`, `task_id`, `result`, `snapshot_id`, hashit) ja snapshot-luettelo. |
| `kernel_manifest` | Snapshotin koko tiedostomanifesti sivutettuna. |
| `kernel_list` | Hakemistomainen näkymä snapshotin poluista. |
| `kernel_read` | Enintään 4096 tavua tai 400 riviä yhdestä tiedostosta. |
| `kernel_search` | Teksti- tai tiedostonimihaku. |
| `kernel_archive_chunk` | Koko deterministisen arkiston paloittainen luku ja hash-varmennus. |

Uusimman vedoksen lukeminen:

```json
{"name":"kernel_status","arguments":{}}
{"name":"kernel_read","arguments":{"snapshot_id":"<status.snapshot_id>","path":"docs/STATUS.md","offset":0,"length":2048}}
```

Edellisen kokonaisen vedoksen löytää `kernel_history`-kutsusta ja lukee samalla
`snapshot_id`-parametrilla. Vanhat työkalut, skeemat ja historialliset
`snapshot_id`:t pysyvät yhteensopivina.

## Kirjoitusoikeus

Julkaisun kirjoitusoikeus on vain kuhan valtuutetulla työntekijällä: SSH
`plan2001.com`-koneeseen ja rajattu `sudo`-oikeus
`/var/lib/plan2001-mcp/kernel-source`-hakemistoon. MCP:n
`task_submit`/`task_status`/`task_claim`/`task_report` ja read-only
lähdetyökalut eivät anna kirjoitusoikeutta lähdevedokseen. Salaisuudet eivät
koskaan päädy lähdearkistoon.

## Automatisoinnin nykytila

- **Toteutettu:** julkaisutyökalu, SSH-siirto, palvelinvarmennus,
  journaloitu ja toipuva atominen aktivointi, idempotentti uudelleenvarmennus,
  historiasäilytys ilman `CURRENT`-osoittimen taaksepäin siirtämistä,
  istuntohistoria, edellisen version säilytys, pending-retry,
  `EXIT`-ansa-kääre sekä dokumentoitu agenttisääntö `AGENTS.md`:ssä.
- **Automaattinen OpenCode-kytkentä:** projektin
  `.opencode/plugins/plan2001-worker-publish.js` latautuu automaattisesti
  tässä työpuussa käynnistyvässä OpenCodessa. Toteutus on
  `tools/host/opencode/plan2001-worker-publish.js` ja valinnainen
  globaali asennus `tools/host/opencode/install.sh`. Kytkentä seuraa
  `task_claim`/`task_report`-työkalujen valmistumista ja julkaisee myös
  `/new`-tilanteessa sekä `session.idle`, `session.error` ja
  `session.deleted` -tapahtumissa, jos väitettyä tehtävää ei ole vielä
  julkaistu. Tila on konekohtaisessa tiedostossa
  `build/mcp-publish/opencode-worker-state.json`, ei julkaistavassa puussa.
  Manuaalinen `make pub` ja `session-publish.sh` säilyvät varamenetelminä.
- **Testattu:** ks. [STATUS](STATUS.md) ja julkaistujen istuntojen
  `sessions.jsonl`.
- **Avoin:** monen samanaikaisen ylätason OpenCode-istunnon reaaliaikainen
  keskinäinen koordinointi on rajallinen: `/new` tai uusi ylätason istunto
  julkaisee muiden istuntojen keskeneräiset väitetyt tehtävät `incomplete`- tai
  `failed`-tuloksena. Tämä rajoite raportoidaan eikä sitä väitetä valmiiksi.

## Ristiviitteet

- [AGENTS.md](../AGENTS.md) — sitova agenttisääntö.
- [STATUS](STATUS.md) — ajantasainen toteutus- ja testaustila.
- [README](../README.md) — julkaisukomennon pikakuvaus.
- [REPOSITORY-LAYOUT](REPOSITORY-LAYOUT.md) — lähdepuun rakenne.
