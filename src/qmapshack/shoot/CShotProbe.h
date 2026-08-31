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

#ifndef CSHOTPROBE_H
#define CSHOTPROBE_H

#include <QJsonObject>
#include <QString>

class QDir;

/**
   @brief Throwaway: can the running application be driven by synthesized input alone?

   It answers one question and is meant to be deleted. Today a scenario replays a closed list of
   state kinds, so every kind of picture that is not on that list costs a code change - which is
   the cost curve that makes the whole system a sink. Synthesized input has no such list: a click
   is a click whatever it lands on. Whether that is true of *this* application, headless, is the
   thing nobody can reason their way to.

   So nothing here calls `edit()`, or any other application method that produces the state. It
   clicks, exactly where a writer would, and reports what happened. It also records the axes a
   picture has to be independent of - scale, style, palette, locale, font, platform - because a
   probe that passes only on one machine has answered nothing.
 */
namespace CShotProbe {
/// @return The report; `ok` is false when any step failed
QJsonObject run(const QDir& outDir);
}  // namespace CShotProbe

#endif  // CSHOTPROBE_H
