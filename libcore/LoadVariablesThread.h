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


#ifndef GNASH_LOADVARIABLESTHREAD_H
#define GNASH_LOADVARIABLESTHREAD_H

#include <string>
#include <map>
#include <atomic>
#include <cassert>
#include <memory>
#include <thread>

namespace gnash {
    class StreamProvider;
    class IOChannel;
    class URL;
}

namespace gnash {

// Exception thrown by LoadVariablesThread constructor if unable to connect
// to the stream input.
class NetworkException {};

/// A manager for loadVariable requests
//
/// Provides services for starting a "load and parse" thread, checking
/// its status and getting a parsed variables structure back when done.
///
class LoadVariablesThread
{
public:
	typedef std::map<std::string, std::string> ValuesMap;

	enum class Status : std::int8_t { STARTED = 0, FINISHED, CANCEL_REQUESTED };

	/// Construct a LoadVariablesThread opening a stream for the given URL
	//
	/// Throws a NetworkException if unable.
	///
	/// @param url
	///	URL to post to and fetch from
	///
	LoadVariablesThread(const StreamProvider& sp, const URL& url);

	/// \brief
	/// Construct a LoadVariablesThread opening a stream for the given URL,
	/// posting the given url-encoded data if using HTTP.
	//
	/// Throws a NetworkException if unable.
	///
	/// @param url
	///	URL to post to and fetch from
	///
	/// @param postdata
	///	Url-encoded post data.
	///
	LoadVariablesThread(const StreamProvider& sp, const URL& url,
            const std::string& postdata);

	/// Destroy the LoadVariablesThread, joining the thread if spawned
	~LoadVariablesThread();

	/// Return the name,value map parsed out of the loaded stream
	ValuesMap getValues()
	{
		assert(completed());
		return _vals;
	}

	bool completed()
	{
		return static_cast<Status>(_status.load()) == Status::FINISHED;
	}

private:

	/// Prevent copy
	LoadVariablesThread& operator==(const LoadVariablesThread&); 
	LoadVariablesThread(const LoadVariablesThread&); 

        void startThread(std::unique_ptr<IOChannel> stream);

	/// Load all data from the _stream input.
	//
	/// This function should be run by a separate thread.
	///
	static ValuesMap completeLoad(std::unique_ptr<IOChannel> stream,
				      const std::atomic<int8_t>& status);

	ValuesMap _vals;

	std::thread _thread;
	std::atomic<int8_t> _status; // enum class Status; int for compatibility.
};

} // namespace gnash

#endif // GNASH_LOADVARIABLESTHREAD_H
