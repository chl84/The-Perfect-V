# Sort skjerm under World Cup — undersøkelse 13. oktober 2026

Undersøkt på den berørte maskinen etter omstart. Alle klokkeslett er norsk
sommertid, UTC+02:00. Den første undersøkelsen endret ikke spillkode, installerte
pakker, skrivebordskonfigurasjon eller personlige lagringer. Etter brukerens
godkjenning ble oppfølgingen nederst gjennomført, med nye tester og endringer i
spillet. Utpakkede prosesskjerner er slettet etter bruk.

## Ny hendelse kl. 03:12:33: kompatibilitetsmodus var ikke tilstrekkelig

Denne oppdateringen gjelder det tredje krasjet, ved videreføring i World Cup.
Resten av rapporten beskriver de to tidligere hendelsene og tiltakene før dette
krasjet. De tidligere beståtte grafikkprøvene dokumenterte ikke stabilitet i
vanlig spilling med musefangst.

**Bekreftet forskjell:** Første tidsavbrudd denne gangen er `gfx_0.1.0`, knyttet
til **Hyprland**, PID 1171 / kommandotråd 1191. Køreset feiler; full GPU-reset
feiler ved gjenopptak med `kiq ... -110`, `KCQ enable failed` og
`GPU Recovery Failed: -110`. Først etter dette kommer tidsavbrudd på
`gfx_0.0.0`; dens registrerte emitted/signaled-sekvenser er begge 180667.

Spillets Mesa-tråd avbrøt med retur **-125**, bibliotekforskyvning `0xbf2305` og
den eksakte strengen:

```text
amdgpu: The CS has cancelled because the context is lost. This context is innocent.
```

Dette skiller seg fra `-62` / «guilty of a hard recovery» i de to første dumpene.
Krasjkommandoen inneholder `--graphics-compat`. Dumpen bekrefter LLVM 22.1.8,
aktiv synkronisering og `Screen::JumpReady`, bakkeindeks 26, renn **27 av 32**,
første omgang, hopp-posisjon 7. Simulasjonen står normalt ved start, elapsed=0,
x=-65.800003 og y=63.139267. Hovedtråden venter i Mesa/EGL/SDLs swap-sti;
spillets eksplisitte `glFinish` ligger før dette kallet.

Hyprland avbrøt kl. 03:12:58 i `CHyprOpenGLImpl::beginSimple` gjennom
`renderHWCursorBuffer`, `attemptHardwareCursor` og `setCursorFromName` under
et arbeidsromsbytte. Dette skjer omtrent 25 sekunder etter første GPU-feil og
kan være en følge av en allerede tapt grafikkontekst. Stakken beviser ikke at
maskinvaremarkøren var den GPU-jobben som opprinnelig stoppet.

### Feil i skjermovergangen og manglende testdekning

`nextCupJumper()` kalte først `newJump()`, som byttet til `Playing` og fanget
musen. Deretter byttet samme funksjon umiddelbart til `JumpReady` og slapp den
igjen, før noe bilde ble tegnet. Hver forberedelse av et menneskehopp sendte
dermed unødvendig låsing/opplåsing og skjuling/visning av markøren til SDL og
skrivebordet. Dette er en bekreftet kodefeil og en mulig utløsende mekanisme;
sammenhengen med selve GPU-låsen er fortsatt ikke bevist.

Alle tidligere fullspilltester brukte `!smoke` for å slå av musefangst. De
isolerte renderprøvene opprettet heller ikke en vanlig spilløkt med disse
markørovergangene. Dermed dekket de ikke denne delen av vanlig spilling.

Forberedelse går nå direkte til `JumpReady`. Musefangst starter ved
`StartTurn`, og SDL-modus endres bare når ønsket tilstand er forskjellig fra
den faktiske. Det separate `SDL_ShowCursor`-kallet er fjernet fordi
[SDLs relative modus allerede skjuler markøren](https://wiki.libsdl.org/SDL2/SDL_SetRelativeMouseMode).
Nullstilling av knappene og bevegelsesakkumulatoren beholdes ved skjermbytte.

Den nye eksplisitte `--smoke-mouse-capture`-testen bruker et synlig
fullskjermvindu og faktisk relativ musemodus. Den klikker beregnede sentre av
Ready/Neste-knapper gjennom SDLs hendelseskø og vanlig hit-testing. Den krever
frigitt mus ved klargjøring/resultat, nøyaktig én fangst ved start og ingen
ekstra modusbytter ved Neste eller gjenopptak. To første programvareforsøk i
flislagt vindu stoppet fordi Hyprlands fokus-følger-mus pauset spillet når
pekeren ble sluppet; ingen GPU-feil ble registrert. Derfor brukes fullskjerm
for den kontrollerte fangsttesten.

### Målrettet skrivebordstiltak

Hyprland brukte maskinvaremarkør på eDP-1 med standardverdien 2 (automatisk).
Brukerens `~/.config/hypr/looknfeel.lua` setter nå
`cursor.no_hardware_cursors = 1` i den eksisterende Lua-konfigurasjonen.
[Hyprland dokumenterer verdien 1 som avslått maskinvaremarkør](https://wiki.hypr.land/Configuring/Basics/Variables/).
`hyprctl reload` og `hyprctl configerrors` besto; `getoption` viser 1 og
`monitors` viser `hardwareCursorsInUse: false`.

Originalen er beholdt i
`~/.config/hypr/looknfeel.lua.bak.tpv-cursor.20261013-032520`.
Tilbakerulling består i å fjerne den tilførte `no_hardware_cursors = 1`-linjen
og kommentaren, deretter kjøre `hyprctl reload` og `hyprctl configerrors`.
Markørutseendet er beholdt. Dette tiltaket unngår den aktuelle markørstien,
men er ikke i seg selv bevis for rotårsaken.

### Vulkan-vurdering og lagring

En full omskriving til Vulkan kan ikke uten videre rette en feil der
Hyprlands GPU-kø stopper først. Spillet, kompositoren og Vulkan-driveren deler
fortsatt GPU, kjernedriver og firmware. Mesa tilbyr dessuten
[Zink, som utfører OpenGL gjennom Vulkan](https://docs.mesa3d.org/drivers/zink.html),
så en annen spilldriver kan sammenlignes uten å skrive om motoren først.

Siste personlige `cup.sjc`, lagret kl. 03:12:29, er kopiert til
`captures/gpu-diagnostics/third-crash/` sammen med den aktuelle krasjbinæren,
tilsvarende typeinformasjon og utvalgte kernel-/GDB-felt. Personlige lagringer
brukes ikke som skrivemål for testene. Ingen utpakket prosesskjerne er beholdt.

## Konklusjon og sikkerhet i funnene

**Bekreftet:** AMD-grafikkøen stoppet mens The Perfect V kjørte. Linux knytter
den tidsavbrutte jobben til spillets Mesa-kommandotråd. Tilbakestilling av den
enkelte køen mislyktes, og driveren startet en full GPU-tilbakestilling. Mesa
avbrøt spillet etter at GPU-konteksten var tapt. Den tidligere hendelsen samme
natt hadde samme driverfeil og ble fulgt av et Hyprland-krasj og en mislykket
Hyprland-omstart.

**Mest sannsynlig forklaring på den sorte skjermen:** Spillets grafikkarbeid
utløser en GPU-lås som også rammer skjermkompositoren, siden begge bruker samme
Radeon-kort. Dette er en feil i grafikkstakken utløst under spillkjøring. Det er
ikke påvist at Omarchy-konfigurasjonen er den opprinnelige årsaken.

**Fortsatt uavklart:** Den eksakte tegnekommandoen eller shaderen som låste
GPU-en, og om utløsende feil ligger i spillets bruk av OpenGL, Mesa/RadeonSI,
kjernedriveren, firmware eller maskinvaren. En CPU-tråd som står i tegning av
hopperkroppen når dumpen tas, identifiserer ikke den tidligere GPU-jobben som
stoppet: kommandoutføringen er asynkron.

## Tidslinje fra de to berørte oppstartene

| Tid | Hendelse |
| --- | --- |
| 00:35:10 | `ring gfx_0.0.0 timeout`, knyttet til `ski-jump-pro`, PID 176420, kommandotråd 176429. |
| 00:35:10 | Tilbakestilling av køen mislykkes; full GPU-tilbakestilling starter. |
| 00:35:11 | Gjenoppretting feiler med `ring kiq_0.2.1.0 test failed (-110)`, `KCQ enable failed`, `resume of IP block <gfx_v10_0> failed -110` og `GPU Recovery Failed: -110`. |
| 00:35:11 | Hyprlands egen grafikkø får også tidsavbrudd. |
| 00:35:29 | Hyprland, PID 1184, avbryter i OpenGL-rendering av maskinvaremarkøren. |
| 00:35:33–34 | Automatisk omstart av Hyprland feiler med `CBackend::create() failed!`; ny krasjdump, PID 178044. |
| 02:12:48 | The Perfect V startes fra skrivebordet. |
| 02:16:15 | Siste fullførte replay og rekordfil lagres. |
| 02:16:17 | World Cup-kontrollpunkt lagres. |
| 02:16:35 | Ny `ring gfx_0.0.0 timeout`, nå knyttet til `the-perfect-v`, PID 42134, kommandotråd 42142. |
| 02:16:35 | Køtilbakestilling mislykkes; full GPU-tilbakestilling starter. |
| 02:16:36 | Loggen sier `GPU reset succeeded, trying to resume`, deretter `PSP is resuming...`. Journalen stopper under gjenopprettingen. |
| 02:17:39 | Nåværende oppstart begynner. |

Meldingen om at selve GPU-resetten lyktes kl. 02:16:36 dokumenterer ikke at
grafikkstakken kom tilbake. Ingen vellykket fullføring av gjenopprettingen er
lagret etter dette. Det finnes heller ingen Hyprland-krasjdump fra den siste
hendelsen. Den første hendelsen viser direkte hvordan en GPU-lås rammet
Hyprland; den siste kan ha stoppet tidligere i gjenopprettingen.

De sentrale linjene fra siste hendelse er:

```text
02:16:35 ring gfx_0.0.0 timeout, signaled seq=330222, emitted seq=330223
02:16:35 Process the-perfect-v pid 42134 thread the-perfec:cs0 pid 42142
02:16:35 Starting gfx_0.0.0 ring reset
02:16:35 Ring gfx_0.0.0 reset failed
02:16:35 GPU reset begin!. Source: 1
02:16:36 MODE2 reset
02:16:36 GPU reset succeeded, trying to resume
02:16:36 PSP is resuming...
```

## Hva krasjdumpen faktisk inneholder

Krasjtråden er Mesa-kommandotråden, ikke spillets simulasjonstråd. De to
spilldumpene har identiske bibliotekforskyvninger i abort-stakken:

```text
abort
libgallium-26.2.2-arch1.1.so + 0xbed063
libgallium-26.2.2-arch1.1.so + 0xbf22b5
libgallium-26.2.2-arch1.1.so + 0x5dcc21
libgallium-26.2.2-arch1.1.so + 0x62784d
```

Matchende Mesa-feilsøkingssymboler kunne ikke hentes: Archs offisielle
debuginfod-tjeneste returnerte HTTP 404 for bibliotekets build-ID
`58549bc5c66cecb4a3972fe621d5756abbe548d6`. De navnløse funksjonene er derfor
ikke gitt oppdiktede navn. Instruksjonene i det installerte biblioteket og
registerverdiene i kjernen bekrefter likevel den konkrete abort-stien:

```text
Kommandoretur: -62
amdgpu: The CS has cancelled because the context is lost.
This context is guilty of a hard recovery.
```

Spillets hovedtråd var samtidig i `ellipsoid`, `drawAthleteGeometry`,
`Renderer::athlete`, `Renderer::begin` og `App::render`.

For å lese spilltilstanden uten å endre krasjbinæren ble gjeldende `main.cpp`
kompilert til et separat objekt med typeinformasjon. Typene ble lagt til i
GDB; objektadressen ble hentet fra kallestedet i den opprinnelige binæren.
Cup-feltene stemmer med det uavhengig validerte kontrollpunktet på disk.

| Felt | Verdi i siste krasjdump |
| --- | --- |
| Skjerm | `Playing` |
| Bakke | Intern indeks 19: Sweden, K140 |
| World Cup | Renn 20 av 32, andre omgang, menneskelig hopper |
| Fase | `Outrun`, originalfase 11 |
| Hopptid | 14,3857 sekunder |
| Posisjon | x = 176,0326; y = -109,2896 |
| Hastighet | vx = 20,0660; vy = -5,8892 |
| Fall | `false` |
| Replay-bilder hittil | 810 |
| Tilrettelagt hoppseed | 290325008 |
| Terrenglister | Nøyaktig to: bakke 18 og 19 |
| Snøfall | `false` |
| Renderer | `AMD Radeon 610M (radeonsi, raphael_mendocino, ACO, DRM 3.64, 7.2.5-3-omarchy)` |

Det lagrede kontrollpunktet og dumpen viser dermed renn 20, uavhengig av det
omtrentlige antallet bakker som ble oppgitt. Tallene over er endelige, normale
simulasjonsverdier. Nytt atmosfærisk snøfall var ikke aktivt i det bildet som
ble tegnet da prosessen ble avbrutt. Fallkorreksjonen var heller ikke aktuell
for denne hoppfasen uten fall. Dette avgrenser den pågående tegningen; det
utelukker ikke tidligere arbeid i samme GPU-kø.

## System, ressurser og oppdateringer

| Komponent | Installert/kjørende versjon |
| --- | --- |
| Omarchy | 4.0.4 |
| Kjørende kjerne | 7.2.5-3-omarchy |
| Separat installert Arch-kjerne | 7.2.3.arch1-3 |
| Mesa | 26.2.2-1 |
| Hyprland | 0.56.2-2 |
| Aquamarine | 0.15.0-2 |
| SDL | SDL2-compat 2.32.72-1, SDL3 3.4.16-1 |
| GPU | Integrert Radeon 610M / Mendocino, `amdgpu` |
| GPU-minne fra oppstartsloggen | 512 MiB VRAM, omtrent 7,45 GiB GTT |
| RAM/swap | Omtrent 14,9 GiB RAM; omtrent 29,8 GiB samlet disk- og zram-swap |

Ingen OOM-drap, GPU-sidefeil, minneallokeringsfeil eller kritisk temperaturfeil
er registrert rundt hendelsene. Tilgjengelig RAM etter omstart sier ikke hvor
mye som var ledig ved krasjet. GPU-minnebruken ved krasjet er heller ikke
registrert, så all ressursmangel kan ikke utelukkes bare ut fra journalen.

Mesa og Hyprland ble oppdatert 8. september, og Omarchy-kjernen ble installert
16. september. Det er ingen samtidig pakkeoppdatering som forklarer disse to
hendelsene. Et søk i de bevarte kjerneloggene fra 9. september fant disse to
GPU-timeout-hendelsene; ingen tidligere tilsvarende registrering i perioden.

Det finnes tre eldre korrigerte CPU L1-cachefeil fra 21.–23. september.
Linux beskriver dem som korrigerte feil. Ingen tilsvarende maskinvarefeil er
registrert samtidig med spillkrasjene. De er ikke bevis for årsaken her, men
maskinvare er heller ikke endelig frikjent.

## Verifikasjon uten ny belastning på AMD-rendereren

1. `ctest --test-dir build --output-on-failure`: **20 av 20 besto**.
2. CPU-kontroll av renderer: **1 859 bilder**, alle 32 bakker, normal- og
   fallforløp, kameraer og spøkelseshopper. Ingen ikke-endelige argumenter,
   ubalanserte OpenGL-stakker eller manglende ressursfrigjøring i modellen.
   Maksimalt to cached lister og 1 345 446 cached vertices. Modellen bruker
   ingen GPU og simulerer ikke hele driveren.
3. Eget testprogram med `SDL_VIDEODRIVER=offscreen`,
   `LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe`, fire rendertråder og
   1440×900/4× MSAA: **227 bilder** på alle 32 bakker, tre kameraer,
   panorama, flyvning, utrenn og brukerens siste replay. Renderer-identiteten
   ble kontrollert som llvmpipe. **GL_NO_ERROR gjennom hele testen**.
4. Samme test med `glFinish()` etter hvert bilde, slik at renderarbeidet var
   ferdig før neste bilde: **227 bilder besto igjen**, uten OpenGL-feil.
5. Native innlasting av brukerens `cup.sjc`: checksum og struktur **gyldige**.
   Siste replay har **1 004 bilder** og gyldig pose, posisjon, hastighet og vind.

En relevant forskjell mellom de to programvaretestene: uten eksplisitt
synkronisering nådde prosessens maksimale residentminne omtrent **1 626 MiB**;
med `glFinish()` stabiliserte det seg rundt **261 MiB**. Dette viser at en
grense på antall terrenglister ikke alene begrenser ressursene til arbeid som
fortsatt er i flyt gjennom en asynkron renderer. Målingen gjelder llvmpipe
med offscreen-presentasjon og rask veksling av bakker. Den dokumenterer ikke
tilsvarende opphopning på Radeon under vanlig spilling med VSync.

Spillet ber om VSync, men sjekker ikke returverdien fra
`SDL_GL_SetSwapInterval` i [main.cpp](../src/main.cpp). Renderløkken har en
`SDL_Delay(1)` og ingen selvstendig grense for antall bilder i flyt.
Dette er et konkret spor for en kontrollert synkroniseringstest, ikke en
bekreftet forklaring på AMD-låsen.

Det vanlige fullspill-smoketesten ble også kjørt isolert med llvmpipe og
midlertidige lagringer. Første styringshopp lyktes, men testen avsluttet senere
med **`CPU jump did not finish`**, exit 1, fra `nextCupJumper` etter grensen
på 20 000 simulasjonstikk. Dette er et separat spillproblem: det gir en
håndtert C++-feil og avslutter spillet, ikke Mesa-aborten og GPU-resetten fra
brukerens hendelse. Fullspill-smoketesten kan derfor ikke rapporteres som
bestått. CPU-hoppfeilen er ikke rettet i denne undersøkelsen.

## Lagringer og hva som kan gjenopptas

`~/.local/share/SkiJumpPro/SkiJumpPro/cup.sjc` er gyldig og ligger på renn
20 av 32, andre omgang, før menneskehoppet som var i gang. World Cup kan
gjenopptas fra dette kontrollpunktet; det ufullførte hoppet må tas på nytt.
Siste fullførte replay og rekorder finnes. Ingen endring eller opprydding er
gjort i brukerens lagringsmappe, og det er ingen dokumentert korrupsjon i de
kontrollerte filene.

GPU-driveren laget også en `devcoredump` i den gamle oppstarten. Denne er
ikke tilgjengelig etter omstart; `/sys/class/devcoredump` inneholder ingen
gjenværende GPU-dump. Prosesskjernene er fortsatt lagret av systemd. Det
manglende GPU-kommandodumpet begrenser hvor presist selve GPU-låsen kan
forklares i ettertid.

## Prioriterte neste tiltak

1. **Midlertidig programvarerendering:**

   ```sh
   LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe ./play.sh
   ```

   Dette går utenom spillets RadeonSI-renderarbeid og vil være tregere.
   Testene ovenfor bekrefter offscreen-bruk av llvmpipe; vanlig
   skrivebordspresentasjon med denne kommandoen er ikke testet her. Kontroller
   at oppstartsteksten faktisk sier `llvmpipe`.

2. **Første kontrollerte maskinvaretest:** Bytt bare shaderkompilator fra ACO
   til LLVM for én kjøring, med `AMD_DEBUG=usellvm ./play.sh`, og behold resten
   av innstillingene. Alternativet finnes både i installert Mesa og i
   [Mesa-dokumentasjonen](https://docs.mesa3d.org/envvars.html#radeonsi-driver-environment-variables).
   ACO er påvist i krasjdumpen. At LLVM vil løse problemet er ikke påvist.
   Denne testen bruker fortsatt AMD-kortet og kan derfor møte samme lås.

3. **Test rendererens kø og MSAA hver for seg:** En diagnostisk variant med
   eksplisitt synkronisering/begrensning av bilder i flyt, deretter en separat
   variant uten 4× MSAA. Registrer VSync-retur, faktisk swap-intervall,
   bildehastighet, valgt bakke, kamera, hoppfase og renderer. Unngå å endre
   flere variabler samtidig; ellers blir et vellykket forsøk vanskelig å tolke.

4. **Sammenlign grafikkstakk:** Kontroller nyere sammenhengende
   Mesa/firmware/kjerne-versjoner eller en installert alternativ kjerne.
   Oppgradering er ikke utført her. Det ble ikke funnet en verifisert
   upstream-feilretting som entydig matcher denne hendelsen. Nyere
   [Mesa 26.2.3](https://docs.mesa3d.org/relnotes/26.2.3.html) inneholder
   RadeonSI/ACO-endringer, men de dokumenterer ikke en løsning på akkurat
   denne låsen.

5. **Hvis maskinvarefeilen skal gjenskapes:** Forbered ekstern logginnsamling
   og uttak av GPU-`devcoredump` før testen, og ta en avgrenset OpenGL-trace
   rundt Sweden K140, andre omgang/utrenn. Ikke bruk gjentatte uregistrerte
   fullsesonger som eneste test. Mesa anbefaler spor som
   [apitrace og isolering av GPU-jobben](https://docs.mesa3d.org/graphics-debugging/debugging-misrenderings-crashes.html)
   for å finne den faktiske kommandoen som henger. Ingen ny maskinvaretest
   med spillet ble startet i denne undersøkelsen.

Å nullstille Omarchy-konfigurasjon, slå av maskinvaremarkøren eller øke
driverens timeout er ikke dokumentert som løsning på denne hendelsen.
Markørkrasjet var en følge av GPU-problemet i den første oppstarten.

## Lokale diagnostiske artefakter

- `/tmp/tpv-core-state.txt`: utvalgte spill-/rendererfelt og abort-retur fra GDB.
- `/tmp/tpv-render-probe.log`: programvarerendering uten eksplisitt synkronisering.
- `/tmp/tpv-render-probe-synchronized.log`: samme rendering med `glFinish()`.
- `/tmp/tpv-software-smoke.log`: fullspilltest som avsluttet i CPU-hoppgrensen.
- `/tmp/tpv-diagnostic-probe.cpp`: kilden til det isolerte render-/lagringsprogrammet.

Testprogrammene brukte egne lagringer under `/tmp`. Ingen prosesskjerne er
bevart der. Bygg- og distribusjonsbinæren har samme SHA-256:
`e88cf0fb61dcef01642cd77d0ff4b1ed3e9487e633760da343492874e04a654e`.

## Oppfølging etter brukerens godkjenning

Den opprinnelige krasjbinæren og kopier av cup/replay ble beholdt under
`captures/gpu-diagnostics/snapshot/`. Testene bruker kopiene, og personlige
lagringer ble etterpå sammenlignet med dem ved SHA-256. Både cup og siste replay
er uendret. Det er ikke gjort pakkeoppdateringer eller skrivebordsendringer.

### Maskinvaretester og hva de avklarer

Et eget, eksplisitt startet renderprogram kontrollerer faktisk driver og
shaderkompilator før tegning. Alle testene bruker 1440×900 og en selvstendig
grense på 60 FPS. De starter med 227 bilder fordelt på alle 32 bakker, tre
kameraer og den lagrede replayen. Varighetstestene veksler deretter gjennom alle
32 bakker på 160 sekunder, fulgt av replay på Sweden K140. Her brukes SDLs
offscreen/EGL-backend; vanlig Wayland-presentasjon testes separat.

| Variant | Varighet etter korttesten | Bilder totalt | Utfall |
| --- | --- | --- | --- |
| RadeonSI / LLVM, 4× MSAA, uten ekstra `glFinish` | 300 sekunder | 17 976 | Besto; ingen OpenGL-feil eller registrerte GPU-feil. |
| RadeonSI / ACO, 4× MSAA, `glFinish` etter hvert bilde | 300 sekunder | 17 941 | Besto; ingen OpenGL-feil eller registrerte GPU-feil. |
| RadeonSI / ACO, MSAA avslått, uten ekstra `glFinish` | 180 sekunder | 10 737 | Besto; ingen OpenGL-feil eller registrerte GPU-feil. |

LLVM-testen nådde maksimalt 142 532 KiB residentminne, omtrent 139 MiB.
ACO/synkronisering nådde 115 912 KiB, omtrent 113 MiB. GPU-enheten hadde
opptil 100 prosent rapportert belastning. Maksimal samlet VRAM-bruk var
omtrent 467 MiB og 474 MiB; maksimal GTT-bruk omtrent 1 169 MiB og 967 MiB.
GPU-målingene gjelder hele enheten, inkludert skrivebord og andre programmer,
og er ikke bare spillets minnebruk.

Disse forsøkene er positive stabilitetsdata, men identifiserer ikke ACO som
bekreftet årsak. Ingen ubeskyttet femminutters standard-ACO-test ble kjørt for
å fremprovosere samme systemlås igjen. Testprogrammet har også annen
presentasjon og tidsstyring enn den opprinnelige spillkjøringen. Et stabilt
kort forsøk beviser ikke at en sjelden GPU-feil er borte.

Den tredje varianten nådde maksimalt 116 008 KiB residentminne, omtrent
113 MiB. Alle tre varighetstestene tegnet samlet **46 654 bilder**. De viste
ikke at MSAA må slås av, og kompatibilitetsmodusen beholder derfor vanlig
antialiasing.

Til slutt ble selve spillets fullspilltest startet i et **synlig, vanlig
Wayland-vindu**, med `--graphics-compat`, dummy-lyd og egne testlagringer.
Oppstarten bekreftet RadeonSI/LLVM, **4× MSAA** og synkronisering aktivert.
Alle 32 bakker, grensesnitt, replay og hele 64-hoppscupen besto med
`OpenGL error=0`, exit 0, uten registrerte GPU-feil. Denne automatiske testen
slår av VSync og vanlig FPS-begrensning, slik øvrige smoketester også gjør.
Den erstatter ikke en lengre manuell cup med vanlig presentasjonstiming.

### Endringer i spillet

- Normal kjøring begrenses til 60 FPS, også hvis VSync ikke fungerer.
- `--graphics-compat` velger `AMD_DEBUG=usellvm` før video initialiseres og
  venter på ferdig renderarbeid før hvert swap. Vanlig 4× MSAA beholdes.
- `--sync-rendering` og `--no-msaa` kan brukes hver for seg for videre tester.
- Faktisk driver, antialiasing, VSync-retur og synkronisering skrives og flushes
  ved oppstart, slik at opplysningene ikke forsvinner i stdout-bufferen ved abort.
- Det er lagt til et valgfritt `render-probe`-bygg og en lokal testwrapper som
  lagrer kjernelogg og GPU-målinger, stopper på timeout/GPU-feil og forsøker å
  hente nye GPU-devcoredumps. Wrapperens tidsgrense, avbrudd og feil ved oppstart
  er kontrollert med ufarlige testprosesser.

Shaderflagget settes bare i spillprosessen. Standardkjøringens shaderkompilator
er ikke endret; start med `./play.sh --graphics-compat` for den nye modusen.

### Den separate World Cup-feilen er rettet

Det mislykkede software-smokeforsøket etterlot et gyldig testkontrollpunkt.
Med været og CPU-ferdighetene fra dette kontrollpunktet, og det lagrede
seedet `439484952`, lot en slik feil seg gjenskape uten grafikk. Det nøyaktige
seedet til CPU-hoppet som feilet i første smoketest ble ikke lagret før
simulasjonen; reproduksjonen bruker seedet fra kontrollpunktet:

```text
tikk 340: x=-0.32426071166992188 -> x=0; fortsatt Inrun
tikk 341: x=0 -> x=0.32404327392578125; fortsatt Inrun
etter 20 000 tikk: x=295.76885986328125; fortsatt Inrun
etter ytterligere 20 000 tikk: samme x; fortsatt Inrun
```

Passeringsbetingelsen krevde `beforeX < 0 && x > 0`. Når float32-integrasjonen
traff `x=0` nøyaktig, ble passeringen aldri registrert. Hopperen fortsatte over
hele landingsbakken med inrun-tilstand og kunne ikke fullføre. Det var altså
ikke bare en for lav grense på antall CPU-tikk.

Betingelsen inkluderer nå kanten: `beforeX <= 0 && x > 0`. En regresjonstest
med den faktiske tilstanden feilet på den gamle koden og besto etter endringen.
Det samme kontrollpunktet og 1 999 nærliggende hoppseeds fullførte deretter.
Alle **20 regresjonssuiter besto**, inkludert de opprinnelige fysikk-, pose-,
replay- og fullforløpssammenligningene.

Fullspilltesten ble kjørt på nytt med programvarerendering, kompatibilitetsmodus
og MSAA avslått. Den besto alle 32 bakker, grensesnitt, replay og hele
64-hoppscupen med `OpenGL error=0`. Den tidligere `CPU jump did not finish`
ble ikke utløst i denne kjøringen.

### Gjentakelse av testene

Kilde, byggebrytere og kommandoer er dokumentert i
[DEVELOPMENT.md](DEVELOPMENT.md). Maskinvareloggene ligger lokalt i
`captures/gpu-diagnostics/`, som er ekskludert fra Git. Ingen nye GPU-feil eller
GPU-devcoredumps oppsto under de fullførte testene ovenfor.

Det nye Release-bygget og `dist/ThePerfectV-linux-x86_64.tar.gz` er ferdig
oppdatert. Bygg- og pakkebinæren har samme SHA-256:
`0fbb7f5615806487049124289a7f53e719485b19907f39120128f444f300a4a8`.
Anbefalt start på denne maskinen er:

```sh
./play.sh --graphics-compat
```

GPU-testene er lovende. Den opprinnelige GPU-låsens eksakte kommando og årsak
er fortsatt ikke endelig identifisert, og det er ikke grunnlag for å love at
den aldri vil komme tilbake.
