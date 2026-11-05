# Coco Emote 👀

A pocket-sized keychain with a face. It blinks, looks around and does cute things by itself. Change its mood from your phone over its own WiFi. No button, no app, no internet needed.

---

## 1. Parts

| Part | Notes | Approx. ₹ |
|---|---|---|
| ESP32-C3 SuperMini | The tiny ESP32-C3 board with USB-C | 400 |
| 1.3" I2C OLED, 128×64 | 4 pins: VCC GND SCL SDA (SH1106 chip) | 470 |
| LiPo battery 3.7 V | Flat cell, 150–300 mAh, **with protection circuit** (e.g. 402030 / 502030) | 135 |
| TP4056 USB-C charger module | The 6-pin one with protection (B+ B– OUT+ OUT–) | 23 |
| Slide switch (SPDT mini) | Power on/off, also how you bring the WiFi page back | 20 |
| Perfboard | Cut to ~50 × 35 mm | 130 |
| Thin wire, solder, Kapton tape | | ~100 |
| Keyring + jump ring | | 60 |

No button needed. (Optional add-on: see §7.)

---

## 2. Wiring

On the SuperMini, the number printed next to each pin **is** the GPIO number used in the code.

| From | To |
|---|---|
| OLED **VCC** | SuperMini **3.3** (3V3) |
| OLED **GND** | SuperMini **G** (GND) |
| OLED **SDA** | SuperMini pin **8** |
| OLED **SCL** | SuperMini pin **9** |

> ⚠️ Check the pin order printed on *your* OLED. Some modules are GND-VCC-SCL-SDA, others VCC-GND-SCL-SDA.

### Power

```
LiPo (+) ──► TP4056 B+
LiPo (–) ──► TP4056 B–

TP4056 OUT+ ──► slide switch ──► SuperMini 5V
TP4056 OUT– ──────────────────► SuperMini G
```

- **Charge** through the TP4056's USB-C port. Its red LED means charging, blue/green means full.
- **Flash code** through the SuperMini's own USB-C port, with the **switch OFF** so USB and the battery don't fight.

---

## 3. Assembly

1. **Lay it out first.** Put the OLED on the left, the SuperMini on the right and the TP4056 underneath or beside it, keeping both USB-C ports at the edge. Mark the perfboard and cut it to size.
2. **Mount the OLED** flat with headers or short wires.
3. **Mount the SuperMini** with its USB-C port at the board edge.
4. **Wire the OLED** (4 wires) as in the table.
5. **Power last.** Battery to TP4056 B+/B–, then OUT+ → switch → 5V, and OUT– → G.
6. **Tape it.** Put Kapton tape over every solder joint behind the board, then stick the battery on the back.
7. **Drill a 3 mm hole** in a free corner for the keyring.

Optional: 3D-print a case, either a tray behind the board or a full shell with a window for the OLED and slots for the switch and charge port.

---

## 4. Flashing the firmware

1. Install **Arduino IDE 2**.
2. **File → Preferences → Additional boards URL:**
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Boards Manager** → install **esp32 by Espressif Systems**.
4. **Library Manager** → install **U8g2** (by olikraus).
5. Open `coco_emote/coco_emote.ino`. `web_page.h` must be in the **same folder**; it opens as a second tab.
6. **Tools → Board → ESP32C3 Dev Module**, and set **USB CDC On Boot → Enabled**.
7. Switch the battery OFF, plug the SuperMini into your computer, and click Upload. If it won't connect: hold **BOOT**, tap **RESET**, release BOOT, then upload again.

If the picture looks shifted or garbled, your OLED is an SSD1306. Swap to the commented `SSD1306` line near the top of the code.

---

## 5. Using it

1. **Flip the switch on.** It says hi, then shows **"Join this WiFi: Coco-Emote-XXXX"**.
2. **Join that WiFi on your phone.** The control page opens by itself. If it doesn't, open **http://192.168.4.1**.
3. Pick a mood, trigger a blink, wink, hop or look, set the brightness, or put it to sleep.

WiFi stays on for **3 minutes** after power-on (longer while you're using the page), then turns itself off, to save battery. The eyes keep going in the mood you picked, and it remembers the mood even when switched off.
**To change mood again:** flip the switch off and on.

### Moods

| Mood | What it does |
|---|---|
| 😌 Chill | Wanders, darts to corners, winks, gets curious (one big eye), random happy hops |
| 😊 Happy | ^ ^ squint eyes, bouncy hops |
| 🥺 Sad | Droopy outer lids, looks down, slow, a tear rolls down |
| 😠 Angry | Slanted inner lids, trembles |
| 😴 Sleepy | Half-closed, slow, nods off, Zzz |
| 😍 Love | Pulsing heart eyes, hops |
| 😮 Surprised | Tall round eyes, barely blinks |

---

## 6. Battery life (rough)

| Mode | Current | 200 mAh lasts |
|---|---|---|
| Eyes on, WiFi off | ~20–25 mA | ~8 h |
| Eyes on, WiFi on | ~70–90 mA | ~2.5 h |
| Switch off | 0 | forever |

---

## 7. Optional: add a button later

A ₹5 tactile button lets you change moods without your phone and adds a real sleep/wake.

- Wire it between SuperMini pin **3** and **G**.
- In the code set `#define HAS_BUTTON 1`.
- **Tap** for the next mood, **hold 1.5 s** for WiFi on/off, **hold 4 s** to sleep (tap to wake).

---

## 8. Before selling

- Use **protected LiPo cells only**, and never leave bare battery wires that could short.
- Add a **case**. Bare perfboard snags on keys and pockets.
- Wireless products sold in India need radio certification (look into **WPC ETA**).
- For volume, move from perfboard to a **custom PCB** with the ESP32-C3, charger IC and switch on one board.

---

## Files

```
coco_emote/
├── coco_emote.ino   firmware (moods, animation, sleep, WiFi page)
├── web_page.h       the phone control page; gets built into the firmware
└── README.md        this guide
```
