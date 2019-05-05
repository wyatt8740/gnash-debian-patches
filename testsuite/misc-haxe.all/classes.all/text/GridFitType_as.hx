// GridFitType_as.hx:  ActionScript 3 "GridFitType" class, for Gnash.
//
// Generated by gen-as3.sh on: 20090515 by "rob". Remove this
// after any hand editing loosing changes.
//
//   Copyright (C) 2009, 2010 Free Software Foundation, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

// This test case must be processed by CPP before compiling to include the
//  DejaGnu.hx header file for the testing framework support.

#if flash9
import flash.text.GridFitType;
import flash.display.MovieClip;
#else
//import flash.GridFitType;
//import flash.MovieClip;
#end
import flash.Lib;
import Type;
import Std;

// import our testing API
import DejaGnu;

// Class must be named with the _as suffix, as that's the same name as the file.
class GridFitType_as {
    static function main() {

#if !flash9
		DejaGnu.note("The GridFitType class does not exist in versions prior to flash9");
#else

		if (Std.is(GridFitType.NONE, String) && Std.string(GridFitType.NONE) == "none") {
			DejaGnu.pass("GridFitType.NONE property exists");
		} else {
			DejaGnu.fail("GridFitType.NONE property does not exist");
		}
		if (Std.is(GridFitType.PIXEL, String) && Std.string(GridFitType.PIXEL) == "pixel") {
			DejaGnu.pass("GridFitType.PIXEL property exists");
		} else {
			DejaGnu.fail("GridFitType.PIXEL property does not exist");
		}
		if (Std.is(GridFitType.SUBPIXEL, String) && Std.string(GridFitType.SUBPIXEL) == "subpixel") {
			DejaGnu.pass("GridFitType.SUBPIXEL property exists");
		} else {
			DejaGnu.fail("GridFitType.SUBPIXEL property does not exist");
		}
#end
        // Call this after finishing all tests. It prints out the totals.
        DejaGnu.done();
    }
}

// local Variables:
// mode: C++
// indent-tabs-mode: t
// End:
