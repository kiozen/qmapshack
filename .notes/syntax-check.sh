#!/bin/bash
# Syntax check only: no object file, no linking, nothing written. Flags copied from the build.
cd /home/oeichler/projects/qmapshack
# The Qt this build tree was configured with. Checking against whatever Qt is on the include path
# instead reports anything newer than it - QStyleHints::setColorScheme, QPalette::Accent - as
# missing. A self-contained Qt keeps its headers and mkspecs under one prefix; a distribution
# package splits them into /usr/include/<multiarch>/qt6 and /usr/lib/<multiarch>/qt6/mkspecs.
QT_CMAKE=$(sed -n 's|^Qt6_DIR:PATH=||p' build/CMakeCache.txt)
QT_LIB=${QT_CMAKE%/cmake/Qt6}
QT=${QT_LIB%/lib*}
if [ -d "$QT/include/QtCore" ]; then
  QT_INC="$QT/include"
  QT_MKSPECS="$QT/mkspecs"
else
  QT_INC="$QT/include/$(/usr/bin/c++ -dumpmachine)/qt6"
  QT_MKSPECS="$QT_LIB/qt6/mkspecs"
fi
[ -d "$QT_INC/QtCore" ] || { echo "no Qt include tree at ${QT_INC:-<unset>}; is build/ configured?" >&2; exit 2; }
FLAGS=(-DAPPLICATION_NAME=QMapShack -DHAVE_DBUS -DHELPPATH=/usr/local/share/doc/HTML
  -DQT_CORE5COMPAT_LIB -DQT_CORE_LIB -DQT_DBUS_LIB -DQT_GUI_LIB -DQT_HELP_LIB -DQT_NETWORK_LIB
  -DQT_NO_DEBUG -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_POSITIONING_LIB -DQT_PRINTSUPPORT_LIB
  -DQT_QMLINTEGRATION_LIB -DQT_QML_LIB -DQT_QUICK_LIB -DQT_SQL_LIB -DQT_SVGWIDGETS_LIB -DQT_SVG_LIB
  -DQT_UITOOLS_LIB -DQT_WEBCHANNEL_LIB -DQT_WEBENGINECORE_LIB -DQT_WEBENGINEWIDGETS_LIB
  -DQT_WIDGETS_LIB -DQT_XML_LIB -DROUTINO_XML_PATH=/usr/share/routino -DVER_MAJOR=1 -DVER_MINOR=21
  -DVER_STEP=0
  -Ibuild/src/qmapshack -Isrc/qmapshack -Ibuild/src/qmapshack/qmapshack_autogen/include
  -Isrc/qmapshack/. -Isrc/qmapshack/../common
  -isystem build -isystem 3rdparty/GarminFitSdk/cpp -isystem build/_deps/blend2d-src
  -isystem "$QT_INC/QtCore" -isystem "$QT_INC"
  -isystem "$QT_MKSPECS/linux-g++"
  -isystem "$QT_INC/QtWidgets" -isystem "$QT_INC/QtGui"
  -isystem "$QT_INC/QtSvg" -isystem "$QT_INC/QtSvgWidgets"
  -isystem "$QT_INC/QtXml" -isystem "$QT_INC/QtSql"
  -isystem "$QT_INC/QtPrintSupport" -isystem "$QT_INC/QtUiTools"
  -isystem "$QT_INC/QtOpenGLWidgets" -isystem "$QT_INC/QtOpenGL"
  -isystem "$QT_INC/QtNetwork" -isystem "$QT_INC/QtWebEngineWidgets"
  -isystem "$QT_INC/QtWebEngineCore" -isystem "$QT_INC/QtQuick"
  -isystem "$QT_INC/QtQml" -isystem "$QT_INC/QtQmlIntegration"
  -isystem "$QT_INC/QtWebChannel" -isystem "$QT_INC/QtPositioning"
  -isystem "$QT_INC/QtHelp" -isystem "$QT_INC/QtCore5Compat"
  -isystem "$QT_INC/QtDBus"
  -std=gnu++20 -fPIE -Wall -Wpedantic -Wno-switch -Wno-strict-aliasing -fms-extensions
  -Wsuggest-override -Woverloaded-virtual -DBL_STATIC -fsyntax-only)
fail=0
for f in "$@"; do
  out=$(/usr/bin/c++ "${FLAGS[@]}" "$f" 2>&1); [ -n "$out" ] && { echo "$out" | head -40; echo "^^^ $f"; fail=1; }
done
exit $fail
