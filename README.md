# DIY Zigbee senzor hladiny vody pro IBC nádrž (ESP32-C6 + JSN-SR04T)

Návod na stavbu bateriového ultrazvukového senzoru hladiny vody, který posílá data přes Zigbee do Home Assistant (Zigbee2MQTT). Vhodné pro IBC nádrže, sudy, nádrže na dešťovou vodu apod.

Tenhle návod vznikl z reálného ladění celého projektu od nuly – včetně všech problémů, na které jsme narazili, a jak jsme je vyřešili. Pokud narazíš na stejnou chybu, mrkni do sekce **Řešení problémů** níže, možná ti to ušetří hodiny hledání.

---

## Nákupní seznam (BOM)

Všechno dostupné v jednom obchodě – [LaskaKit.cz](https://www.laskakit.cz):

| Součástka | Kód | Poznámka |
|---|---|---|
| LaskaKit ESP32-C6-DEVKit V2 (WiFi 6, Bluetooth 5, Zigbee) | LA100117P | Nízkopříkonová deska s vestavěnou nabíječkou LiPol baterie |
| Vodotěsný ultrazvukový měřič vzdálenosti JSN-SR04T (s kabelem) | LA131064 | Min. měřitelná vzdálenost cca 25 cm |
| PWM MOSFET modul D4184, 40VDC 50A | LA143035 | Spíná napájení senzoru (low-side, N-kanál) |
| GeB LiPol baterie 2800mAh 3,7V JST-PH 2.0 | LA123137 | Nebo podobná, hlavně JST-PH konektor |
| LaskaKit Sada 1000 rezistorů (34 hodnot) | LA180032 | Potřebuješ jen 1kΩ a 2kΩ pro dělič napětí |
| Elektrolytický kondenzátor 470µF, 16V | LA212004I | **Nutné** proti bootloopu při startu Zigbee rádia (viz níže) |
| Voděodolná krabička ABS IP65, 200×150×100mm | – | Propojovací box, 25 pozic |

**Volitelně:** kabelová průchodka do krabičky, oboustranná lepicí páska/suchý zip na upevnění komponent uvnitř.

---

## Jak to funguje – princip

1. ESP32-C6 se probudí z hlubokého spánku (deep sleep).
2. Připojí se k Zigbee síti.
3. Sepne MOSFET modul, který přivede napájení na ultrazvukový senzor.
4. Změří vzdálenost k hladině vody (medián z 5 měření pro stabilitu).
5. Přepočítá vzdálenost na % naplnění nádrže.
6. Odešle hodnotu přes Zigbee do Home Assistant.
7. Jde zpátky spát (typicky na 30 minut) – tím šetří baterii.

---

## Zapojení

| Odkud | Kam |
|---|---|
| Baterie V+ | Svorka **"+"** na MOSFET modulu **a** přímo VCC senzoru (trvalé napájení) |
| Baterie V- | Svorka **"-"** na MOSFET modulu, GND desky |
| GND senzoru | Svorka **LOAD** na MOSFET modulu (spínaná větev!) |
| GPIO10 desky | Pin **PWM** na MOSFET modulu |
| GND (u PWM) na MOSFET modulu | GND desky |
| GPIO12 desky | Pin **Trig/RX** senzoru |
| GPIO13 desky | Přes dělič 1kΩ+2kΩ → pin **Echo/TX** senzoru |
| Baterie (JST-PH) | BAT konektor na desce |

**Dělič napětí (Echo signál je 5V, GPIO snese jen 3,3V):**
Echo senzoru → 1kΩ → (odbočka do GPIO13) → 2kΩ → GND. Je to řada, ne dva rezistory vedle sebe.

⚠️ **Piny 3V3 a VCC na hlavičce této konkrétní desky jsou napájecí VSTUPY, ne výstupy** – nedají se použít k napájení externích věcí! Napájej senzor/MOSFET přímo z vodičů baterie (odbočka), ne z těchto pinů.

⚠️ **Piny IO0, IO1, IO3 na V2 desce nejsou vyvedené** na header – proto používáme IO12, IO13, IO10.

---

## Finální firmware (Arduino)

Firmware je ke stažení jako přiložený soubor [`firmware/water_level_zigbee_sensor.ino`](firmware/water_level_zigbee_sensor.ino).

### Nastavení v Arduino IDE

1. File → Preferences → Additional boards manager URLs:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Tools → Board → Boards Manager → nainstaluj **esp32 by Espressif Systems**
3. Tools nastavení:
   - Board: **ESP32C6 Dev Module**
   - Zigbee mode: **Zigbee ED (end device)**
   - Partition Scheme: **Zigbee 4MB with spiffs**
   - Erase All Flash Before Sketch Upload: **Enabled** (při prvním nahrání/novém párování)

### Klíčové technické rozhodnutí – proč standardní Humidity senzor?

Původně jsme zkoušeli vlastní Zigbee "Analog Input" cluster (přesná procenta a litry jako dvě samostatné hodnoty). Fungovalo to na úrovni Zigbee komunikace, ale **Zigbee2MQTT hodnoty spolehlivě nepublikoval do MQTT** ani s ručně napsaným "external converterem". Řešení: použít **standardní Zigbee typ senzoru** (Humidity/vlhkost, rozsah 0–100 %), který Z2M i Home Assistant rozpoznají automaticky, bez jakéhokoliv custom kódu na straně Z2M. Litry se pak dopočítají v Home Assistant šablonou (% × kapacita nádrže).

---

## Nastavení Home Assistant

### 1. Šablonový senzor pro litry

Nastavení → Zařízení a služby → Pomocníci → Přidat pomocníka → Šablona → Šablonový senzor:

- Název: `Objem vody v nádrži`
- Stavová šablona:
  ```
  {{ (states('sensor.hladinomer_humidity') | float(0) / 100 * 1000) | round(1) }}
  ```
  (uprav `sensor.hladinomer_humidity` a `1000` podle skutečného entity_id a kapacity tvé nádrže)
- Jednotka: `L`

### 2. Grafická karta (vyžaduje HACS + custom kartu tank-card)

```yaml
type: vertical-stack
cards:
  - type: custom:tank-card
    tank_count: 1
    tank_capacity: 1000
    sensor_mode: fill_level_l
    level_sensor: sensor.objem_vody_v_nadrzi
    content_type: water
    unit: L
    tank_form: rect
    bg_color: rgba(0,0,0,0.3)
    show_unittank: true
    entities:
      - name: Nádrž
    title: Nádrž
  - type: gauge
    entity: sensor.hladinomer_humidity
    name: Stav hladiny
    min: 0
    max: 100
    severity:
      red: 0
      yellow: 10
      green: 50
```

Instalace tank-card: HACS → Frontend → Custom repositories → přidej `https://github.com/jinx-22/tank-card`.

---

## Řešení problémů

### "Zigbee se nepodarilo spustit, restartuji..." (bootloop)

**Příčina:** Nedostatečně stabilní napájení – start Zigbee rádia potřebuje krátkou proudovou špičku, kterou baterie/spoje nemusí zvládnout, což ESP32 vyhodnotí jako podpětí (brownout) a restartuje se.

**Řešení:** Připájet (ne jen zkroutit!) elektrolytický kondenzátor 470µF/16V paralelně na napájecí vodiče senzoru/MOSFETu, co nejblíž k desce. **Mechanicky nejisté (zkroucené, nepájené) spoje jsou nejčastější příčina nekonzistentního chování** – propájet je řeší většinu podobných problémů.

### "Nepodarilo se pripojit" / zařízení se nepřipojí k síti

**Příčina:** Známý bug v knihovně `arduino-esp32` Zigbee ([GitHub issue #10644](https://github.com/espressif/arduino-esp32/issues/10644)) – po probuzení z deep sleep se end device nedokáže spolehlivě spojit stejným "rejoin" mechanismem jako jiná Zigbee zařízení, a chová se, jako by potřeboval kompletně nové párování.

**Řešení (dokud není opraveno v knihovně):** Nechat v Zigbee2MQTT trvale zapnuté Permit join (`permit_join: true` v konfiguraci). Zkontrolovat v Boards Manageru, jestli není dostupná novější verze `esp32` balíčku s opravou.

**Update:** Po přechodu na standardní typ senzoru (Humidity, viz níže) se ukázalo, že rejoin funguje spolehlivě i s vypnutým trvalým Permit join – problém byl zřejmě vázaný na kombinaci s vlastním Zigbee clusterem, ne obecně na knihovnu.

### Zařízení se v Zigbee2MQTT ukazuje jako "Nepodporováno" / hodnoty se nepropisují do Home Assistant

**Příčina:** Vlastní Zigbee cluster (Analog Input) vyžaduje "external converter" v Zigbee2MQTT. I s ním nastaveným se ukázalo, že spontánní reporty ze zařízení se nepublikují spolehlivě do MQTT (Z2M je interně zpracuje, ale nepublikuje).

**Řešení:** Nepoužívat vlastní cluster. Použít **standardní Zigbee typ senzoru** (např. `ZigbeeTempSensor` s přidaným `addHumiditySensor()`) – ty Z2M/HA rozpozná a zpracuje automaticky, bez jakéhokoliv converteru.

### JSN-SR04T hlásí "mimo dosah" i s předmětem před senzorem

**Příčina:** JSN-SR04T má **minimální měřitelnou vzdálenost cca 25 cm** – blíž prostě neumí měřit.

**Řešení:** Namontovat senzor tak, aby i při plné nádrži byl od hladiny alespoň 25–30 cm.

### Napětí 0V na senzoru i přes zapojenou baterii

**Příčina:** Piny `3V3`/`VCC` na hlavičce desky LaskaKit ESP32-C6-DEVKit jsou **napájecí vstupy**, ne výstupy (potvrzeno v diskuzi na LaskaKit.cz).

**Řešení:** Napájet senzor/MOSFET přímo z vodičů baterie (odbočka), ne z těchto pinů na desce.

---

## Fyzická montáž

- Senzor namontuj do horního plnicího hrdla IBC nádrže, kolmo dolů, s dostatečnou vzdáleností od hladiny (viz výše).
- Elektroniku (deska + MOSFET modul + baterie) umísti do voděodolné krabičky (IP65+), připevněné na kovové kleci IBC nádrže.
- Kabel od senzoru veď podél klece do krabičky přes kabelovou průchodku.
- Baterii i desku uvnitř krabičky připevni oboustrannou lepicí páskou nebo suchým zipem (ne šrouby do LiPol baterie!).

---

## Poznámka k bezpečnosti baterie

Baterie (LiPol) má z výroby ochranný obvod proti podbití. I tak doporučujeme pravidelně (podle nastaveného intervalu spánku a spotřeby) baterii dobíjet přes USB port označený **"USB"** (ne "UART", ten slouží jen k nahrávání programu).

---

*Tento návod vznikl na základě reálného projektu, včetně všech slepých uliček. Pokud narazíš na problém, který tu není popsaný, zkontroluj především: (1) kvalitu/pájení spojů, (2) přesné GPIO piny podle konkrétní verze tvé desky, (3) zda odpovídá napětí a proudové možnosti napájecího zdroje.*
