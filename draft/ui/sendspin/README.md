[← Back to main README](../../README.md)

# ui/sendspin

Now-Playing-Kachel für einen lokal laufenden [Sendspin](https://esphome.io/components/sendspin/)-Player:
Titel, Artist, Album und Album-Cover. Feature-Modul, nur `now_playing.yaml` (kein remote-Pendant,
da Sendspin immer auf dem Gerät selbst läuft).

## Voraussetzungen

- `sendspin:`-Hub im Gerät konfiguriert (nicht im Modul enthalten)
- ESPHome 2026.8.0 oder neuer
- PSRAM: jeder Cover-Slot reserviert dauerhaft 2 × `cover_size`² × 2 Byte (200 px ≈ 160 kB)

## Variablen

| Variable      | Required | Description                                                  |
| ------------- | -------- | ------------------------------------------------------------ |
| `uid`         | ✅        | Eindeutiger Präfix für alle IDs                              |
| `row`         | ✅        | Grid-Zeile (0-basiert)                                       |
| `column`      | ✅        | Grid-Spalte (0-basiert)                                      |
| `icon`        | ✅        | MDI-Glyph als Platzhalter ohne Cover, z.B. `$mdi_music`      |
| `row_span`    | —        | Default `1`                                                  |
| `column_span` | —        | Default `1` – empfohlen `2`+, damit Cover und Text passen    |
| `page_id`     | —        | Default `main_page`                                          |
| `cover_size`  | —        | Cover-Kantenlänge in px, Default `200`                       |
| `idle_text`   | —        | Text ohne Wiedergabe, Default `Keine Wiedergabe`             |
| `title_font`  | —        | Default `nunito_36`                                          |
| `artist_font` | —        | Default `$text_font`                                         |
| `album_font`  | —        | Default `nunito_18`                                          |

## Usage

```yaml
now_playing: !include
  file: esphome-modular-lvgl-buttons/ui/sendspin/now_playing.yaml
  vars:
    uid: np
    row: 0
    column: 0
    column_span: 2
    icon: $mdi_music
```

`$mdi_music` (bzw. dein gewähltes Icon) muss im MDI-Font des Geräts als Glyph deklariert sein.

## Notes

- `cover_size` ist die einzige Pixelangabe: Sendspin liefert das Cover serverseitig fest skaliert.
  Für kleine Displays (480×480) eher `140`–`160`, für 10" eher `240`–`300`.
- Lange Titel laufen als Lauftext, Artist/Album werden mit `…` gekürzt.
- Songtitel enthalten oft Umlaute/Sonderzeichen: prüfen, ob die verwendeten Fonts die nötigen Glyphen enthalten.
