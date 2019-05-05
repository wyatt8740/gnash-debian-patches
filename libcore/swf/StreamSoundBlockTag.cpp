//
//   Copyright (C) 2007, 2008, 2009, 2010, 2011, 2012
//   Free Software Foundation, Inc.
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

#include "StreamSoundBlockTag.h"

#include <boost/intrusive_ptr.hpp>
#include <cstdint>

#include "utility.h"
#include "sound_handler.h" 
#include "movie_root.h"
#include "movie_definition.h"
#include "MovieClip.h" // for execute
#include "SoundInfo.h" // for loader
#include "SWFStream.h"
#include "log.h"
#include "RunResources.h"
#include "MediaHandler.h"

namespace gnash {
namespace SWF {

void
StreamSoundBlockTag::executeActions(MovieClip* m, DisplayList& /*dlist*/) const
{

    sound::sound_handler* handler =
        getRunResources(*getObject(m)).soundHandler(); 

#ifdef USE_SOUND
    if (handler) {
        // This makes it possible to stop only the stream when framejumping.
        m->setStreamSoundId(_handler_id);
        handler->playStream(_handler_id, _blockId);
        getRoot(*getObject(m)).setStreamBlock(_handler_id, _blockId);
    }
#endif  // USE_SOUND
}

void
StreamSoundBlockTag::loader(SWFStream& in, TagType tag, movie_definition& m,
        const RunResources& r)
{
    assert(tag == SWF::SOUNDSTREAMBLOCK); 

    sound::sound_handler* handler = r.soundHandler(); 

    // If we don't have a sound_handler registered stop here
    if (!handler) return;

    // Get the ID of the sound stream currently being loaded
    const int sId = m.get_loading_sound_stream_id();

    // Get the SoundInfo object that contains info about the sound stream.
    // Ownership of the object is in the soundhandler
    media::SoundInfo* sinfo = handler->get_sound_info(sId);

    // If there is no SoundInfo something is wrong...
    if (!sinfo) {
        IF_VERBOSE_MALFORMED_SWF(
            log_swferror(_("Found SOUNDSTREAMBLOCK tag w/out preceding "
                "SOUNDSTREAMHEAD"));
        );
        return;
    }

    media::audioCodecType format = sinfo->getFormat();

    std::uint16_t sampleCount;
    std::int16_t seekSamples = 0;

    // MP3 format blocks have additional info
    if (format == media::AUDIO_CODEC_MP3) {
        in.ensureBytes(4);
        // MP3 blocks have restrictions on the number of samples they can
        // contain (due to the codec), so have a variable number of samples
        // per block.
        sampleCount = in.read_u16();
        seekSamples = in.read_u16();
    }
    else sampleCount = sinfo->getSampleCount();

    // Sometimes this will be 0; in this case it serves as a synchronization
    // point only.
    const unsigned int dataLength = in.get_tag_end_position() - in.tell();

    media::MediaHandler* mh = r.mediaHandler();
    const size_t padding = mh ? mh->getInputPaddingSize() : 0;

    // Reserve padding too.
    SimpleBuffer buf(dataLength + padding);
    buf.resize(dataLength);

    const unsigned int bytesRead = in.read((char*)buf.data(), dataLength);
    
    if (bytesRead < dataLength) {
        throw ParserException(_("Tag boundary reported past end of stream!"));
    }

    // Fill the data on the appropiate sound, and receives the starting point
    // for later "start playing from this frame" events.
    //
    // TODO: the amount of sound data used should depend on the sampleCount,
    // not on the size of the data. Currently the sound_handler ignores
    // sampleCount completely.
    sound::sound_handler::StreamBlockId blockId =
        handler->addSoundBlock(std::move(buf), sampleCount, seekSamples, sId);

    boost::intrusive_ptr<ControlTag> s(new StreamSoundBlockTag(sId, blockId));

    m.addControlTag(s); 
}

} // namespace gnash::SWF
} // namespace gnash

// Local Variables:
// mode: C++
// indent-tabs-mode: nil
// End:
