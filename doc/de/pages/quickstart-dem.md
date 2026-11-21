# Höhendaten und Schummerung

Diese Seite zeigt, wie Sie Höhendaten zu QMapShack hinzufügen, sie einschalten und das Gelände damit schummern.

Bilder: *© OpenStreetMap contributors*; *Datenquelle: Land Tirol - data.tirol.gv.at, CC BY 4.0*.

```{index} single: DEM
```

```{index} single: Höhe; unter dem Mauszeiger
```

## Was Höhendaten leisten

Höhendaten, ein DEM (digitales Höhenmodell), enthalten die Höhe des Geländes. QMapShack nutzt sie auf drei Arten:

- Sie zeigen die Höhe jedes Punktes. Mit eingeschaltetem DEM zeigt die Statusleiste unten rechts als **Höhe** und
    **Hangneigung** die Höhe und die Neigung am Mauszeiger.

    ```{image} ../images/quickstart-dem/status-bar.png
    :class: below-right
    ```

- Auf Wunsch fügen sie Tracks und Wegpunkten eine Höhe hinzu oder ersetzen die vorhandene. Routen bekommen ihre
    Höhe daraus, wenn sie berechnet werden.
- Sie schummern das Gelände auf der Karte, sodass Berge und Täler hervortreten.

```{index} single: DEM; hinzufügen
```

```{index} single: DEM; neu laden
```

## Ein DEM hinzufügen

Höhendaten kommen meist als eine oder mehrere Bilddateien, zum Beispiel GeoTIFF. QMapShack öffnet sie nicht direkt:
Es liest eine `.vrt`-Datei, die die Bilddateien auflistet und zu einem DEM zusammenfügt.

1. Kopieren Sie die Dateien in das Verzeichnis `DEM` Ihres
    [Hauptverzeichnisses](quickstart-main-window.md#das-hauptverzeichnis-einrichten): eine einzelne Datei direkt
    hinein, mehrere Dateien in ein eigenes Unterverzeichnis.
2. Klicken Sie auf **Werkzeug → VRT Builder**.
3. Klicken Sie auf den Knopf **...** neben **Quelldateien auswählen** und wählen Sie die kopierten Dateien aus.
4. Klicken Sie auf den Knopf **...** neben **Zieldatei auswählen**. Speichern Sie einen Namen mit der Endung `.vrt`
    im Verzeichnis `DEM`.
5. Klicken Sie auf **Start**.
6. ```{image} ../images/quickstart-dem/listed.png
    :align: right
    ```

    Klicken Sie mit der rechten Maustaste in die Liste im Bereich **Dig. Höhenmodell (DEM)** und auf
    **DEM erneut laden**. QMapShack beobachtet das Verzeichnis nicht, deshalb erscheint eine neue Datei erst danach
    oder beim nächsten Start. Das DEM steht dann in der Liste, grau wie jedes ausgeschaltete DEM.

Eine anderswo erstellte `.vrt`-Datei kann ihre Dateien mit dem vollen Pfad nennen und findet sie nach einem Kopieren
dann nicht mehr. Wenn Sie sie im Verzeichnis `DEM` erstellen, passiert das nicht.

```{index} single: DEM; einschalten
```

## Das DEM einschalten

1. ```{image} ../images/quickstart-dem/dem-button.png
    :align: right
    ```

    Klicken Sie auf den Knopf in der Zeile des DEM im Bereich **Dig. Höhenmodell (DEM)**. Die Zeile wird fett und
    zeigt **aktiv**.

2. Die Karte sieht aus wie vorher: Das DEM schummert nichts, bis Sie die Schummerung einschalten. Aber wo das DEM
    Daten hat, zeigt die Statusleiste jetzt als **Höhe** und **Hangneigung** die Höhe und die Neigung am Mauszeiger.
    Wenn nicht, stimmt etwas mit dem DEM nicht.

```{index} single: DEM; Schummerung
```

```{index} single: Schummerung
```

## Das Gelände schummern

1. ```{image} ../images/quickstart-dem/hillshading.png
    :align: right
    ```

    Klicken Sie auf den Pfeil links vom Namen des DEM. Seine Einstellungen öffnen sich darunter. Aktivieren Sie
    **Schummerung**.

2. ```{image} ../images/quickstart-dem/shaded.png
    :align: right
    ```

    Das Gelände ist jetzt auf der Karte geschummert: Hänge, die vom Licht abgewandt sind, sind dunkler.

3. Der Schieberegler unter dem Namen des DEM stellt seine Deckkraft ein. QMapShack zeichnet das DEM über die Karte,
    deshalb macht der Schieberegler nach rechts die Schummerung stärker und die Karte dunkler. Der Schieberegler neben
    **Schummerung** ändert das Relief selbst: nach links wirken die Hänge steiler, nach rechts das Gelände flacher.
    Das Licht kommt immer aus Nordwesten.

    ```{image} ../images/quickstart-dem/stronger.png
    :class: below-right
    ```

Wie eine Karte lässt sich ein DEM auf einen Bereich von Zoomstufen begrenzen, mit den zwei Knöpfen neben dem Balken
unter dem Schieberegler. Siehe
[Eine Karte auf einen Zoombereich begrenzen](quickstart-zoom-pan.md#eine-karte-auf-einen-zoombereich-begrenzen).
