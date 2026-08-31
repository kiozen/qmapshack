/**********************************************************************************************
   Copyright (C) 2014 Oliver Eichler <oliver.eichler@gmx.de>

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

#include "setup/CCommandProcessor.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>

CAppOpts* CCommandProcessor::processOptions(const QStringList& arguments) {
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();

  QCommandLineOption nosplashOption(QStringList() << "n" << "no-splash", tr("Do not show splash screen."));
  parser.addOption(nosplashOption);

  QCommandLineOption debugOption(QStringList() << "d" << "debug", tr("Print debug output to console."));
  parser.addOption(debugOption);

  QCommandLineOption logfileOption(QStringList() << "f" << "logfile", tr("Print debug output to logfile."));
  parser.addOption(logfileOption);

  QCommandLineOption configOption(QStringList() << "c" << "config", tr("File with QMapShack configuration."),
                                  tr("file"));
  parser.addOption(configOption);

  QCommandLineOption localeOption(QStringList() << "l" << "locale", tr("Application locale."), tr("code"));
  parser.addOption(localeOption);

  QCommandLineOption fontFamilyOption(QStringList() << "font-family", tr("Application font family."), tr("name"));
  parser.addOption(fontFamilyOption);

  QCommandLineOption fontSizeOption(QStringList() << "font-size", tr("Application font size."), tr("size"));
  parser.addOption(fontSizeOption);

  QCommandLineOption colorSchemeOption(QStringList() << "color-scheme",
                                       tr("Pin the colour scheme instead of following the desktop: light or dark."),
                                       tr("name"));
  parser.addOption(colorSchemeOption);

  QCommandLineOption styleOption(QStringList() << "style",
                                 tr("Qt style.") % "\n" % tr("Available:") % " " % QStyleFactory::keys().join(", ") %
                                     "\n" % tr("Recommended:") % " Fusion",
                                 tr("name"));
  parser.addOption(styleOption);

  // Developer only: the subsystem behind these is compiled under QMS_DOC_MODE alone, so a user's
  // binary must reject them rather than accept a switch that does nothing. The values stay on
  // CAppOpts either way - empty - so no reader of them needs a branch.
  QString shootDir;
  QString shootTask;
  QString shootTarget;
  QString shootScenario;
  QString shootOnly;
  QString docDir;
  QString docChapter;

#ifdef QMS_DOC_MODE
  QCommandLineOption shootOption(QStringList() << "shoot",
                                 tr("Render the documentation images into the given directory."), tr("dir"));
  parser.addOption(shootOption);

  QCommandLineOption shootTaskOption(QStringList() << "shoot-task",
                                     tr("What --shoot does: chapter, list, inspect or explore."), tr("task"),
                                     "chapter");
  parser.addOption(shootTaskOption);

  QCommandLineOption shootTargetOption(
      QStringList() << "shoot-target",
      tr("What the task works on: the chapter file for chapter, the exposure name for inspect and explore."),
      tr("file-or-id"));
  parser.addOption(shootTargetOption);

  QCommandLineOption shootScenarioOption(QStringList() << "shoot-scenario",
                                         tr("Which of the chapter's scenarios to shoot."), tr("name"));
  parser.addOption(shootScenarioOption);

  QCommandLineOption onlyOption(QStringList() << "only", tr("Shoot only the images whose id matches this glob."),
                                tr("glob"));
  parser.addOption(onlyOption);

  QCommandLineOption docOption(QStringList() << "doc",
                               tr("Documentation mode: load the fixture and record shots into this checkout."),
                               tr("dir"));
  parser.addOption(docOption);

  QCommandLineOption docChapterOption(QStringList() << "doc-chapter", tr("Which chapter F9 appends to."), tr("name"),
                                      "scratch");
  parser.addOption(docChapterOption);
#endif

  parser.addPositionalArgument("files", tr("Files for future use."));

  parser.process(arguments);

#ifdef QMS_DOC_MODE
  shootDir = parser.value(shootOption);
  shootTask = parser.value(shootTaskOption);
  shootTarget = parser.value(shootTargetOption);
  shootScenario = parser.value(shootScenarioOption);
  shootOnly = parser.value(onlyOption);
  docDir = parser.value(docOption);
  docChapter = parser.value(docChapterOption);
#endif

  return new CAppOpts(parser.isSet(nosplashOption), parser.isSet(debugOption), parser.isSet(logfileOption),
                      parser.value(configOption), parser.value(localeOption), parser.value(fontFamilyOption),
                      parser.value(fontSizeOption), parser.value(colorSchemeOption), shootDir, shootTask, shootTarget,
                      shootScenario, shootOnly, docDir, docChapter, parser.positionalArguments());
}
