/**********************************************************************************************
    Copyright (C) 2026 Oliver Eichler <oliver.eichler@gmx.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

**********************************************************************************************/

/**
   The exposure catalog: how to build one widget class from the fixture and the running
   application. Only 9 of the 61 dialogs take nothing but a parent; the rest want a domain object,
   and every one of those is either a fixture item, a singleton alive inside CMainWindow, or a
   result the dialog writes back through a reference.

   An entry costs one line and is paid once per class, never per image. A writer who wants a shot
   of an already-exposed widget needs no developer at all.

   Not exposed, and why: CExportDatabase and CSearchDatabase need a live QSqlDatabase with content,
   CRangeToolSetup exists only while the range mouse mode is active, and CTemplateWidget and
   CShotDocPanel are not user-facing.
 */

#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QUrl>
#include <QWidget>

#include "CAbout.h"
#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "canvas/CCanvasSelect.h"
#include "canvas/CCanvasSetup.h"
#include "dem/CDemPathSetup.h"
#include "gis/CGisDatabase.h"
#include "gis/CGisItemRate.h"
#include "gis/CGisListDB.h"
#include "gis/CGisListWks.h"
#include "gis/CGisWorkspace.h"
#include "gis/CSelDevices.h"
#include "gis/CSetupWorkspace.h"
#include "gis/db/CDBProject.h"
#include "gis/db/CResolveDatabaseConflict.h"
#include "gis/db/CSelectDBFolder.h"
#include "gis/db/CSelectSaveAction.h"
#include "gis/db/CSetupDatabase.h"
#include "gis/db/CSetupFolder.h"
#include "gis/ovl/CDetailsOvlArea.h"
#include "gis/prj/CDetailsPrj.h"
#include "gis/prj/IGisProject.h"
#include "gis/rte/CCreateRouteFromWpt.h"
#include "gis/rte/CDetailsRte.h"
#include "gis/rte/router/brouter/CRouterBRouterInfo.h"
#include "gis/rte/router/routino/CRouterRoutinoPathSetup.h"
#include "gis/search/CGeoSearchConfig.h"
#include "gis/search/CGeoSearchConfigDialog.h"
#include "gis/search/CGeoSearchWebConfigDialog.h"
#include "gis/search/CSearchExplanationDialog.h"
#include "gis/summary/CGisSummary.h"
#include "gis/summary/CGisSummarySetup.h"
#include "gis/trk/CCombineTrk.h"
#include "gis/trk/CCutTrk.h"
#include "gis/trk/CDetailsTrk.h"
#include "gis/trk/CEnergyCyclingDialog.h"
#include "gis/trk/CGisItemTrk.h"
#include "gis/trk/CInvalidTrk.h"
#include "gis/trk/CTrkToAreaDialog.h"
#include "gis/trk/CTrkToRteDialog.h"
#include "gis/wpt/CDetailsGeoCache.h"
#include "gis/wpt/CDetailsWpt.h"
#include "gis/wpt/CGisItemWpt.h"
#include "gis/wpt/CProjWpt.h"
#include "gis/wpt/CSetupIconAndName.h"
#include "grid/CGrid.h"
#include "grid/CGridSetup.h"
#include "grid/CProjWizard.h"
#include "helpers/CElevationDialog.h"
#include "helpers/CInputDialog.h"
#include "helpers/CLinksDialog.h"
#include "helpers/CMapIconSizesSetup.h"
#include "helpers/COverviewAdvisory.h"
#include "helpers/CPhotoViewer.h"
#include "helpers/CPositionDialog.h"
#include "helpers/CProgressDialog.h"
#include "helpers/CSelectCopyAction.h"
#include "helpers/CSelectProjectDialog.h"
#include "helpers/CShortcutConfig.h"
#include "helpers/CShortcutSetupDialog.h"
#include "helpers/CTimeDialog.h"
#include "helpers/CToolBarConfig.h"
#include "helpers/CToolBarSetupDialog.h"
#include "helpers/CVrtAdvisoryDialog.h"
#include "helpers/CWptIconDialog.h"
#include "map/CMapDraw.h"
#include "map/CMapPathSetup.h"
#include "poi/CPoiPathSetup.h"
#include "print/CPrintDialog.h"
#include "print/CScreenshotDialog.h"
#include "realtime/CRtSelectSource.h"
#include "realtime/CRtWorkspace.h"
#include "shoot/CShotContext.h"
#include "shoot/CShotRegistry.h"
#include "units/CCoordFormatSetup.h"
#include "units/CTimeZoneSetup.h"
#include "units/CUnitsSetup.h"
#include "units/IUnit.h"
#include "widgets/CTextEditWidget.h"

namespace {
/**
   @brief A result the dialog writes back through a reference.

   The dialog keeps the reference, so it has to outlive the dialog. One shot is built at a time, so
   one value per type is enough. A dialog taking two of a type takes the second from spare().
 */
template <typename T>
T& scratch() {
  static T value;
  return value;
}

/// @brief The second value of a type, for a dialog that writes back through two references
template <typename T>
T& spare() {
  static T value;
  return value;
}

/// @brief A dock or configuration object of the running application, addressed by its class
template <typename T>
T* live(CShotContext& c) {
  return (nullptr == c.mainWindow()) ? nullptr : c.mainWindow()->findChild<T*>();
}

/// @brief One link, so the link editor has a row to show
QList<IGisItem::link_t>& demoLinks() {
  QList<IGisItem::link_t>& links = scratch<QList<IGisItem::link_t>>();
  if (links.isEmpty()) {
    links << IGisItem::link_t{QUrl("https://www.qmapshack.org"), QObject::tr("The QMapShack project page"), ""};
  }
  return links;
}

/// @brief A VRT that needs its overviews rebuilt, which no fixture holds
COverviewAdvisory::advice_t demoAdvice() {
  COverviewAdvisory::advice_t advice;
  advice.suggestedLevels << 2 << 4 << 8 << 16 << 32;

  COverviewAdvisory::file_info_t weak;
  weak.path = "tiles/n47e011.tif";
  COverviewAdvisory::file_info_t coarse;
  coarse.path = "tiles/n47e012.tif";
  coarse.overviewSizes << 4096 << 2048;
  advice.perFileInfo << weak << coarse;

  advice.diskUsageBytes = 3LL * 1024 * 1024 * 1024;
  advice.diskUsageIsEstimate = true;
  return advice;
}

/// @brief The raster the advisory above is about
COverviewAdvisory::geometry_t demoGeometry() {
  COverviewAdvisory::geometry_t geometry;
  geometry.xsizePx = 40000;
  geometry.ysizePx = 30000;
  geometry.pixelSizeX = 10.0;
  geometry.pixelSizeY = 10.0;
  return geometry;
}

/// @brief Stand-in for a user's photo, so the viewer has something to show
QList<CGisItemWpt::image_t>& demoImages() {
  QList<CGisItemWpt::image_t>& images = scratch<QList<CGisItemWpt::image_t>>();
  if (images.isEmpty()) {
    QImage pixmap(640, 480, QImage::Format_RGB32);
    QPainter p(&pixmap);
    QLinearGradient gradient(0, 0, 0, 480);
    gradient.setColorAt(0, QColor(80, 130, 180));
    gradient.setColorAt(1, QColor(230, 220, 190));
    p.fillRect(pixmap.rect(), gradient);
    p.setPen(Qt::white);
    p.drawText(pixmap.rect(), Qt::AlignCenter, QObject::tr("Demo photo"));
    p.end();

    CGisItemWpt::image_t image;
    image.pixmap = pixmap;
    image.info = QObject::tr("A synthetic photo for the documentation shots.");
    image.fileName = "demo.jpg";
    images << image;
  }
  return images;
}
}  // namespace

// --- takes nothing but a parent ------------------------------------------------------------

SHOT_EXPOSE("About", "The about box", CAbout, [](CShotContext&, QWidget* p) -> QWidget* { return new CAbout(p); });
SHOT_EXPOSE("UnitsSetup", "Unit system and slope display", CUnitsSetup,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CUnitsSetup(p); });
SHOT_EXPOSE("CoordFormatSetup", "Coordinate format", CCoordFormatSetup,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CCoordFormatSetup(p); });
SHOT_EXPOSE("TimeZoneSetup", "Time zone and date format", CTimeZoneSetup,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CTimeZoneSetup(p); });
SHOT_EXPOSE("MapIconSizesSetup", "Icon sizes on the map", CMapIconSizesSetup,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CMapIconSizesSetup(p); });
SHOT_EXPOSE("SearchExplanation", "Workspace search syntax help", CSearchExplanationDialog,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CSearchExplanationDialog(p); });
SHOT_EXPOSE("CutTrk", "Cut track options", CCutTrk,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CCutTrk(p); });
SHOT_EXPOSE("GisItemRate", "Rating and keywords", CGisItemRate,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CGisItemRate(p, {"demo", "documentation"}, 3.5); });
SHOT_EXPOSE("RouterBRouterInfo", "BRouter status and version", CRouterBRouterInfo,
            [](CShotContext&, QWidget*) -> QWidget* {
              CRouterBRouterInfo* dlg = new CRouterBRouterInfo();
              dlg->setLabel(QObject::tr("BRouter"));
              dlg->setInfo(QObject::tr("The information BRouter reports about itself appears here."));
              return dlg;
            });
SHOT_EXPOSE("ProgressDialog", "Progress of a long operation", CProgressDialog,
            [](CShotContext&, QWidget* p) -> QWidget* {
              return new CProgressDialog(QObject::tr("Reading files..."), 0, 100, p);
            });
SHOT_EXPOSE("TextEditWidget", "Rich text editor for descriptions and comments", CTextEditWidget,
            [](CShotContext&, QWidget* p) -> QWidget* {
              return new CTextEditWidget(QObject::tr("<b>A description</b><p>Text, links and images.</p>"), p);
            });

// --- takes a fixture item -------------------------------------------------------------------

SHOT_EXPOSE("DetailsWpt", "Waypoint detail panel", CDetailsWpt,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CDetailsWpt(*c.wpt(), p); });
SHOT_EXPOSE("DetailsTrk", "Track detail panel", CDetailsTrk,
            [](CShotContext& c, QWidget*) -> QWidget* { return new CDetailsTrk(*c.trk()); });
SHOT_EXPOSE("DetailsRte", "Route detail panel", CDetailsRte,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CDetailsRte(*c.rte(), p); });
SHOT_EXPOSE("DetailsArea", "Area detail panel", CDetailsOvlArea,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CDetailsOvlArea(*c.area(), p); });
SHOT_EXPOSE("DetailsPrj", "Project detail panel", CDetailsPrj,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CDetailsPrj(*c.project(), p); });
SHOT_EXPOSE("DetailsGeoCache", "Geocache detail panel", CDetailsGeoCache,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CDetailsGeoCache(*c.wpt(), p); });
SHOT_EXPOSE("ProjWpt", "Project a waypoint by bearing and distance", CProjWpt,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CProjWpt(*c.wpt(), p); });
SHOT_EXPOSE("InvalidTrk", "Invalid track point report", CInvalidTrk,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CInvalidTrk(*c.trk(), p); });
SHOT_EXPOSE("EnergyCyclingDialog", "Cycling energy and power parameters", CEnergyCyclingDialog,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              return new CEnergyCyclingDialog(c.trk()->getEnergyCycling(), p);
            });
SHOT_EXPOSE("CombineTrk", "Combine several tracks into one", CCombineTrk,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CCombineTrk(c.keys(), {}, p); });
SHOT_EXPOSE("CreateRouteFromWpt", "Build a route from waypoints", CCreateRouteFromWpt,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CCreateRouteFromWpt(c.keys(), p); });
SHOT_EXPOSE("SelectCopyAction", "What to do with an item that is already there", CSelectCopyAction,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CSelectCopyAction(c.trk(), c.trk(), p); });
SHOT_EXPOSE("SelectSaveAction", "What to save when database and workspace differ", CSelectSaveAction,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CSelectSaveAction(c.trk(), c.trk(), p); });
SHOT_EXPOSE("ResolveDatabaseConflict", "Which side of a database conflict wins", CResolveDatabaseConflict,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              return new CResolveDatabaseConflict(QObject::tr("The item in the database has changed."), c.trk(),
                                                  scratch<CDBProject::action_e>(), p);
            });
SHOT_EXPOSE("SelDevices", "Which device to copy a project to", CSelDevices,
            [](CShotContext& c, QWidget*) -> QWidget* { return new CSelDevices(c.project(), c.wksList()); });
SHOT_EXPOSE("TrkToRteDialog", "Turn a track into a route", CTrkToRteDialog, [](CShotContext& c, QWidget*) -> QWidget* {
  scratch<IGisProject*>() = c.project();
  scratch<QString>() = QObject::tr("Demo Route");
  return new CTrkToRteDialog(scratch<IGisProject*>(), scratch<QString>(), scratch<bool>());
});
SHOT_EXPOSE("TrkToAreaDialog", "Turn a track into an area", CTrkToAreaDialog,
            [](CShotContext& c, QWidget*) -> QWidget* {
              scratch<IGisProject*>() = c.project();
              scratch<QString>() = QObject::tr("Demo Area");
              return new CTrkToAreaDialog(scratch<IGisProject*>(), scratch<QString>());
            });
SHOT_EXPOSE("SelectProjectDialog", "Which project an item goes into", CSelectProjectDialog,
            [](CShotContext& c, QWidget*) -> QWidget* {
              return new CSelectProjectDialog(scratch<QString>(), spare<QString>(), scratch<IGisProject::type_e>(),
                                              c.wksList());
            });
SHOT_EXPOSE("LinksDialog", "Links attached to an item", CLinksDialog,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CLinksDialog(demoLinks(), p); });
SHOT_EXPOSE("PhotoViewer", "A photo attached to a waypoint", CPhotoViewer,
            [](CShotContext&, QWidget* p) -> QWidget* { return new CPhotoViewer(demoImages(), 0, p); });
SHOT_EXPOSE("TimeDialog", "Edit a timestamp", CTimeDialog,
            [](CShotContext& c, QWidget* p) -> QWidget* { return new CTimeDialog(p, c.trk()->getTimestamp()); });
SHOT_EXPOSE("PositionDialog", "Edit a position", CPositionDialog, [](CShotContext& c, QWidget* p) -> QWidget* {
  scratch<QPointF>() = c.wpt()->getPosition();
  return new CPositionDialog(p, scratch<QPointF>());
});
SHOT_EXPOSE("ElevationDialog", "Edit an elevation", CElevationDialog, [](CShotContext& c, QWidget* p) -> QWidget* {
  scratch<QVariant>() = c.wpt()->getElevation();
  return new CElevationDialog(p, scratch<QVariant>(), QVariant(NOINT), c.wpt()->getPosition());
});
SHOT_EXPOSE("InputDialog", "Edit a single value", CInputDialog, [](CShotContext& c, QWidget* p) -> QWidget* {
  scratch<QVariant>() = c.trk()->getName();
  return new CInputDialog(p, QObject::tr("Name:"), scratch<QVariant>(), QVariant());
});
SHOT_EXPOSE("SetupIconAndName", "Icon and name of a waypoint", CSetupIconAndName,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              scratch<QString>() = c.wpt()->getIconName();
              spare<QString>() = c.wpt()->getName();
              return new CSetupIconAndName(scratch<QString>(), spare<QString>(), p);
            });
SHOT_EXPOSE("WptIconDialog", "Waypoint symbol chooser", CWptIconDialog,
            [](CShotContext& c, QWidget*) -> QWidget* { return new CWptIconDialog(c.mainWindow()); });

// --- takes a singleton alive inside CMainWindow ----------------------------------------------

SHOT_EXPOSE("CanvasSetup", "Canvas projection and scales", CCanvasSetup,
            [](CShotContext& c, QWidget*) -> QWidget* { return new CCanvasSetup(c.canvas()); });
SHOT_EXPOSE("CanvasSelect", "Which canvas an action applies to", CCanvasSelect,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              scratch<CCanvas*>() = c.canvas();
              return new CCanvasSelect(scratch<CCanvas*>(), p);
            });
SHOT_EXPOSE("SetupWorkspace", "Workspace behaviour", CSetupWorkspace, [](CShotContext& c, QWidget* p) -> QWidget* {
  return new CSetupWorkspace(c.workspace(), &CGisDatabase::self(), p);
});
SHOT_EXPOSE("GridSetup", "Grid projection and colour", CGridSetup, [](CShotContext& c, QWidget*) -> QWidget* {
  CCanvas* canvas = c.canvas();
  if (nullptr == canvas) {
    return nullptr;
  }
  CGrid* grid = canvas->findChild<CGrid*>();
  CMapDraw* map = canvas->findChild<CMapDraw*>();
  return (nullptr == grid || nullptr == map) ? nullptr : new CGridSetup(grid, map);
});
SHOT_EXPOSE("ScreenshotDialog", "Save or print a screenshot of the map", CScreenshotDialog,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              return (nullptr == c.canvas()) ? nullptr : new CScreenshotDialog(*c.canvas(), p);
            });
SHOT_EXPOSE("PrintDialog", "Print the map on several pages", CPrintDialog, [](CShotContext& c, QWidget*) -> QWidget* {
  CGisItemTrk* trk = c.trk();
  if (nullptr == c.canvas() || nullptr == trk) {
    return nullptr;
  }
  return new CPrintDialog(CPrintDialog::eTypePrint, trk->getBoundingRect(), c.canvas());
});
SHOT_EXPOSE("ToolBarSetupDialog", "Which buttons the tool bar carries", CToolBarSetupDialog,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              CToolBarConfig* config = live<CToolBarConfig>(c);
              return (nullptr == config) ? nullptr : new CToolBarSetupDialog(p, config);
            });
SHOT_EXPOSE("ShortcutSetupDialog", "Keyboard shortcuts", CShortcutSetupDialog,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              CShortcutConfig* config = live<CShortcutConfig>(c);
              return (nullptr == config) ? nullptr : new CShortcutSetupDialog(p, config);
            });
SHOT_EXPOSE("GeoSearchConfigDialog", "Which geo search service to use", CGeoSearchConfigDialog,
            [](CShotContext& c, QWidget* p) -> QWidget* {
              CGeoSearchConfig* config = live<CGeoSearchConfig>(c);
              return (nullptr == config) ? nullptr : new CGeoSearchConfigDialog(p, config);
            });
SHOT_EXPOSE("GeoSearchWebConfigDialog", "Web services offered on a position", CGeoSearchWebConfigDialog,
            [](CShotContext&, QWidget* p) -> QWidget* {
              return new CGeoSearchWebConfigDialog(scratch<QList<CGeoSearchWeb::service_t>>(), p);
            });
SHOT_EXPOSE("GisSummarySetup", "Which folders the summary watches", CGisSummarySetup,
            [](CShotContext& c, QWidget*) -> QWidget* {
              CGisSummary* summary = live<CGisSummary>(c);
              return (nullptr == summary) ? nullptr : new CGisSummarySetup(*summary);
            });
SHOT_EXPOSE("RtSelectSource", "Add a realtime source", CRtSelectSource, [](CShotContext& c, QWidget*) -> QWidget* {
  CRtWorkspace* wks = live<CRtWorkspace>(c);
  return (nullptr == wks) ? nullptr : new CRtSelectSource(*wks);
});
SHOT_EXPOSE("SetupDatabase", "Add or create a database", CSetupDatabase, [](CShotContext& c, QWidget*) -> QWidget* {
  CGisListDB* list = live<CGisListDB>(c);
  return (nullptr == list) ? nullptr : new CSetupDatabase(*list);
});
SHOT_EXPOSE("SetupFolder", "Name and type of a database folder", CSetupFolder,
            [](CShotContext&, QWidget* p) -> QWidget* {
              scratch<IDBFolder::type_e>() = IDBItem::eTypeGroup;
              scratch<QString>() = QObject::tr("Demo Folder");
              return new CSetupFolder(scratch<IDBFolder::type_e>(), scratch<QString>(), true, p);
            });
SHOT_EXPOSE("SelectDBFolder", "Which database folder to use", CSelectDBFolder,
            [](CShotContext&, QWidget* p) -> QWidget* {
              return new CSelectDBFolder(scratch<QList<quint64>>(), scratch<QString>(), spare<QString>(), p);
            });

// --- takes a path list the dialog writes back -------------------------------------------------

SHOT_EXPOSE("MapPathSetup", "Where map files are looked for", CMapPathSetup, [](CShotContext&, QWidget*) -> QWidget* {
  return new CMapPathSetup(scratch<QStringList>(), scratch<QString>());
});
SHOT_EXPOSE("DemPathSetup", "Where elevation files are looked for", CDemPathSetup,
            [](CShotContext&, QWidget*) -> QWidget* { return new CDemPathSetup(scratch<QStringList>()); });
SHOT_EXPOSE("PoiPathSetup", "Where POI files are looked for", CPoiPathSetup,
            [](CShotContext&, QWidget*) -> QWidget* { return new CPoiPathSetup(scratch<QStringList>()); });
SHOT_EXPOSE("RouterRoutinoPathSetup", "Where Routino databases are looked for", CRouterRoutinoPathSetup,
            [](CShotContext&, QWidget*) -> QWidget* { return new CRouterRoutinoPathSetup(scratch<QStringList>()); });

// --- takes input a fixture cannot hold --------------------------------------------------------

SHOT_EXPOSE("ProjWizard", "Build a projection string", CProjWizard, [](CShotContext&, QWidget*) -> QWidget* {
  // The wizard writes into a line edit it does not own, so one comes with it and dies with it.
  QLineEdit* line = new QLineEdit();
  line->setText("+proj=merc +ellps=WGS84 +datum=WGS84 +units=m +no_defs");
  CProjWizard* dlg = new CProjWizard(*line);
  line->setParent(dlg);
  line->hide();
  return dlg;
});
SHOT_EXPOSE("VrtAdvisoryDialog", "A VRT whose overviews make rendering slow", CVrtAdvisoryDialog,
            [](CShotContext&, QWidget* p) -> QWidget* {
              return new CVrtAdvisoryDialog("alps.vrt", demoAdvice(), demoGeometry(), false, p);
            });
