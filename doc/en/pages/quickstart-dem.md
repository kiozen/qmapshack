# Elevation data and hillshading

This page shows how to add elevation data to QMapShack, turn it on and shade the terrain with it.

Images: *© OpenStreetMap contributors*; *Datenquelle: Land Tirol - data.tirol.gv.at, CC BY 4.0*.

```{index} single: DEM
```

```{index} single: Elevation; under the mouse pointer
```

## What elevation data does

Elevation data, a DEM (digital elevation model), holds the height of the terrain. QMapShack uses it in three ways:

- It shows the elevation of any point. With a DEM turned on, the status bar at the bottom right shows, as **Ele.**
    and **Slope**, the elevation and slope at the mouse pointer.

    ```{image} ../images/quickstart-dem/status-bar.png
    :class: below-right
    ```

- On request, it adds an elevation to tracks and waypoints, or replaces the one they have. Routes get theirs from it
    when they are calculated.
- It shades the terrain on the map, so that mountains and valleys stand out.

```{index} single: DEM; add
```

```{index} single: DEM; reload
```

## Adding a DEM

Elevation data usually comes as one or several image files, for example GeoTIFF. QMapShack doesn't open them
directly: it reads a `.vrt` file, which lists the image files and joins them into one DEM.

1. Copy the files into the `DEM` folder of your
    [home folder](quickstart-main-window.md#setting-up-the-home-folder): a single file directly into it, several
    files into a subfolder of their own.
2. Click **Tool → VRT Builder**.
3. Click the **...** button beside **Select source files** and select the files you copied.
4. Click the **...** button beside **Target Filename**. Save a name ending in `.vrt` in the `DEM` folder.
5. Click **Start**.
6. ```{image} ../images/quickstart-dem/listed.png
    :align: right
    ```

    Right-click the list in the **Dig. Elev. Model (DEM)** panel and click **Reload DEM**. QMapShack doesn't watch the
    folder, so a new file shows up only after this, or at the next start. The DEM appears in the list, gray like every
    DEM that is off.

A `.vrt` made elsewhere can point to its files by their full path, so it may not find them after a copy. Making it
in the `DEM` folder avoids that.

```{index} single: DEM; turn on
```

## Turning on the DEM

1. ```{image} ../images/quickstart-dem/dem-button.png
    :align: right
    ```

    Click the button on the DEM's row in the **Dig. Elev. Model (DEM)** panel. The row turns bold and says **active**.

2. The map looks the same as before: the DEM shades nothing until you turn on hillshading. But where the DEM has
    data, the status bar now shows, as **Ele.** and **Slope**, the elevation and slope at the mouse pointer. If it
    doesn't, something is wrong with the DEM.

```{index} single: DEM; hillshading
```

```{index} single: Hillshading
```

## Shading the terrain

1. ```{image} ../images/quickstart-dem/hillshading.png
    :align: right
    ```

    Click the arrow left of the DEM's name. Its settings open below it. Check **Hillshading**.

2. ```{image} ../images/quickstart-dem/shaded.png
    :align: right
    ```

    The terrain is now shaded on the map: slopes facing away from the light are darker.

3. The slider below the DEM's name sets its opacity. QMapShack draws the DEM over the map, so dragging the slider
    to the right makes the shading stronger and the map darker. The slider beside **Hillshading** changes the relief
    itself: drag it to the left and slopes look steeper, to the right and the terrain looks flatter. The light always
    comes from the northwest.

    ```{image} ../images/quickstart-dem/stronger.png
    :class: below-right
    ```

Like a map, a DEM can be limited to a range of zoom steps, with the two buttons beside the bar below the slider.
See [Limiting a map to a zoom range](quickstart-zoom-pan.md#limiting-a-map-to-a-zoom-range).
