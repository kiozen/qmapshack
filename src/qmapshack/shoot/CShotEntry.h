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

#ifndef CSHOTENTRY_H
#define CSHOTENTRY_H

#include <optional>

class CAppOpts;
class CMainWindow;

/**
   @brief All main() knows about the documentation subsystem.

   The subsystem is developer only - `shoot/` is compiled and Qt6::Test linked under QMS_DOC_MODE
   alone - so this is the one place that answers for both builds. Without it the three calls are
   inline no-ops the optimizer drops, which keeps main() free of preprocessor branches.
 */
namespace CShotEntry {
#ifdef QMS_DOC_MODE

/// @return true when the options ask for a shoot or a documentation run
bool isDocRun(const CAppOpts& opts);

/// @return false for the run that only shows the panel: its main window is a fixture, not a window
bool showsMainWindow(const CAppOpts& opts);

/**
   @brief Point the tile cache and the workspace database somewhere harmless.

   Both runs read a scratch configuration that knows no maps, and CDiskCache::cleanupRemovedMaps()
   deletes the cache directory of every map the configuration does not know - so without this a run
   wipes the user's tile cache and saves over their workspace. Call before anything reads either.
 */
void prepare(const CAppOpts& opts);

/// @return The process exit code when the run owns the application, nothing when main() carries on
std::optional<int> run(const CAppOpts& opts, CMainWindow& window);

#else

inline bool isDocRun(const CAppOpts&) { return false; }
inline bool showsMainWindow(const CAppOpts&) { return true; }
inline void prepare(const CAppOpts&) {}
inline std::optional<int> run(const CAppOpts&, CMainWindow&) { return std::nullopt; }

#endif
}  // namespace CShotEntry

#endif  // CSHOTENTRY_H
