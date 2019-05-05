// tag_loaders.cpp: SWF tags loaders, for Gnash.
//
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012
//   Free Software Foundation, Inc
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

#ifdef HAVE_CONFIG_H
#include "gnashconfig.h" // HAVE_ZLIB_H, USE_SWFTREE
#endif

#include "tag_loaders.h"

#include <cassert>

#include "utility.h"
#include "log.h"
#include "SWFStream.h"
#include "sprite_definition.h"
#include "SWFMovieDefinition.h"
#include "SWF.h"
#include "swf/TagLoadersTable.h"
#include "URL.h"
#include "GnashException.h"
#include "GnashAlgorithm.h"
#include "sound_definition.h"
#include "SoundInfo.h"
#include "MediaHandler.h"
#include "SimpleBuffer.h"
#include "sound_handler.h"
#include "RunResources.h"

namespace gnash {
namespace SWF {

// Anonymous namespace
namespace {

const std::uint32_t samplerates[] = { 5512, 11025, 22050, 44100 };

} 

// Label the current frame of m with the name from the SWFStream.
void
frame_label_loader(SWFStream& in, TagType tag, movie_definition& m,
		const RunResources& /*r*/)
{
    assert(tag == SWF::FRAMELABEL); // 43

    std::string name;
    in.read_string(name);

    m.add_frame_name(name);

    // FIXME: support SWF6 "named anchors"
    //
    // If SWF version is >= 6 check the byte after terminating NULL
    // if it is 1 this label can be accessed by #name and it's
    // entrance sets the browser URL with anchor appended
    //
    // To avoid relying on SWFStream::tell (see task #5838)
    // we should add a new method to that class
    // (ie: SWFStream::current_tag_length)
    //
    // See server/sample/test_clipping_layer.swf for a testcase.
    //
    size_t end_tag = in.get_tag_end_position();
    size_t curr_pos = in.tell();
    if (end_tag != curr_pos) {
        if (end_tag == curr_pos + 1) {
            log_unimpl(_("anchor-labeled frame not supported"));
        }
        else {
            IF_VERBOSE_MALFORMED_SWF(
            log_swferror(_("frame_label_loader end position %d, "
                       "read up to %d"),
                     end_tag, curr_pos);
            );
        }
    }
}


// Create and initialize a sprite, and add it to the movie.
void
sprite_loader(SWFStream& in, TagType tag, movie_definition& m,
		const RunResources& r)
{
    assert(tag == SWF::DEFINESPRITE); // 39 - DefineSprite

    in.ensureBytes(2);
    const std::uint16_t id = in.read_u16();

    IF_VERBOSE_PARSE(
        log_parse(_("  sprite:  char id = %d"), id);
    );

    // A DEFINESPRITE tag as part of a DEFINESPRITE
    // would be a malformed SWF, anyway to be compatible
    // we should still allow that. See bug #22468.
    IF_VERBOSE_MALFORMED_SWF(
        try {
            dynamic_cast<SWFMovieDefinition&>(m);
        }
        catch (const std::bad_cast&) {
            log_swferror(_("Nested DEFINESPRITE tags. Will add to "
                           "top-level DisplayObjects dictionary."));
        }
    );

    // will automatically read the sprite
    sprite_definition* ch = new sprite_definition(m, in, r, id);

    IF_VERBOSE_MALFORMED_SWF(
        if (!ch->get_frame_count()) {
            log_swferror(_("Sprite %d advertise no frames"), id);
        }
    );

    m.addDisplayObject(id, ch);
}

// Common data

/// Sample rate table for DEFINESOUNDHEAD tags
//
/// The value found in the tag is encoded as 2 bits and
/// represent a multiple of 5512.5.
/// NOTE that the first element of this table lacks the .5
/// portion of the actual value. Dunno what consequences 
/// it could have...

// @@ There are two sets of code to decode/expand/byteswap audio here.
// @@ There should be one (search for ADPCM).

// Load a DefineSound tag.
void
define_sound_loader(SWFStream& in, TagType tag, movie_definition& m,
		const RunResources& r)
{
    assert(tag == SWF::DEFINESOUND); // 14

    sound::sound_handler* handler = r.soundHandler();

    in.ensureBytes(2+4+1+4); // DisplayObject id + flags + sample count

    const std::uint16_t id = in.read_u16();

#ifdef USE_SOUND
    media::audioCodecType format = static_cast<media::audioCodecType>(
            in.read_uint(4));

    std::uint8_t sample_rate_in = in.read_uint(2);

    if (sample_rate_in >= arraySize(samplerates)) {
        IF_VERBOSE_MALFORMED_SWF(
            log_swferror(_("DEFINESOUNDLOADER: sound sample rate %d (expected "
                    "0 to %u)"), +sample_rate_in, arraySize(samplerates));
        );
        sample_rate_in = 0;
    }
    const std::uint32_t sample_rate = samplerates[sample_rate_in];

    const bool sample_16bit = in.read_bit(); 
    const bool stereo = in.read_bit(); 

    const std::uint32_t sample_count = in.read_u32();

    std::int16_t delaySeek = 0;

    if (format == media::AUDIO_CODEC_MP3) {
        in.ensureBytes(2);
        delaySeek = in.read_s16();
    }

    IF_VERBOSE_PARSE(
        log_parse(_("define sound: ch=%d, format=%s, rate=%d, 16=%d, "
            "stereo=%d, ct=%d, delay=%d"), id, format, sample_rate,
            sample_16bit, stereo, sample_count, delaySeek);
    );

    // If we have a sound_handler, ask it to init this sound.

    if (handler) {
        // First it is the amount of data from file,
        // then the amount allocated at *data (it may grow)
        const unsigned dataLength = in.get_tag_end_position() - in.tell();

        // Allocate MediaHandler::getInputPaddingSize() bytes more for the
        // SimpleBuffer 
        size_t allocSize = dataLength;
        media::MediaHandler* mh = r.mediaHandler();
        if (mh) allocSize += mh->getInputPaddingSize();

        std::unique_ptr<SimpleBuffer> data(new SimpleBuffer(allocSize));

        // dataLength is already calculated from the end of the tag, which
        // should be inside the end of the file. TODO: check that this is 
        // the case.
        const unsigned int bytesRead = in.read(
                reinterpret_cast<char*>(data->data()), dataLength);
        data->resize(bytesRead); // in case it's shorter...

        if (bytesRead < dataLength) {
            throw ParserException(_("Tag boundary reported past end of "
                        "SWFStream!"));
        }

        // Store all the data in a SoundInfo object
        const media::SoundInfo sinfo(format, stereo, sample_rate,
                    sample_count, sample_16bit, delaySeek);

        // Stores the sounddata in the soundhandler, and the ID returned
        // can be used to starting, stopping and deleting that sound
        const int handler_id = handler->create_sound(std::move(data), sinfo);

        if (handler_id >= 0) {
            sound_sample* sam = new sound_sample(handler_id, r);
            m.add_sound_sample(id, sam);
        }

    }
    else {
        // is this nice to do?
        log_error(_("There is no sound handler currently active, "
            "so DisplayObject with id %d will not be added to "
            "the dictionary"), id);
    }
#endif	// USE_SOUND
}


void
file_attributes_loader(SWFStream& in, TagType tag, movie_definition& m,
        const RunResources& /*r*/)
{
    assert(tag == SWF::FILEATTRIBUTES); // 69

    struct file_attrs_flags {
        unsigned reserved1;
        bool metadata;
        bool as3;
        unsigned reserved2;
        bool network;
        unsigned reserved3;
    };

    file_attrs_flags flags;

    in.ensureBytes(1 + 3);
    flags.reserved1 = in.read_uint(3);
    flags.metadata = in.read_bit(); 
    flags.as3 = in.read_bit();
    flags.reserved2 = in.read_uint(2);
    flags.network = in.read_bit(); 
    flags.reserved3 = in.read_uint(24);

    IF_VERBOSE_PARSE(
        log_parse(_("File attributes: metadata=%s network=%s"),
              flags.metadata ? _("true") : _("false"),
              flags.network ? _("true") : _("false"))
    );

    if (!flags.network) {
        log_unimpl(_("FileAttributes tag in the SWF requests that "
                "network access is not granted to this movie "
                "(or application?) when loaded from the filesystem. "
                    "Anyway Gnash won't care; "
                "use white/black listing in your .gnashrc instead"));
    }

    if (flags.as3) {
        log_unimpl(_("This SWF file requires AVM2: there will be no "
                    "ActionScript interpretation"));
    }
    else log_debug("This SWF uses AVM1");

    // TODO: - don't allow later FileAttributes tags in the same movie
    //         to override the first one used.
    //       - only use if it is the *first* tag in the SWFStream.

    if (flags.as3) m.setAS3();

}


void
metadata_loader(SWFStream& in, TagType tag, movie_definition& m,
		const RunResources& /*r*/)
{
    assert(tag == SWF::METADATA); 

    // this is supposed to be an XML string
    std::string metadata;
    in.read_string(metadata);

    IF_VERBOSE_PARSE(
        log_parse(_("  RDF metadata (information only): [[\n%s\n]]"),
            metadata);
    );

    // The metadata tag exists exclusively for external
    // description of the SWF file and should be ignored
    // by the SWF player.
    //
    // Note: the presence of metadata should correspond to the
    // file attributes flag hasMetadata; otherwise the SWF
    // is malformed.
    //
    // This should be in RDF format, so should be easy to parse
    // (knowing how well Adobe conform to XML standards...) if
    // it's worth it.
    // See http://www.w3.org/TR/rdf-syntax-grammar/
    log_debug(_("Descriptive metadata from movie %s: %s"),
            m.get_url(), metadata);

#ifdef USE_SWFTREE
    // If the Movie Properties tree is disabled, the metadata
    // is discarded to save parsing time and memory. There seems
    // to be no limit on its length, although you'd have to be
    // malicious or stupid to put really enormous amounts of
    // descriptive metadata in a SWF. There can be one tag for each
    // loaded SWF, however, so it could mount up. 
    m.storeDescriptiveMetadata(metadata);
#endif

}

void
serialnumber_loader(SWFStream& in, TagType tag, movie_definition& /*m*/, 
        const RunResources& /*r*/)
{
    assert(tag == SWF::SERIALNUMBER); // 41

    in.ensureBytes(26);

    const std::uint32_t id = in.read_u32();
    const std::uint32_t edition = in.read_u32();
    const std::uint8_t major = in.read_u8();
    const std::uint8_t minor = in.read_u8();

    const std::uint32_t buildL = in.read_u32();
    const std::uint32_t buildH = in.read_u32();
    const std::uint64_t build =
        (static_cast<std::uint64_t>(buildH) << 32) + buildL;

    const std::uint32_t timestampL = in.read_u32();
    const std::uint32_t timestampH = in.read_u32();
    // This timestamp is number of milliseconds since 1 Jan 1970 (epoch)
    std::uint64_t timestamp =
        (static_cast<std::uint64_t>(timestampH) << 32) + timestampL;

    std::stringstream ss;
    ss << "SERIALNUMBER: Version " << id << "." << edition 
        << "." << +major << "." << +minor;
    ss << " - Build " << build;
    ss << " - Timestamp " << timestamp;

    log_debug("%s", ss.str());

    // attach to movie_definition ?
}

void
reflex_loader(SWFStream& in, TagType tag, movie_definition& /*m*/,
        const RunResources& /*r*/)
{
    assert(tag == SWF::REFLEX); // 777

    in.ensureBytes(3);
    const std::uint8_t first = in.read_u8();
    const std::uint8_t second = in.read_u8();
    const std::uint8_t third = in.read_u8();

    IF_VERBOSE_PARSE(
        log_parse(_("  reflex = \"%c%c%c\""), first, second, third);
    );
}

} // namespace gnash::SWF

// Local Variables:
// mode: C++
// indent-tabs-mode: t
// End:

} // namespace gnash
