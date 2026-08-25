/**********************************************************************************************
    Copyright (C) 2009 Joerg Wunsch <j@uriah.heep.sax.de>

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
#ifndef CAPPOPTS_H
#define CAPPOPTS_H
/*
 * This class holds the options passed from the command-line,
 * including the positional arguments.
 */

#include <QStringList>

class CAppOpts {
 public:
  const bool nosplash;  // -n, do not display splash screen
  const bool debug;     // -d, print debug messages
  const bool logfile;   // -f, print debug messages to logfile
  const QString configfile;
  const QString locale;
  const QString fontfamily;
  const QString fontsize;
  const QString colorScheme;    // --color-scheme, "light" or "dark"; empty follows the desktop
  const QString shootDir;       // --shoot, output directory; non-empty enters shoot mode
  const QString shootTask;      // --shoot-task, one of build|list|inspect|explore
  const QString shootTarget;    // --shoot-target, the chapter file or the exposure to work on
  const QString shootScenario;  // --shoot-scenario, which of the chapter's scenarios to shoot
  const QString shootOnly;      // --only, the single shot id to take
  const QString docDir;         // --doc, the checkout documentation mode writes into
  const QString docChapter;     // --doc-chapter, which chapter F9 appends to
  const QStringList arguments;

  CAppOpts(bool noSplash, bool doDebug, bool doLogfile, const QString& config, const QString& locale,
           const QString& fontFamily, const QString& fontSize, const QString& colorScheme, const QString& shootDir,
           const QString& shootTask, const QString& shootTarget, const QString& shootScenario, const QString& shootOnly,
           const QString& docDir, const QString& docChapter, const QStringList& args)
      : nosplash(noSplash),
        debug(doDebug),
        logfile(doLogfile),
        configfile(config),
        locale(locale),
        fontfamily(fontFamily),
        fontsize(fontSize),
        colorScheme(colorScheme),
        shootDir(shootDir),
        shootTask(shootTask),
        shootTarget(shootTarget),
        shootScenario(shootScenario),
        shootOnly(shootOnly),
        docDir(docDir),
        docChapter(docChapter),
        arguments(args) {}
};

extern CAppOpts* qlOpts;
#endif  // CAPPOPTS_H
