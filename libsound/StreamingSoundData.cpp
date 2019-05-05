// StreamingSoundData.cpp - embedded sound definition, for gnash
//
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012
//   Free Software Foundation, Inc
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

#include "StreamingSoundData.h"

#include <vector>
#include <cstdint>

#include "SoundInfo.h"
#include "MediaHandler.h" 
#include "log.h"
#include "GnashException.h" 
#include "StreamingSound.h"
#include "utility.h"

namespace gnash {
namespace sound {


size_t
StreamingSoundData::append(SimpleBuffer data,
        size_t sampleCount, int seekSamples)
{
    _buffers.push_back(std::move(data));
    _blockData.emplace_back(sampleCount, seekSamples);
    assert(_blockData.size() == _buffers.size());
    return _buffers.size() - 1;
}

StreamingSoundData::StreamingSoundData(media::SoundInfo info,
        int nVolume)
    :
    soundinfo(std::move(info)),
    volume(nVolume)
{
}

size_t
StreamingSoundData::playingBlock() const
{
    if (_soundInstances.empty()) return 0;
    return static_cast<StreamingSound*>(_soundInstances.front())->currentBlock();
}

void
StreamingSoundData::clearInstances()
{
    std::lock_guard<std::mutex> lock(_soundInstancesMutex);
    _soundInstances.clear();
}

StreamingSoundData::Instances::iterator
StreamingSoundData::eraseActiveSound(Instances::iterator i)
{
    // Mutex intentionally NOT locked...
    return _soundInstances.erase(i);
}

std::unique_ptr<StreamingSound>
StreamingSoundData::createInstance(media::MediaHandler& mh, unsigned long block)
{
    std::unique_ptr<StreamingSound> ret(new StreamingSound(*this, mh, block));

    std::lock_guard<std::mutex> lock(_soundInstancesMutex);

    // Push the sound onto the playing sounds container.
    _soundInstances.push_back(ret.get());

    return ret;
}

StreamingSoundData::~StreamingSoundData()
{
    clearInstances();
}

void
StreamingSoundData::eraseActiveSound(InputStream* inst)
{
    std::lock_guard<std::mutex> lock(_soundInstancesMutex);

    Instances::iterator it = std::find(
            _soundInstances.begin(),
            _soundInstances.end(),
            inst);

    if (it == _soundInstances.end()) {
        log_error("StreamingSoundData::eraseActiveSound: instance %p "
                "not found!", inst);
        return;
    }
    
    eraseActiveSound(it);
}

bool
StreamingSoundData::isPlaying() const
{
    std::lock_guard<std::mutex> lock(_soundInstancesMutex);
    return !_soundInstances.empty();
}

size_t
StreamingSoundData::numPlayingInstances() const
{
    std::lock_guard<std::mutex> lock(_soundInstancesMutex);
    return _soundInstances.size();
}

InputStream*
StreamingSoundData::firstPlayingInstance() const
{
    std::lock_guard<std::mutex> lock(_soundInstancesMutex);
    return _soundInstances.front();
}

void
StreamingSoundData::getPlayingInstances(std::vector<InputStream*>& to) const
{
    std::lock_guard<std::mutex> lock(_soundInstancesMutex);
    for (InputStream* stream : _soundInstances)
    {
        to.push_back(stream);
    }
}

} // gnash.sound namespace 
} // namespace gnash
