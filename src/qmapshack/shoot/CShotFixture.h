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

#ifndef CSHOTFIXTURE_H
#define CSHOTFIXTURE_H

class CShotContext;

/**
   @brief Builds the demo fixture in memory and hands it to the context.

   Deliberately synthesized rather than loaded from committed files: the frozen sample project of
   the plan's section 8 is the highest-risk item and is not what this demo is trying to answer.
   Everything a recipe sees goes through CShotContext' role accessors, so replacing this with
   CGisWorkspace::loadGisProject() later touches no recipe.
 */
namespace CShotFixture {
/// @brief Create the demo project in the workspace and populate the context's role accessors
void build(CShotContext& ctx);
}  // namespace CShotFixture

#endif  // CSHOTFIXTURE_H
