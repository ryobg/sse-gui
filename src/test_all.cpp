/**
 * @file test_all.cpp
 * @brief Implementing the main test driver
 * @internal
 *
 * This file is part of Skyrim SE GUI mod (aka SSEGUI).
 *
 *   SSEGUI is free software: you can redistribute it and/or modify it
 *   under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   SSEGUI is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with SSEGUI. If not, see <http://www.gnu.org/licenses/>.
 *
 * @endinternal
 *
 * @ingroup Public API
 *
 * @details
 * n / a
 */

#include <sse-gui/sse-gui.h>

//--------------------------------------------------------------------------------------------------

/// Test whether we will crash with nullptr arguments
bool test_ssegui_version ()
{
    int a, m, i;
    const char* b;
    ssegui_version (nullptr, nullptr, nullptr, nullptr);
    ssegui_version (&a, nullptr, nullptr, nullptr);
    ssegui_version (&a, &m, nullptr, nullptr);
    ssegui_version (&a, &a, &a, nullptr);
    ssegui_version (nullptr, &m, &m, &b);
    a = m = i = -1; b = nullptr;
    ssegui_version (&a, &m, &i, &b);
    return a >= 0 && m >= 0 && i >= 0 && b;
}

//--------------------------------------------------------------------------------------------------

int main ()
{
    int ret = 0;
    ret += test_ssegui_version ();
    return ret;
}

//--------------------------------------------------------------------------------------------------

