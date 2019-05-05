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

#include "MediaHandlerGst.h"

#include <sstream>

#include "VideoDecoderGst.h"
#include "AudioDecoderGst.h"
#include "MediaParserGst.h"
#include "VideoConverterGst.h"
#include "VideoInputGst.h"
#include "AudioInputGst.h"
#include "FLVParser.h"
#include "MediaParser.h"
#include "GnashFactory.h"

#ifdef DECODING_SPEEX
#include "AudioDecoderSpeex.h"
#endif

#include "IOChannel.h" // for visibility of destructor

#include "MediaParserGst.h"


namespace gnash { 
namespace media {
namespace gst {

std::string
MediaHandlerGst::description() const
{
    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);
    std::ostringstream s;
    s << "Gstreamer " <<  major << "." << minor << "." << micro;
    return s.str();
}

std::unique_ptr<MediaParser>
MediaHandlerGst::createMediaParser(std::unique_ptr<IOChannel> stream)
{
    std::unique_ptr<MediaParser> parser;

    try
    {
        if (isFLV(*stream))
        {
            parser.reset(new FLVParser(std::move(stream)));
        }
        else
        {
            parser.reset(new MediaParserGst(std::move(stream)));
        }
    }
    catch (GnashException& ex)
    {
        log_error("Could not create Gstreamer based media parser for "
                "input stream: %s", ex.what());
        assert(!parser.get());
    }

    return parser;
}

std::unique_ptr<VideoDecoder>
MediaHandlerGst::createVideoDecoder(const VideoInfo& info)
{
    if (info.type != CODEC_TYPE_FLASH) {

        ExtraInfoGst* extrainfo = dynamic_cast<ExtraInfoGst*>(info.extra.get());

        if (!extrainfo) {
            log_error(_("Wrong arguments given to GST VideoDecoder"));
            return std::unique_ptr<VideoDecoder>();
        }
        return std::unique_ptr<VideoDecoder>(
            new VideoDecoderGst(extrainfo->caps));
    }
    videoCodecType format = static_cast<videoCodecType>(info.codec);
    int width = info.width;
    int height = info.height;

    std::uint8_t* extradata = nullptr;
    size_t datasize = 0;

    ExtraVideoInfoFlv* extrainfo = dynamic_cast<ExtraVideoInfoFlv*>(info.extra.get());
    if (extrainfo) {
        extradata = extrainfo->data.get();
        datasize = extrainfo->size;
    }

    std::unique_ptr<VideoDecoder> ret( new VideoDecoderGst(format, width, height, extradata, datasize) );
    return ret;
}

std::unique_ptr<AudioDecoder>
MediaHandlerGst::createAudioDecoder(const AudioInfo& info)
{
    std::unique_ptr<AudioDecoder> ret;

#ifdef DECODING_SPEEX
    if (info.codec == AUDIO_CODEC_SPEEX) {
        assert(info.type == CODEC_TYPE_FLASH);
        ret.reset(new AudioDecoderSpeex);
    } else
#endif
    {
        try {
            ret.reset(new AudioDecoderGst(info));
        }
        catch (const MediaException& ex) {

            if (info.type != CODEC_TYPE_FLASH) throw;

            try {
                ret = createFlashAudioDecoder(info);
            } 
            catch (const MediaException& ex2) {
                boost::format err = boost::format(
                    _("MediaHandlerGst::createAudioDecoder: %s "
                      "-- %s")) % ex.what() % ex2.what();
                throw MediaException(err.str());
            }
        }
    
    }

    return ret;
}

std::unique_ptr<VideoConverter>
MediaHandlerGst::createVideoConverter(ImgBuf::Type4CC srcFormat, ImgBuf::Type4CC dstFormat)
{
    std::unique_ptr<VideoConverter> converter;

    try
    {
        converter.reset(new VideoConverterGst(srcFormat, dstFormat));
    }
    catch (GnashException& ex)
    {
        log_error("Could not create Gstreamer based video converter parser for "
                "input format: %s", ex.what());
    }
    
    return converter;
}

AudioInput*
MediaHandlerGst::getAudioInput(size_t /*index*/)
{
    // FIXME: these should be stored in the media handler, not newly
    // created each time. The documentation is correct, implementation wrong.
    return new AudioInputGst();
}

VideoInput*
MediaHandlerGst::getVideoInput(size_t /*index*/)
{
    // FIXME: these should be stored in the media handler, not newly
    // created each time. The documentation is correct, implementation wrong.
    return new VideoInputGst();
}

void
MediaHandlerGst::cameraNames(std::vector<std::string>& names) const
{
    VideoInputGst::getNames(names);
}

} // gnash.media.gst namespace
} // gnash.media namespace 
} // gnash namespace
