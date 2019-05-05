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

#include "MovieLoader.h"

#include <memory> 
#include <functional>
#include <algorithm>
#include <thread>

#include "log.h"
#include "MovieFactory.h"
#include "movie_root.h"
#include "DisplayObject.h"
#include "as_value.h"
#include "as_object.h"
#include "movie_definition.h"
#include "Movie.h"
#include "MovieClip.h"
#include "URL.h"
#include "namedStrings.h"
#include "ExecutableCode.h"
#include "RunResources.h"
#include "StreamProvider.h"

//#define GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING 1
//#define GNASH_DEBUG_LOCKING 1

namespace gnash {

MovieLoader::MovieLoader(movie_root& mr)
    :
    _killed(false),
    _movieRoot(mr)
{
}

// private
// runs in loader thread
void
MovieLoader::processRequests()
{
#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
    log_debug("Starting movie loader thread");
#endif


    while (1) {

        // check for shutdown/cancel request
        if (_killed.load() == true) {
#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
            log_debug("Loader thread killed");
#endif
            return;
        }

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processRequests: lock on requests: trying");
#endif

        std::unique_lock<std::mutex> lock(_requestsMutex);

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processRequests: lock on requests: obtained");
#endif

        // Find first non-completed request (the others we'll wait)
        Requests::iterator endIt = _requests.end();
        Requests::iterator it = find_if(_requests.begin(), endIt,
                                        std::bind(&Request::pending,
                                                  std::placeholders::_1));

        if (it == endIt) {

#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
            log_debug("Movie loader thread getting to sleep (nothing more to do)");
#endif
            // all completed, we can get to sleep
            _wakeup.wait(lock);

#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
            log_debug("Movie loader thread waked up");
#endif

#ifdef GNASH_DEBUG_LOCKING
            log_debug("processRequests: lock on requests: release");
#endif

            continue;
        }

        Request& lr = *it;

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processRequests: lock on requests: release");
#endif

        lock.unlock(); // now main thread can continue to push requests

        processRequest(lr);

    }

}

// private
// runs in loader thread
void
MovieLoader::processRequest(Request& r)
{
    const URL& url = r.getURL();
    bool usePost = r.usePost();
    const std::string* postdata = usePost ? &(r.getPostData()) : nullptr;

#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
    log_debug("Movie loader thread processing request for target %s",
        r.getTarget());
#endif

	boost::intrusive_ptr<movie_definition> md (
        MovieFactory::makeMovie(url, _movieRoot.runResources(),
                                nullptr, true, postdata)
    );
    r.setCompleted(md);

#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
    log_debug("Movie loader thread completed request for target %s",
        r.getTarget());
#endif
}

// public
// runs in main thread
void
MovieLoader::clear()
{
    if (_thread.joinable()) {

#ifdef GNASH_DEBUG_LOCKING
        log_debug("clear: lock on requests: trying");
#endif
        std::unique_lock<std::mutex> requestsLock(_requestsMutex);

#ifdef GNASH_DEBUG_LOCKING
        log_debug("clear: lock on requests: obtained");
#endif

#ifdef GNASH_DEBUG_LOCKING
        log_debug("clear: lock on kill: trying");
#endif

#ifdef GNASH_DEBUG_LOCKING
        log_debug("clear: lock on kill: obtained");
#endif

        _killed = true;

#ifdef GNASH_DEBUG_LOCKING
        log_debug("clear: lock on kill: release for kill");
#endif

        log_debug("waking up loader thread");

        _wakeup.notify_all(); // in case it was sleeping

#ifdef GNASH_DEBUG_LOCKING
        log_debug("clear: lock on requests: release after notify_all");
#endif
        requestsLock.unlock(); // allow the thread to die

        log_debug("MovieLoader notified, joining");
        _thread.join();
        log_debug("MovieLoader joined");
    }

    // no thread now, can clean w/out locking
    clearRequests();

#ifdef GNASH_DEBUG_LOCKING
    log_debug("clear: lock on requests: release if not after notify_all");
#endif
}

// private, no locking
// runs in main thread
void
MovieLoader::clearRequests()
{
    _requests.clear();
}

// private 
// runs in main thread
bool
MovieLoader::processCompletedRequest(const Request& r)
{
    //GNASH_REPORT_FUNCTION;

    boost::intrusive_ptr<movie_definition> md;
    if (!r.getCompleted(md)) return false; // not completed yet

    const std::string& target = r.getTarget();
    DisplayObject* targetDO = _movieRoot.findCharacterByTarget(target);
    as_object* handler = r.getHandler();

    if (!md) {

        if (targetDO && handler) {
            // Signal load error
            // Tested not to happen if target isn't found at time of loading
            //

            as_value arg1(getObject(targetDO));

            // FIXME: docs suggest the string can be either "URLNotFound" or
            // "LoadNeverCompleted". This is neither of them:
            as_value arg2("Failed to load movie or jpeg");

            // FIXME: The last argument is HTTP status, or 0 if no connection
            // was attempted (sandbox) or no status information is available
            // (supposedly the Adobe mozilla plugin).
            as_value arg3(0.0);

            callMethod(handler, NSV::PROP_BROADCAST_MESSAGE, "onLoadError",
                    arg1, arg2, arg3);
        }
        return true; // nothing to do, but completed
    }

    const URL& url = r.getURL();

	Movie* extern_movie = md->createMovie(*_movieRoot.getVM().getGlobal());
	if (!extern_movie) {
		log_error(_("Can't create Movie instance "
                    "for definition loaded from %s"), url);
		return true; // completed in any case...
	}

	// Parse query string
	MovieClip::MovieVariables vars;
	url.parse_querystring(url.querystring(), vars);
    extern_movie->setVariables(vars);

    if (targetDO) {
        targetDO->getLoadedMovie(extern_movie);
    }
    else {
        unsigned int levelno;
        const int version = _movieRoot.getVM().getSWFVersion();
        if (isLevelTarget(version, target, levelno)) {
            log_debug("processCompletedRequest: _level loading "
                    "(level %u)", levelno);
	        extern_movie->set_depth(levelno + DisplayObject::staticDepthOffset);
            _movieRoot.setLevel(levelno, extern_movie);
        }
        else {
            log_debug("Target %s of a loadMovie request doesn't exist at "
                        "load complete time", target);
            return true;
        }
    }

    if (handler && targetDO) {
        // Dispatch onLoadStart
        // FIXME: should be signalled before starting to load
        //        (0/-1 bytes loaded/total) but still with *new*
        //        display object as target (ie: the target won't
        //        contain members set either before or after loadClip.
        callMethod(handler, NSV::PROP_BROADCAST_MESSAGE, "onLoadStart",
            getObject(targetDO));

        // Dispatch onLoadProgress
        // FIXME: should be signalled on every readNonBlocking()
        //        with a buffer size of 65535 bytes.
        //
        size_t bytesLoaded = md->get_bytes_loaded();
        size_t bytesTotal = md->get_bytes_total();
        callMethod(handler, NSV::PROP_BROADCAST_MESSAGE, "onLoadProgress",
            getObject(targetDO), bytesLoaded, bytesTotal);

        // Dispatch onLoadComplete
        // FIXME: find semantic of last arg
        callMethod(handler, NSV::PROP_BROADCAST_MESSAGE, "onLoadComplete",
            getObject(targetDO), as_value(0.0));


        // Dispatch onLoadInit
        
        // This event must be dispatched when actions
        // in first frame of loaded clip have been executed.
        //
        // Since getLoadedMovie or setLevel above will invoke
        // construct() and thus queue all actions in first
        // frame, we'll queue the
        // onLoadInit call next, so it happens after the former.
        //
        std::unique_ptr<ExecutableCode> code(
                new DelayedFunctionCall(targetDO, handler,
                    NSV::PROP_BROADCAST_MESSAGE, 
                    "onLoadInit", getObject(targetDO)));

        getRoot(*handler).pushAction(std::move(code), movie_root::PRIORITY_DOACTION);
    }

    return true;

}

// private 
// runs in main thread
void
MovieLoader::processCompletedRequests()
{
    //GNASH_REPORT_FUNCTION;

    for (;;) {

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processCompletedRequests: lock on requests: trying");
#endif

        std::unique_lock<std::mutex> requestsLock(_requestsMutex);

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processCompletedRequests: lock on requests: obtained");
#endif

#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
        log_debug("Checking %d requests for completeness",
            _requests.size());
#endif

        Requests::iterator endIt = _requests.end();
        Requests::iterator it = find_if(_requests.begin(), endIt,
                                        std::bind(&Request::completed,
                                                  std::placeholders::_1));

        // Releases scoped lock.
        if (it == endIt) break;

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processCompletedRequests: lock on requests: releasing");
#endif
        requestsLock.unlock();

        Request& firstCompleted = *it;

#ifdef GNASH_DEBUG_LOADMOVIE_REQUESTS_PROCESSING
        log_debug("Load request for target %s completed",
            firstCompleted->getTarget());
#endif

        bool checkit = processCompletedRequest(firstCompleted);
        assert(checkit);

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processCompletedRequests: lock on requests for removal: "
                    "trying");
#endif

        requestsLock.lock();

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processCompletedRequests: lock on requests for removal: "
                  "obtained")
#endif

        _requests.erase(it);

#ifdef GNASH_DEBUG_LOCKING
        log_debug("processCompletedRequests: lock on requests for removal: "
                    "release");
#endif
    }
}

// public
// runs in main thread
void
MovieLoader::loadMovie(const std::string& urlstr,
                               const std::string& target,
                               const std::string& data,
                               MovieClip::VariablesMethod method,
                               as_object* handler)
{

    /// URL security is checked in StreamProvider::getStream() down the
    /// chain.
    URL url(urlstr, _movieRoot.runResources().streamProvider().baseURL());

    /// If the method is MovieClip::METHOD_NONE, we send no data.
    if (method == MovieClip::METHOD_GET)
    {
        /// GET: append data to query string.
        const std::string& qs = url.querystring();
        std::string varsToSend(qs.empty() ? "?" : "&");
        varsToSend.append(urlstr);
        url.set_querystring(qs + varsToSend);
    }

    log_debug("MovieLoader::loadMovie(%s, %s)", url.str(), target);

    const std::string* postdata = (method == MovieClip::METHOD_POST) ? &data
                                                                     : nullptr;

#ifdef GNASH_DEBUG_LOCKING
    log_debug("loadMovie: lock on requests: trying");
#endif

    std::lock_guard<std::mutex> lock(_requestsMutex);

#ifdef GNASH_DEBUG_LOCKING
    log_debug("loadMovie: lock on requests: obtained");
#endif

    _requests.push_front(
        new Request(url, target, postdata, handler)
    );

    // Start or wake up the loader thread 
    if (!_thread.joinable()) {
        _killed=false;
        _thread = std::thread(std::bind(&MovieLoader::processRequests, this));
    } else {
        log_debug("loadMovie: waking up existing thread");
        _wakeup.notify_all();
    }

#ifdef GNASH_DEBUG_LOCKING
    log_debug("loadMovie: lock on requests: release");
#endif
}

// public
// runs in main thread
MovieLoader::~MovieLoader()
{
    clear(); // will kill the thread
}

// public
// runs in main thread
void
MovieLoader::setReachable() const
{

#ifdef GNASH_DEBUG_LOCKING
    log_debug("setReachable: lock on requests: trying");
#endif

    std::lock_guard<std::mutex> lock(_requestsMutex);

#ifdef GNASH_DEBUG_LOCKING
    log_debug("setReachable: lock on requests: obtained");
#endif

    std::for_each(_requests.begin(), _requests.end(),
            std::mem_fn(&Request::setReachable));

#ifdef GNASH_DEBUG_LOCKING
    log_debug("setReachable: lock on requests: release");
#endif
}


} // namespace gnash
