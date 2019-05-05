// 
//   Copyright (C) 2005, 2006, 2007, 2009, 2010, 2011, 2012
//      2013 Free Software
//   Foundation, Inc
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

// Test case for NetStream ActionScript class
// compile this test case with Ming makeswf, and then
// execute it like this gnash -1 -r 0 -v out.swf


rcsid="NetStream.as";
#include "check.as"

#if OUTPUT_VERSION < 6

// Added in SWF6; some versions of the pp won't care, but not
// testing anyway.
// Use changes quite significantly from SWF6 (connections to media
// server) to SWF7 (streaming from various sources).

#else // OUTPUT_VERSION >= 6

check_equals(typeof(NetStream), 'function');
check_equals(typeof(NetStream.prototype), 'object');

var netstreamObj = new NetStream;


/* Constructor */

// test the NetStream constuct FAILED: ! NetStream.prototype.hasOwnProperty('bufferLength')or
check_equals ( typeof(netstreamObj), 'object' );


/* 
 * These properties should not yet exist. They may be added  *
 * when the NetStream connection is established.             *
 *                                                           */

check(! NetStream.prototype.hasOwnProperty('currentFps'));
check(! netstreamObj.hasOwnProperty('currentFps'));

check(! NetStream.prototype.hasOwnProperty('bufferLength'));
check(! netstreamObj.hasOwnProperty('bufferLength'));

check(! NetStream.prototype.hasOwnProperty('bufferTime'));
check(! netstreamObj.hasOwnProperty('bufferTime'));

check(! NetStream.prototype.hasOwnProperty('liveDelay'));
check(! netstreamObj.hasOwnProperty('liveDelay'));

check(! NetStream.prototype.hasOwnProperty('time'));
check(! netstreamObj.hasOwnProperty('time'));

/* Added in SWF7 (still apply to SWF6) */
check(! NetStream.prototype.hasOwnProperty('bytesLoaded'));
check(! netstreamObj.hasOwnProperty('bytesLoaded'));

check(! NetStream.prototype.hasOwnProperty('bytesTotal'));
check(! netstreamObj.hasOwnProperty('bytesTotal'));

check( NetStream.prototype.hasOwnProperty('close'));
check(! netstreamObj.hasOwnProperty('close'));

check( NetStream.prototype.hasOwnProperty('pause'));
check(! netstreamObj.hasOwnProperty('pause'));

check( NetStream.prototype.hasOwnProperty('play'));
check(! netstreamObj.hasOwnProperty('play'));


/* Subscriber Methods */

# if OUTPUT_VERSION > 5
// test the NetStream::close method
check_equals ( typeof(netstreamObj.close), 'function' );
// test the NetStream::setBufferTime method
check_equals ( typeof(netstreamObj.setBufferTime), 'function');
# else
// this is verified on at least one pp version for SWF5, but we
// aren't testing it. 
// test the NetStream::close method
check_equals ( typeof(netstreamObj.close), 'undefined' );
// test the NetStream::setBufferTime method
check_equals ( typeof(netstreamObj.setBufferTime), 'undefined');
# endif
check_equals ( typeof(netstreamObj.pause), 'function' );
// test the NetStream::play method
check_equals ( typeof(netstreamObj.play), 'function');
// test the NetStream::seek method
check_equals ( typeof(netstreamObj.seek), 'function' );

// receiveAudio (use with media server)
check_equals ( typeof(netstreamObj.receiveAudio()), 'undefined');
// receiveVideo (use with media server)
check_equals ( typeof(netstreamObj.receiveVideo()), 'undefined');

# if OUTPUT_VERSION == 6
check_equals ( typeof(netstreamObj.setbuffertime), 'function');
# else 
// SWF7 up is case-sensitive !
check_equals ( typeof(netstreamObj.setbuffertime), 'undefined');
# endif 


/* Publisher Methods */

// For use with a media server, in SWF6
// test attachAudio
check_equals ( typeof(netstreamObj.attachAudio()), 'undefined');
// test attachVideo
check_equals ( typeof(netstreamObj.attachVideo()), 'undefined');
// test publish
check_equals ( typeof(netstreamObj.publish()), 'undefined');
// test send
check_equals ( typeof(netstreamObj.send()), 'undefined');


/* Event Handlers */

check_equals(typeof(netstreamObj.onPlayStatus), 'undefined');
netstreamObj.onPlayStatus = 4;
check_equals(typeof(netstreamObj.onPlayStatus), 'number');
netstreamObj.onPlayStatus = "str";
check_equals(typeof(netstreamObj.onPlayStatus), 'string');

check_equals(typeof(netstreamObj.onStatus), 'undefined');
netstreamObj.onStatus = 4;
check_equals(typeof(netstreamObj.onStatus), 'number');
netstreamObj.onStatus = "str";
check_equals(typeof(netstreamObj.onStatus), 'string');

check_equals(typeof(netstreamObj.onCuePoint), 'undefined');
netstreamObj.onCuePoint = 4;
check_equals(typeof(netstreamObj.onCuePoint), 'number');
netstreamObj.onCuePoint = "str";
check_equals(typeof(netstreamObj.onCuePoint), 'string');

check_equals(typeof(netstreamObj.onMetaData), 'undefined');
netstreamObj.onMetaData = 4;
check_equals(typeof(netstreamObj.onMetaData), 'number');
netstreamObj.onMetaData = "str";
check_equals(typeof(netstreamObj.onMetaData), 'string');


/* Properties */

// currentFps (read-only)
check_equals ( typeof(netstreamObj.currentFps), 'undefined' );
netstreamObj.currentFps = 'string';
check_equals ( typeof(netstreamObj.currentFps), 'string' );
netstreamObj.currentFps = false;
check_equals ( typeof(netstreamObj.currentFps), 'boolean' );

// bufferLength (read-only)
check_equals ( typeof(netstreamObj.bufferLength), 'undefined' );
netstreamObj.bufferLength = 'string';

check_equals ( typeof(netstreamObj.bufferLength), 'string' );
netstreamObj.bufferLength = false;
check_equals ( typeof(netstreamObj.bufferLength), 'boolean' );

// bufferTime
check_equals ( typeof(netstreamObj.bufferTime), 'undefined' );
netstreamObj.setBufferTime(10);
check_equals(netstreamObj.bufferTime, NULL);
netstreamObj.bufferTime = 20;
check_equals(netstreamObj.bufferTime, 20);
netstreamObj.setBufferTime = 30;
check_equals(netstreamObj.bufferTime, 20);
netstreamObj.setBufferTime(false);
check_equals(netstreamObj.bufferTime, 20);
netstreamObj.setBufferTime('string');
check_equals(netstreamObj.bufferTime, 20);
netstreamObj.setBufferTime('5');
check_equals(netstreamObj.bufferTime, 20);

// liveDelay (read-only)
check_equals ( typeof(netstreamObj.liveDelay), 'undefined' );
netstreamObj.liveDelay = 'string';
check_equals ( typeof(netstreamObj.liveDelay), 'string' );

// time (read-only)
check_equals ( typeof(netstreamObj.time), 'undefined' );
netstreamObj.time = 'string';
check_equals ( typeof(netstreamObj.time), 'string' );


/* Two properties added in SWF7 */

// bytesLoaded (read-only)
check_equals ( typeof(netstreamObj.bytesLoaded), 'undefined' );
// bytesLoaded (read-only)
check_equals ( typeof(netstreamObj.bytesTotal), 'undefined' );

// See when NetStream object is populated
check(! NetStream.prototype.hasOwnProperty('currentFps'));
nc = { isConnected: true };
netstreamObj = new NetStream(nc);
check(! NetStream.prototype.hasOwnProperty('currentFps'));
nc = new NetConnection();
check(! NetStream.prototype.hasOwnProperty('currentFps'));
netstreamObj = new NetStream(nc);
check(! NetStream.prototype.hasOwnProperty('currentFps'));
nc.connect(null);
check(! NetStream.prototype.hasOwnProperty('currentFps'));
netstreamObj = new NetStream(nc); // Here's when !
check(NetStream.prototype.hasOwnProperty('currentFps')); 
var NetStreamProtoBackup = NetStream.prototype;
NetStream.prototype = {};
netstreamObj = new NetStream(); 
check(!NetStream.prototype.hasOwnProperty('currentFps')); 
netstreamObj = new NetStream(nc); // And prototype is populated again!
check(NetStream.prototype.hasOwnProperty('currentFps')); 

#endif // OUTPUT_VERSION >= 6

#if OUTPUT_VERSION < 6
check_totals(0);
#else
check_totals(74);
#endif
