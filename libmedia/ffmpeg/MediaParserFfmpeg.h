// MediaParserFfmpeg.h: FFMEPG media parsers, for Gnash
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

#ifndef GNASH_MEDIAPARSER_FFMPEG_H
#define GNASH_MEDIAPARSER_FFMPEG_H

#include <memory>
#include <boost/optional.hpp>

#include "MediaParser.h" // for inheritance
#include "ffmpegHeaders.h"
#include "Id3Info.h"

// Forward declaration
namespace gnash {
	class IOChannel;
}

namespace gnash {
namespace media {
namespace ffmpeg {

/// Extra info found in audio stream by the parser.
//
/// The info will be needed for proper initialization of decoder.
///
class ExtraAudioInfoFfmpeg : public AudioInfo::ExtraInfo
{
public:
	ExtraAudioInfoFfmpeg(std::uint8_t* nData, size_t nDataSize)
		:
		data(nData),
		dataSize(nDataSize)
	{
	}
	std::uint8_t* data;
	size_t dataSize;
};

/// Extra info found in video stream by the parser.
//
/// The info will be needed for proper initialization of decoder.
///
class ExtraVideoInfoFfmpeg : public VideoInfo::ExtraInfo
{
public:
	ExtraVideoInfoFfmpeg(std::uint8_t* nData, size_t nDataSize)
		:
		data(nData),
		dataSize(nDataSize)
	{
	}
	std::uint8_t* data;
	size_t dataSize;
};

/// FFMPEG based MediaParser
class MediaParserFfmpeg: public MediaParser
{
public:

	/// Construct a ffmpeg-based media parser for given stream
	//
	/// Can throw a GnashException if input format couldn't be detected
	///
	MediaParserFfmpeg(std::unique_ptr<IOChannel> stream);

	~MediaParserFfmpeg();

	// See dox in MediaParser.h
	virtual bool seek(std::uint32_t&);

	// See dox in MediaParser.h
	virtual bool parseNextChunk();

	// See dox in MediaParser.h
	virtual std::uint64_t getBytesLoaded() const;

    virtual boost::optional<Id3Info> getId3Info() const;

private:

	/// Initialize parser, figuring format and 
	/// creating VideoInfo and AudioInfo objects
	void initializeParser();

	/// Parse next media frame
	//
	/// @return false on error or eof, true otherwise
	///
	bool parseNextFrame();

	/// Input chunk reader, to be called by ffmpeg parser
	int readPacket(std::uint8_t* buf, int buf_size);

	/// ffmpeg callback function
	static int readPacketWrapper(void* opaque, std::uint8_t* buf, int buf_size);

	/// Input stream seeker, to be called by ffmpeg parser
	std::int64_t seekMedia(std::int64_t offset, int whence);

	/// ffmpeg callback function
	static std::int64_t seekMediaWrapper(void *opaque, std::int64_t offset, int whence);

	/// Read some of the input to figure an AVInputFormat
	AVInputFormat* probeStream();

	AVInputFormat* _inputFmt;

	/// the format (mp3, avi, etc.)
	AVFormatContext *_formatCtx;

	/// Index of the video stream in input
	int _videoStreamIndex;

	/// Video input stream
	AVStream* _videoStream;

	/// Index of the audio stream in input
	int _audioStreamIndex;

	// audio
	AVStream* _audioStream;

	/// ?
        AVIOContext* _avIOCxt;

	/// Size of the ByteIO context buffer
	//
	/// This seems to be the size of chunks read
	/// by av_read_frame.
	///
	static const size_t byteIOBufferSize = 1024;

	std::unique_ptr<unsigned char[]> _byteIOBuffer;

	/// The last parsed position, for getBytesLoaded
	std::uint64_t _lastParsedPosition;

	/// Return sample size from SampleFormat
	//
	/// TODO: move somewhere in ffmpeg utils..
	///
	std::uint16_t SampleFormatToSampleSize(AVSampleFormat fmt);

	/// Make an EncodedVideoFrame from an AVPacket and push to buffer
	//
	bool parseVideoFrame(AVPacket& packet);

	/// Make an EncodedAudioFrame from an AVPacket and push to buffer
	bool parseAudioFrame(AVPacket& packet);

    boost::optional<Id3Info> _id3Object;
};


} // gnash.media.ffmpeg namespace 
} // gnash.media namespace 
} // namespace gnash

#endif // __MEDIAPARSER_FFMPEG_H__
