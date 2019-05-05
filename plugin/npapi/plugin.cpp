// 
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014,
//   2016 Free Software Foundation, Inc
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


#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include <boost/format.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/find.hpp>
#define BOOST_IOSTREAMS_USE_DEPRECATED
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <cassert>
#include <string>
#include <cstdlib> // getenv
#include <stdlib.h> // putenv
#include <sys/types.h>
#include "GnashSleep.h" // usleep

#if defined(HAVE_WINSOCK_H) && !defined(__OS2__)
# include <winsock2.h>
# include <windows.h>
#else
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <sys/socket.h>
#endif


#define MIME_TYPES_HANDLED  "application/x-shockwave-flash"
// The name must be this value to get flash movies that check the
// plugin version to load.
#define PLUGIN_NAME    "Shockwave Flash"
#define MIME_TYPES_DESCRIPTION  MIME_TYPES_HANDLED ":swf:" PLUGIN_NAME

// Some javascript plugin detectors use the description
// to decide the flash version to display. They expect the
// form (major version).(minor version) r(revision).
// e.g. "8.0 r99."
#define FLASH_VERSION DEFAULT_FLASH_MAJOR_VERSION "." \
    DEFAULT_FLASH_MINOR_VERSION " r" DEFAULT_FLASH_REV_NUMBER "."

#define PLUGIN_DESCRIPTION \
  "Shockwave Flash " FLASH_VERSION "<br>Gnash " VERSION ", the GNU SWF Player. \
  Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011 \
  <a href=\"http://www.fsf.org\">Free \
  Software Foundation</a>, Inc. <br> \
  Gnash comes with NO WARRANTY, to the extent permitted by law. \
  You may redistribute copies of Gnash under the terms of the \
  <a href=\"http://www.gnu.org/licenses/gpl.html\">GNU General Public \
  License</a>. For more information about Gnash, see <a \
  href=\"http://www.gnu.org/software/gnash/\"> \
  http://www.gnu.org/software/gnash</a>. \
  <br>\
  Compatible Shockwave Flash " FLASH_VERSION

#include "plugin.h" 
#include "GnashSystemIOHeaders.h"
#include "StringPredicates.h"
#include "external.h"
#include "callbacks.h"
#if NPAPI_VERSION == 190
#include "npupp.h"
#else
#include "npapi.h"
#include "npruntime.h"
#include "npfunctions.h"
#endif
#include "GnashNPVariant.h"

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <sys/param.h>
#include <csignal>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#include <climits>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

// Macro to prevent repeated logging calls for the same
// event
#define LOG_ONCE(x) { \
    static bool warned = false; \
    if (!warned) { warned = true; x; } \
}

// For scriptable plugin support
#include "pluginScriptObject.h"

extern NPNetscapeFuncs NPNFuncs;
extern NPPluginFuncs NPPFuncs;

namespace gnash {
NPBool plugInitialized = FALSE;
std::string cookiefile;
}

/// \brief Return the MIME Type description for this plugin.
#ifdef NPAPI_CONST
const
#endif
char*
NPP_GetMIMEDescription(void)
{
    return const_cast<char *>(MIME_TYPES_DESCRIPTION);
}

static bool waitforgdb = false;
static bool createSaLauncher = false;

static std::map<int, std::string> cookiemap;

static const char* getPluginDescription();

static const char*
getPluginDescription() 
{
    static const char* desc = nullptr;
    if (!desc) {
        desc = std::getenv("GNASH_PLUGIN_DESCRIPTION");
        if (desc == nullptr) desc = PLUGIN_DESCRIPTION;
    }
    return desc;
}

boost::iostreams::file_descriptor_sink getfdsink(char mkstemplate[]);

boost::iostreams::file_descriptor_sink
getfdsink(char mksTemplate[])
{
  int fd, suffix = std::string(mksTemplate).size() - std::string(mksTemplate).find("XXXXXX") - 6;
#ifdef HAVE_MKSTEMPS
  fd = mkstemps (mksTemplate, suffix);
#else
  if (suffix > 0) {
    char *mksTNoSuff = const_cast<char*>(std::string(mksTemplate).substr(0, std::string(mksTemplate).size() - suffix).c_str());
    fd = mkstemp (mksTNoSuff);
    const char *mksTSuffix = std::string(mksTemplate).substr(std::string(mksTemplate).size() - suffix, suffix).c_str();
    std::stringstream mksTFull;
    mksTFull << mksTNoSuff << mksTSuffix;
    if (rename (mksTNoSuff, mksTFull.str().c_str()) != 0) {
      gnash::log_error("Failed to rename %s", mksTNoSuff);
    }
    strcpy (mksTemplate, mksTFull.str().c_str());
  } else {
    fd = mkstemp (mksTemplate);
  }
#endif
#if BOOST_VERSION < 104400
  boost::iostreams::file_descriptor_sink fdsink(fd, true);
#else
  boost::iostreams::file_descriptor_sink fdsink(fd, boost::iostreams::close_handle);
#endif
  return fdsink;
}

//
// general initialization and shutdown
//

/// \brief Initialize the plugin
///
/// This C++ function gets called once when the plugin is loaded,
/// regardless of how many instantiations there is actually playing
/// movies. So this is where all the one time only initialization
/// stuff goes.
NPError
NS_PluginInitialize()
{

    if ( gnash::plugInitialized ) {
        gnash::log_debug("NS_PluginInitialize called, but ignored (we already initialized)");
        return NPERR_NO_ERROR;
    }

    gnash::log_debug("NS_PluginInitialize call ---------------------------");

    // Browser Functionality Checks

    NPError err = NPERR_NO_ERROR;
    NPBool supportsXEmbed = TRUE;

    // First, check for XEmbed support. The NPAPI Gnash plugin
    // only works with XEmbed, so tell the plugin API to fail if
    // XEmbed is not found.
    err = NPN_GetValue(nullptr,NPNVSupportsXEmbedBool,
                       (void *)&supportsXEmbed);

    if (err != NPERR_NO_ERROR || !supportsXEmbed) {
        gnash::log_error("NPAPI ERROR: No xEmbed support in this browser!");
        return NPERR_INCOMPATIBLE_VERSION_ERROR;
    } else {
        gnash::log_debug("xEmbed supported in this browser");
    }

    // GTK is not strictly required, but we do use the Glib main event loop,
    // so lack of GTK means reduced functionality.
    NPNToolkitType toolkit;
    err = NPN_GetValue(nullptr, NPNVToolkit, &toolkit);

    if (err != NPERR_NO_ERROR || toolkit != NPNVGtk2) {
        gnash::log_error("NPAPI ERROR: No GTK2 support in this browser! Have version %d", (int)toolkit);
    } else {
        gnash::log_debug("GTK2 supported in this browser");
    }

    /*
    Check for environment variables.
    */
    char* opts = std::getenv("GNASH_OPTIONS");
    if (opts != nullptr) {
        gnash::log_debug("GNASH_OPTIONS: %s", opts);
        
        // Should the plugin wait for gdb to be attached?
        if ( strstr(opts, "waitforgdb") ) {
            waitforgdb = true;
        }

        // Should the plugin write a script to invoke
        // the standalone player for debugging ?
        if ( strstr(opts, "writelauncher") ) {
            createSaLauncher = true;
        }

    }

    // Append SYSCONFDIR/gnashpluginrc and ~/.gnashpluginrc to GNASHRC

    std::string newGnashRc;

#if !defined(__OS2__ ) && ! defined(__amigaos4__)
    newGnashRc.append(SYSCONFDIR);
    newGnashRc.append("/gnashpluginrc");
#endif

    const char *home = nullptr;
#if defined(__amigaos4__)
    //on AmigaOS we have a GNASH: assign that point to program dir
    home = "/gnash";
#elif defined(__HAIKU__)
    BPath bp;
    if (B_OK != find_directory(B_USER_SETTINGS_DIRECTORY, &bp))
    {
        std::cerr << "Failed to find user settings directory" << std::endl;
    } else {
        bp.Append("Gnash");
        home = bp.Path();
    }
#else
    home = std::getenv("HOME");
#endif
    if ( home ) {
        newGnashRc.append(":");
        newGnashRc.append(home);
#ifdef __HAIKU__
        newGnashRc.append("/gnashpluginrc");
#else
        newGnashRc.append("/.gnashpluginrc");
#endif
    } else {
        gnash::log_error("WARNING: NPAPI plugin could not find user home dir");
    }

    const char *gnashrc = std::getenv("GNASHRC");
    if ( gnashrc ) {
        newGnashRc.append(":");
        newGnashRc.append(gnashrc);
    }

    if ( setenv("GNASHRC", newGnashRc.c_str(), 1) ) {
        gnash::log_debug("WARNING: NPAPI plugin could not append to the GNASHRC env variable");
    } else {
        gnash::log_debug("NOTE: NPAPI plugin set GNASHRC to %d", newGnashRc);
    }

    /* Success */

    gnash::plugInitialized = TRUE;

    return NPERR_NO_ERROR;
}

/// \brief Shutdown the plugin
///
/// This C++ function gets called once when the plugin is being
/// shutdown, regardless of how many instantiations actually are
/// playing movies. So this is where all the one time only
/// shutdown stuff goes.
void
NS_PluginShutdown()
{
#if 0
    if (!plugInitialized) {
        gnash::log_debug("Plugin already shut down");
        return;
    }

    plugInitialized = FALSE;
#endif
}

/// \brief Retrieve values from the plugin for the Browser
///
/// This C++ function is called by the browser to get certain
/// information is needs from the plugin. This information is the
/// plugin name, a description, etc...
NPError
NS_PluginGetValue(NPPVariable aVariable, void *aValue)
{
    NPError err = NPERR_NO_ERROR;

    switch (aVariable) {
      case NPPVpluginNameString:
          *static_cast<const char **> (aValue) = PLUGIN_NAME;
          break;
          
          // This becomes the description field you see below the opening
          // text when you type about:plugins and in
          // navigator.plugins["Shockwave Flash"].description, used in
          // many flash version detection scripts.
      case NPPVpluginDescriptionString:
          *static_cast<const char **>(aValue) = getPluginDescription();
          break;

      case NPPVpluginWindowBool:
          break;
          
      case NPPVpluginTimerInterval:
          break;
          
      case NPPVpluginKeepLibraryInMemory:
          break;
          
      case NPPVpluginNeedsXEmbed:
#ifdef HAVE_GTK2
          *static_cast<NPBool *>(aValue) = TRUE;
#else
          *static_cast<NPBool *>(aValue) = FALSE;
#endif
          break;

      case NPPVpluginScriptableNPObject:
          break;

#if NPAPI_VERSION != 190
      case NPPVpluginUrlRequestsDisplayedBool:
          break;
      case NPPVpluginWantsAllNetworkStreams:
          break;
#endif
          
      default:
          err = NPERR_INVALID_PARAM;
          break;
    }
    return err;
}

/// \brief construct our plugin instance object
///
/// This instantiates a new object via a C++ function used by the
/// browser.
nsPluginInstanceBase *
NS_NewPluginInstance(nsPluginCreateData * aCreateDataStruct)
{
    // gnash::log_debug(__PRETTY_FUNCTION__);

    if(!aCreateDataStruct) {
        return nullptr;
    }

    return new gnash::nsPluginInstance(aCreateDataStruct);
}

/// \brief destroy our plugin instance object
///
/// This destroys our instantiated object via a C++ function used by the
/// browser.
void
NS_DestroyPluginInstance(nsPluginInstanceBase* aPlugin)
{
    delete static_cast<gnash::nsPluginInstance *> (aPlugin);
}

namespace gnash {

inline bool HasScripting()
{
    return (NPNFuncs.version >= NPVERS_HAS_NPRUNTIME_SCRIPTING);
}


//
// nsPluginInstance class implementation
//

/// \brief Constructor
nsPluginInstance::nsPluginInstance(nsPluginCreateData* data)
    :
    nsPluginInstanceBase(),
    _instance(data->instance),
    _window(0),
    _width(0),
    _height(0),
    _streamfd(-1),
    _childpid(0),
    _filefd(-1),
    _name(),
    _scriptObject(nullptr)
{
    // gnash::log_debug("%s: %x", __PRETTY_FUNCTION__, (void *)this);

    for (size_t i=0, n=data->argc; i<n; ++i) {
        std::string name, val;
        gnash::StringNoCaseEqual noCaseCompare;

        if (data->argn[i]) {
            name = data->argn[i];
        }

        if (data->argv[i]) {
            val = data->argv[i];
        }

        if (noCaseCompare(name, "name")) {
            _name = val;
        }

        _params[name] = val;
    }

    if (HasScripting()) {
        _scriptObject = (GnashPluginScriptObject*) NPN_CreateObject( _instance,
            GnashPluginScriptObject::marshalGetNPClass());
    }
    
    return;
}

gboolean
cleanup_childpid(gpointer data)
{
    int* pid = static_cast<int*>(data);

    int status;
    int rv = waitpid(*pid, &status, WNOHANG);

    if (rv <= 0) {
        // The child process has not exited; it may be deadlocked. Kill it.

        kill(*pid, SIGKILL);
        waitpid(*pid, &status, 0);
    }
 
    gnash::log_debug("Child process exited with status %s", status);

    delete pid;

    return FALSE;
}

/// \brief Destructor
nsPluginInstance::~nsPluginInstance()
{
//    gnash::log_debug("plugin instance destruction");

    if (_scriptObject) {
        NPN_ReleaseObject(_scriptObject);
    }

    do { } while (g_source_remove_by_user_data(this));

    if (_childpid > 0) {
        // When the child has terminated (signaled by GTK through GtkSocket), it
        // remains as a defunct process and we remove it from the kernel table now.
        
        // FIXME: we should ideally do this before the GtkSocket goes away, but
        // after the delete signal has been sent.
        
        // If all goes well, Gnash will already have terminated.
        int status;
        int rv = waitpid(_childpid, &status, WNOHANG);

        if (rv <= 0) {
             int* pid = new int(_childpid);
             gnashSleep(1000);
	     cleanup_childpid(pid);
        } else {
            gnash::log_debug("Child process exited with status %d", status);
        }

        // Remove cookiefile if writelauncher not set
        const char* options = std::getenv("GNASH_OPTIONS");
        if ((!options || !strstr(options, "writelauncher")) &&
            (!cookiefile.empty())) {
            std::map<int, std::string>::iterator it = cookiemap.find(_childpid);
            if (it != cookiemap.end()) {
                if (remove(it->second.c_str())) {
                    gnash::log_error("Cookiefile %s removal failed [pid %d]",
                        it->second, _childpid);
                } else {
                    gnash::log_debug("Cookiefile %s removed [pid %d]",
                        it->second, _childpid);
                }
                cookiemap.erase(it);
            }
        }
    }
    _childpid = 0;
}

/// \brief Initialize an instance of the plugin object
/// 
/// This methods initializes the plugin object, and is called for
/// every movie that gets played. This is where the movie playing
/// specific initialization goes.
NPBool
nsPluginInstance::init(NPWindow* aWindow)
{
    if(!aWindow) {
        gnash::log_error("%s: ERROR: Window handle was bogus!", __PRETTY_FUNCTION__);
        return FALSE;
    } else {
#if GNASH_PLUGIN_DEBUG > 1
        std::cout << "X origin: = " << aWindow->x 
            << ", Y Origin = " << aWindow->y
            << ", Width = " << aWindow->width
            << ", Height = " << aWindow->height
            << ", WindowID = " << aWindow->window
            << ", this = " << static_cast<void*>(this) << std::endl;
#endif
    }

    return TRUE;
}

/// \brief Shutdown an instantiated object
///
/// This shuts down an object, and is called for every movie that gets
/// played. This is where the movie playing specific shutdown code
/// goes.
void
nsPluginInstance::shut()
{
    gnash::log_debug("Gnash plugin shutting down");

    if (_streamfd != -1) {
        if (close(_streamfd) == -1) {
            perror("closing _streamfd");
        } else {
            _streamfd = -1;
        }
    }

}
/// \brief Set the window to be used to render in
///
/// This sets up the window the plugin is supposed to render
/// into. This calls passes in various information used by the plugin
/// to setup the window. This may get called multiple times by each
/// instantiated object, so it can't do much but window specific
/// setup here.
NPError
nsPluginInstance::SetWindow(NPWindow* aWindow)
{
    if(!aWindow) {
        gnash::log_error(std::string(__FUNCTION__) + ": ERROR: Window handle was bogus!");
        return NPERR_INVALID_PARAM;
#if 0
    } else {
        gnash::log_debug("%s: X origin = %d, Y Origin = %d, Width = %d,"
            " Height = %d, WindowID = %p, this = %p",
            __FUNCTION__,
            aWindow->x, aWindow->y, aWindow->width, aWindow->height,
            aWindow->window, this);
#endif
    }

    if (_window) {
        return NPERR_GENERIC_ERROR;
    }

    _width = aWindow->width;
    _height = aWindow->height;

    _window = reinterpret_cast<Window> (aWindow->window);

    // When testing the interface to the plugin, don't start the player
    // as a debug client "nc -l 1111" is used instead.
    if (!_childpid && !_swf_url.empty()) {
        return startProc();
    }

    return NPERR_NO_ERROR;
}

NPError
nsPluginInstance::GetValue(NPPVariable aVariable, void *aValue)
{

    if (aVariable == NPPVpluginScriptableNPObject) {
        if (_scriptObject) {
            void **v = (void **)aValue;
            NPN_RetainObject(_scriptObject);
            *v = _scriptObject;
        } else {
            gnash::log_debug("_scriptObject is not assigned");
        }
    }    

    // log_debug("SCRIPT OBJECT getValue: %x, ns: %x", (void *)_scriptObject, (void *)this);

    return NS_PluginGetValue(aVariable, aValue);
}

/// \brief Open a new data stream
///
/// Opens a new incoming data stream, which is the flash movie we want
/// to play.
/// A URL can be pretty ugly, like in this example:
/// http://www.shockwave.com/swf/navbar/navbar_sw.swf?atomfilms=http%3a//www.atomfilms.com/af/home/&shockwave=http%3a//www.shockwave.com&gameblast=http%3a//gameblast.shockwave.com/gb/gbHome.jsp&known=0
/// ../flash/gui.swf?ip_addr=foobar.com&ip_port=3660&show_cursor=true&path_prefix=../flash/&trapallkeys=true"
///

NPError
nsPluginInstance::NewStream(NPMIMEType /*type*/, NPStream* stream,
                            NPBool /*seekable*/, uint16_t* /*stype*/)
{
    // gnash::log_debug("%s: %x", __PRETTY_FUNCTION__, (void *)this);

    if (_childpid) {
        // Apparently the child process has already been started for this
        // plugin instance. It is puzzling that this method gets called
        // again. Starting a new process for the same movie will cause
        // problems later, so we'll stop here.
        return NPERR_GENERIC_ERROR;
    }
    _swf_url = stream->url;

    if (!_swf_url.empty() && _window) {
        return startProc();
    }

    return NPERR_NO_ERROR;
}

/// \brief Destroy the data stream we've been reading.
NPError
nsPluginInstance::DestroyStream(NPStream* /*stream*/, NPError /*reason*/)
{
    if (_streamfd != -1) {
        if (close(_streamfd) == -1) {
            perror("closing _streamfd");
        } else {
            _streamfd = -1;
        }
    }

    return NPERR_NO_ERROR;
}

/// \brief Return how many bytes we can read into the buffer
int32_t
nsPluginInstance::WriteReady(NPStream* /* stream */ )
{
    //gnash::log_debug("Stream for %s is ready", stream->url);
    if ( _streamfd != -1 ) {
	return 1024;
    } else {
	return 0;
    }
}

/// \brief Read the data stream from Mozilla/Firefox
///
int32_t
nsPluginInstance::Write(NPStream* /*stream*/, int32_t /*offset*/, int32_t len,
        void* buffer)
{
    int written = write(_streamfd, buffer, len);
    return written;
}

bool
nsPluginInstance::handlePlayerRequestsWrapper(GIOChannel* iochan,
        GIOCondition cond, nsPluginInstance* plugin)
{
    return plugin->handlePlayerRequests(iochan, cond);
}

bool
nsPluginInstance::handlePlayerRequests(GIOChannel* iochan, GIOCondition cond)
{
    // gnash::log_debug("%s: %d: %x", __PRETTY_FUNCTION__, __LINE__, (void *)this);

    if ( cond & G_IO_HUP ) {
        gnash::log_debug("Player control socket hang up");
        return false;
    }

    assert(cond & G_IO_IN);

    gnash::log_debug("Checking player requests on FD #%d",
              g_io_channel_unix_get_fd(iochan));

    const size_t buf_size = 1;
    gchar buffer[buf_size];

    do {
        GError* error = nullptr;
        gsize bytes_read = 0;

        GIOStatus status = g_io_channel_read_chars(iochan, buffer, buf_size,
                                                   &bytes_read, &error);

        switch (status) {
          case G_IO_STATUS_ERROR:
              gnash::log_error("error reading request line: %s",
                               error ? error->message : "unspecified error");
              g_error_free(error);
              return false;
          case G_IO_STATUS_EOF:
              gnash::log_error("EOF (error: %s)", 
                               error ? error->message : "unspecified error");
              g_error_free(error);
              return false;
          case G_IO_STATUS_AGAIN:
              gnash::log_debug("read again");
              continue;
          case G_IO_STATUS_NORMAL:
              // process request
              _requestbuf.append(buffer, buffer+bytes_read);
#if 0
              gnash::log_debug("Normal read: %s", std::string(buffer, buffer+bytes_read));
#endif
              break;
          default:
              gnash::log_error("Abnormal status!");
              return false;
        }
    } while (g_io_channel_get_buffer_condition(iochan) & G_IO_IN);

    // process request..
    processPlayerRequest();
    
    return true;
}

// This GIOFunc handler removes the source from the mainloop.
gboolean 
remove_handler(GIOChannel*, GIOCondition, gpointer)
{
    return FALSE;
}


// There may be multiple Invoke messages in a single packet, so each
// packet needs to be broken up into separate messages to be parsed.
bool
nsPluginInstance::processPlayerRequest()
{
#if 0
     gnash::log_debug(__PRETTY_FUNCTION__);

     log_debug("SCRIPT OBJECT %d: %x", __LINE__, this->getScriptObject());
#endif

    if ( _requestbuf.size() < 4 ) {
        gnash::log_error("Invalid player request (too short): %s", _requestbuf);
        return false;
    }


    std::string& packet = _requestbuf;
    do {
        boost::trim_left(packet);
        if (packet.empty()) {
            return false;
        }

        std::string term = "</invoke>";
        std::string::size_type pos = packet.find(term);
        // no terminator, bad message
        if (pos == std::string::npos) {
            gnash::log_debug("Incomplete Invoke message. Probably a fragment.");
            return false;
        }
         
        // Extract a message from the packet
        std::string msg = packet.substr(0, pos + term.size());
        std::shared_ptr<plugin::ExternalInterface::invoke_t> invoke =
            plugin::ExternalInterface::parseInvoke(_scriptObject, msg);

        // drop the parsed message from the packet
        packet.erase(0, msg.size());
        
        if (!invoke) {
            log_error("Failed to parse invoke message: %s", msg);
            return false;
        }

        if (!invoke->name.empty()) {
            gnash::log_debug("Requested method is: %s", invoke->name);
        } else {
            gnash::log_error("Invoke request missing a name to invoke.");
            continue;
        }
        
        if (invoke->name == "getURL") {
            
            assert(invoke->args.size() > 1);
            
            // The first argument is the URL string.
            std::string url = NPVariantToString(invoke->args[0].get());
#if 0
            gnash::log_debug("Got a getURL() request: %s", url);
#endif
            // The second is the method, namely GET or POST.
            std::string op = NPVariantToString(invoke->args[1].get());
            // The third is the optional target, which is something like
            // _blank or _self.
            std::string target;
            
            // The fourth is the optional data. If there is data, the target
            // field is always set so this argument is on the correct index.
            std::string data;
            
            if (invoke->args.size() >= 3) {
                target = NPVariantToString(invoke->args[2].get());
            }
            
            // An empty target defaults to "_self"
            // This is _required_ for chromium,
            // see https://savannah.gnu.org/bugs/?32425
            if ( target.empty() ) target = "_self";
            
            if (invoke->args.size() == 4) {
                data = NPVariantToString(invoke->args[3].get());
            }
            if (op == "GET") {
                gnash::log_debug("Asked to getURL '%s' in target %s", url,
                                 target);
                NPN_GetURL(_instance, url.c_str(), target.c_str());
            } else if (op == "POST") {                
                gnash::log_debug("Asked to postURL '%s' this data %s", url,
                                 data);
                NPN_PostURL(_instance, url.c_str(), target.c_str(), data.size(),
                            data.c_str(), false);
            } else {
                log_error("Unexpected op in getURL (expected POST or GET).");
            }
            
            continue; 
        } else if (invoke->name == "fsCommand") {
            
            assert(invoke->args.size() > 1);
            std::string command = NPVariantToString(invoke->args[0].get());
            std::string arg = NPVariantToString(invoke->args[1].get());            
            std::string name = _name; 
            std::stringstream jsurl;
            jsurl << "javascript:" << name << "_DoFSCommand('" << command
                  << "','" << arg <<"')";
            
            // TODO: check if _self is a good target for this
            static const char* tgt = "_self";
            
            gnash::log_debug("Calling NPN_GetURL(%s, %s)",
                             jsurl.str(), tgt);
            
            NPN_GetURL(_instance, jsurl.str().c_str(), tgt);
            continue;
        } else if (invoke->name == "addMethod") {
            
            assert(!invoke->args.empty());

            if (!HasScripting()) {
               LOG_ONCE(log_debug("Ignoring addMethod, no scripting."));
               continue;
            }
            // Make this flash function accessible to Javascript. The
            // actual callback lives in libcore/movie_root, but it
            // needs to be on the list of supported remote methods so
            // it can be called by Javascript.
            std::string method = NPVariantToString(invoke->args[0].get());
            NPIdentifier id = NPN_GetStringIdentifier(method.c_str());
            // log_debug("SCRIPT OBJECT addMethod: %x, %s", (void *)_scriptObject, method);
            this->getScriptObject()->AddMethod(id, remoteCallback);
            continue;
        }

        if (!HasScripting()) {
           LOG_ONCE(log_debug("Ignoring invoke, no scripting."));
           continue;
        }
        
        NPVariant result;
        VOID_TO_NPVARIANT(result);
        bool invokeResult = false;
        
        // This is the player invoking a method in Javascript
        if (!invoke->name.empty() && !invoke->args.empty()) {
            //Convert the as_value argument to NPVariant
            const size_t count = invoke->args.size() - 1;
            std::unique_ptr<NPVariant[]> args(new NPVariant[count]);
            //Skip the first argument
            for (size_t i = 0; i < count; ++i) {
                invoke->args[i+1].copy(args[i]);
            }
            
            NPIdentifier id = NPN_GetStringIdentifier(invoke->name.c_str());
            gnash::log_debug("Invoking JavaScript method %s", invoke->name);
            NPObject* windowObject;
            NPN_GetValue(_instance, NPNVWindowNPObject, &windowObject);
            invokeResult=NPN_Invoke(_instance, windowObject, id, args.get(),
                                    count, &result);
            NPN_ReleaseObject(windowObject);
        }
        // We got a result from invoking the Javascript method
        std::stringstream ss;

        ss << plugin::ExternalInterface::convertNPVariant(&result);
        NPN_ReleaseVariantValue(&result);
        size_t ret = _scriptObject->writePlayer(ss.str());
        if (ret != ss.str().size()) {
            log_error("Couldn't write the response to Gnash, network problems.");
            return false;
        }
    } while (!packet.empty());
    
    return true;
}

std::string
getGnashExecutable()
{
    std::string procname;
    bool process_found = false;
    struct stat procstats;

    char *gnash_env = std::getenv("GNASH_PLAYER");

    if (gnash_env) {
        procname = gnash_env;
        process_found = (0 == stat(procname.c_str(), &procstats));
        if (!process_found) {
            gnash::log_error("Invalid path to gnash executable: ");
            return "";
        }
    }

    if (!process_found) {
        procname = GNASHBINDIR "/gtk-gnash";
        process_found = (0 == stat(procname.c_str(), &procstats));
    }
    if (!process_found) {
        procname = GNASHBINDIR "/qt4-gnash";
        process_found = (0 == stat(procname.c_str(), &procstats));
    }

    if (!process_found) {
        gnash::log_error(std::string("Unable to find Gnash in ") + GNASHBINDIR);
        return "";
    }

    return procname;
}

void
create_standalone_launcher(const std::string& page_url, const std::string& swf_url,
                           const std::map<std::string, std::string>& params)
{
#ifdef CREATE_STANDALONE_GNASH_LAUNCHER
    if (!createSaLauncher) {
        return;
    }

    char debugname[] = "/tmp/gnash-debug-XXXXXX.sh";
    boost::iostreams::file_descriptor_sink fdsink = getfdsink(debugname);
#if BOOST_VERSION >= 104400
    if (fdsink.handle() == -1) {
        gnash::log_error("Failed to create sink: %s", debugname);
        return;
    }
#endif
    boost::iostreams::stream<boost::iostreams::file_descriptor_sink>
        saLauncher (fdsink);

    if (!saLauncher) {
        gnash::log_error("Failed to open new file for standalone launcher: %s", debugname);
        return;
    }

    saLauncher << "#!/bin/sh" << std::endl
               << getGnashExecutable() << " ";

    if (!page_url.empty()) {
        saLauncher << "-U '" << page_url << "' ";
    }
    if (!cookiefile.empty()) {
        saLauncher << "-C " << cookiefile << " ";
    }
    for (std::map<std::string,std::string>::const_iterator it = params.begin(),
        itEnd = params.end(); it != itEnd; ++it) {
        const std::string& nam = it->first; 
        const std::string& val = it->second;
        saLauncher << "-P '"
                   << boost::algorithm::replace_all_copy(nam, "'", "'\\''")
                   << "="
                   << boost::algorithm::replace_all_copy(val, "'", "'\\''")
                   << "' ";
    }

    saLauncher << "'" << swf_url << "' "
               << "$@"       // allow caller to pass any additional argument
               << std::endl;

    saLauncher.close();
    fdsink.close();
#endif
}

std::string
nsPluginInstance::getDocumentProp(const std::string& propname) const
{
    std::string rv;

    if (!HasScripting()) {
        LOG_ONCE( gnash::log_debug("Browser doesn't support scripting") );
        return rv;
    }

    NPObject* windowobj;
    NPError err = NPN_GetValue(_instance, NPNVWindowNPObject, &windowobj);
    if (err != NPERR_NO_ERROR || !windowobj) {
        return rv;
    }

    std::shared_ptr<NPObject> window_obj(windowobj, NPN_ReleaseObject);
  
    NPIdentifier doc_id = NPN_GetStringIdentifier("document");

    NPVariant docvar;
    if(! NPN_GetProperty(_instance, windowobj, doc_id, &docvar) ) {
        return rv;
    }

    std::shared_ptr<NPVariant> doc_var(&docvar, NPN_ReleaseVariantValue);

    if (!NPVARIANT_IS_OBJECT(docvar)) {
        return rv;
    }

    NPObject* doc_obj = NPVARIANT_TO_OBJECT(docvar);
    
    NPIdentifier prop_id = NPN_GetStringIdentifier(propname.c_str());

    NPVariant propvar;
    if (!NPN_GetProperty(_instance, doc_obj, prop_id, &propvar)) {
        return rv;
    }

    std::shared_ptr<NPVariant> prop_var(&propvar, NPN_ReleaseVariantValue);

    if (!NPVARIANT_IS_STRING(propvar)) {
        return rv;
    }

    const NPString& prop_str = NPVARIANT_TO_STRING(propvar);

    rv = NPStringToString(prop_str);
    return rv;
}



void
nsPluginInstance::setupCookies(const std::string& pageurl)
{
    // Cookie appear to drop anything past the domain, so we strip
    // that off.
    std::string::size_type pos;
    pos = pageurl.find("/", pageurl.find("//", 0) + 2) + 1;
    std::string url = pageurl.substr(0, pos);

    std::string ncookie;
 
    char *cookie = nullptr;
    uint32_t length = 0;

    NPError rv = NPERR_GENERIC_ERROR;
#if NPAPI_VERSION != 190
    if (NPNFuncs.getvalueforurl) {
        rv = NPN_GetValueForURL(_instance, NPNURLVCookie, url.c_str(),
                                &cookie, &length);
    } else {
        LOG_ONCE( gnash::log_debug("Browser doesn't support getvalueforurl") );
    }
#endif

    // Firefox does not (always) return the cookies that are associated
    // with a domain name through GetValueForURL.
    if (rv == NPERR_GENERIC_ERROR) {
        log_debug("Trying window.document.cookie for cookies");
        ncookie = getDocumentProp("cookie");
    }

    if (cookie) {
        ncookie.assign(cookie, length);
        NPN_MemFree(cookie);
    }

    if (ncookie.empty()) {
        gnash::log_debug("No stored Cookie for %s", url);
        cookiefile.clear();
        return;
    }

    gnash::log_debug("The Cookie for %s is %s", url, ncookie);
    char mkstemplate[] = "/tmp/gnash-cookie.XXXXXX";
    boost::iostreams::file_descriptor_sink fdsink = getfdsink(mkstemplate);
#if BOOST_VERSION >= 104400
    if (fdsink.handle() == -1) {
        gnash::log_error("Failed to create sink: %s", mkstemplate);
        return;
    }
#endif
    cookiefile = mkstemplate;
    boost::iostreams::stream<boost::iostreams::file_descriptor_sink>
        cookiestream (fdsink);

    // Firefox provides cookies in the following format:
    //
    // cookie1=value1;cookie2=value2;cookie3=value3
    //
    // Whereas libcurl expects cookies in the following format:
    //
    // Set-Cookie: cookie1=value1;
    // Set-Cookie: cookie2=value2;
  
    typedef boost::char_separator<char> char_sep;
    typedef boost::tokenizer<char_sep> tokenizer;
    tokenizer tok(ncookie, char_sep(";"));

    for (tokenizer::iterator it=tok.begin(); it != tok.end(); ++it) {
        cookiestream << "Set-Cookie: " << *it << std::endl;
    }
 
    cookiestream.close();
    fdsink.close();
    gnash::log_debug("Cookiefile is %s", cookiefile);
}

void
nsPluginInstance::setupProxy(const std::string& url)
{
    // In pre xulrunner 1.9, (Firefox 3.1) this function does not exist,
    // so we can't use it to read the proxy information.
#if NPAPI_VERSION != 190
    if (!NPNFuncs.getvalueforurl) return;
#endif

    char *proxy = nullptr;
    uint32_t length = 0;
#if NPAPI_VERSION != 190
    NPN_GetValueForURL(_instance, NPNURLVProxy, url.c_str(),
                       &proxy, &length);
#endif
    if (!proxy) {
        gnash::log_debug("No proxy setting for %s", url);
        return;
    }

    std::string nproxy (proxy, length);
    NPN_MemFree(proxy);

    gnash::log_debug("Proxy setting for %s is %s", url, nproxy);

    std::vector<std::string> parts;
    boost::split(parts, nproxy,
        boost::is_any_of(" "), boost::token_compress_on);
    if ( parts[0] == "DIRECT" ) {
        // no proxy
    }
    else if ( parts[0] == "PROXY" ) {
        // authenticated proxies: need to specify proxy credentials through
        // http_proxy env var set to user:pass@proxy:port. If set, it won't be
        // overridden. See https://savannah.gnu.org/bugs/?26713
        char *http_proxy=getenv("http_proxy");
        if (!http_proxy) {
            gnash::log_debug("Setting http_proxy to %s", parts[1].c_str());
            if (setenv("http_proxy", parts[1].c_str(), 1) < 0) {
                gnash::log_error(
                    "Couldn't set environment variable http_proxy to %s",
                    parts[1].c_str());
            }
        } else {
            gnash::log_debug("http_proxy already set to %s", http_proxy);
        }
    }
    else {
        gnash::log_error("Unknown proxy type: %s", nproxy);
    }

}

std::vector<std::string>
nsPluginInstance::getCmdLine(int hostfd, int controlfd)
{
    std::vector<std::string> arg_vec;

    std::string cmd = getGnashExecutable();
    if (cmd.empty()) {
        gnash::log_error("Failed to locate the Gnash executable!");
        return arg_vec;
    }
    arg_vec.push_back(cmd);

    arg_vec.push_back("-u");
    arg_vec.push_back(_swf_url);
    
    std::string pageurl = getCurrentPageURL();
    if (pageurl.empty()) {
        gnash::log_error("Could not get current page URL!");
    } else {
        arg_vec.push_back("-U");
        arg_vec.push_back(pageurl);
    }

    setupCookies(pageurl);
    setupProxy(pageurl);

    std::stringstream pars;
    pars << "-x "  <<  _window          // X window ID to render into
         << " -j " << _width            // Width of window
         << " -k " << _height;           // Height of window
#if GNASH_PLUGIN_DEBUG > 1
    pars << " -vv ";
#endif
    if ((hostfd > 0) && (controlfd)) {
        pars << " -F " << hostfd            // Socket to send commands to
             << ":"    << controlfd;        // Socket determining lifespan
    }
    if (!cookiefile.empty()) {
        pars << " -C " << cookiefile;
    }
    std::string pars_str = pars.str();
    typedef boost::char_separator<char> char_sep;
    boost::tokenizer<char_sep> tok(pars_str, char_sep(" "));
    arg_vec.insert(arg_vec.end(), tok.begin(), tok.end());

    for (std::map<std::string,std::string>::const_iterator it = _params.begin(),
        itEnd = _params.end(); it != itEnd; ++it) {
        const std::string& nam = it->first; 
        const std::string& val = it->second;
        arg_vec.push_back("-P");
        arg_vec.push_back(nam + "=" + val);
    }
    arg_vec.push_back("-");

    create_standalone_launcher(pageurl, _swf_url, _params);

    return arg_vec;
}

template <std::size_t N>
void
close_fds(const int (& except)[N])
{
    // Rather than close all the thousands of possible file
    // descriptors, we start after stderr and keep closing higher numbers
    // until we encounter ten fd's in a row that
    // aren't open. This will tend to close most fd's in most programs.
    int numfailed = 0, closed = 0;
    for (int anfd = fileno(stderr)+1; numfailed < 10; anfd++) {
        if (std::find(except, except+N, anfd) != except+N) {
            continue;
        }
        if (close(anfd) < 0) {
            numfailed++;
        } else {
            numfailed = 0;
            closed++;
        }
    }
    gnash::log_debug("Closed %d files.", closed);
} 

void
wait_for_gdb()
{
    if (!waitforgdb) {
        return;
    }

    // For debugging the plugin (GNASH_OPTIONS=waitforgdb)
    // Block here until gdb is attached and sets waitforgdb to false.

    std::cout << std::endl << "  Attach GDB to PID " << getpid()
              << " to debug!" << std::endl
              << "  This thread will block until then!" << std::endl
              << "  Once blocked here, you can set other breakpoints."
              << std::endl
              << "  Do a \"set variable waitforgdb=$false\" to continue"
              << std::endl << std::endl;

    while (waitforgdb) {
        sleep(1);
    }
}

void
nsPluginInstance::setupIOChannel(int fd, GIOFunc handler, GIOCondition signals)
{
    GIOChannel* ichan = g_io_channel_unix_new(fd);
    g_io_channel_set_close_on_unref(ichan, true);

    gnash::log_debug("New IO Channel for fd #%d",
                     g_io_channel_unix_get_fd(ichan));
    g_io_add_watch(ichan, signals, handler, this); 
    g_io_channel_unref(ichan);
}


NPError
nsPluginInstance::startProc()
{

    int p2c_pipe[2];
    int c2p_pipe[2];
    int p2c_controlpipe[2];

    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, p2c_pipe);
    if (ret == -1) {
        gnash::log_error("socketpair(p2c) failed: %s", strerror(errno));
        return NPERR_GENERIC_ERROR;
    }
    _streamfd = p2c_pipe[1];

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, c2p_pipe);
    if (ret == -1) {
        gnash::log_error("socketpair(c2p) failed: %s", strerror(errno));
        return NPERR_GENERIC_ERROR;
    }

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, p2c_controlpipe);
    if (ret == -1) {
        gnash::log_error("socketpair(control) failed: %s", strerror(errno));
        return NPERR_GENERIC_ERROR;
    }

    if (HasScripting() && _scriptObject) {
        _scriptObject->setControlFD(p2c_controlpipe[1]);
        _scriptObject->setHostFD(c2p_pipe[0]);
    }
    
    // Setup the command line for starting Gnash

    std::vector<std::string> arg_vec = getCmdLine(c2p_pipe[1],
                                                  p2c_controlpipe[0]);

    if (arg_vec.empty()) {
        gnash::log_error("Failed to obtain command line parameters.");
        return NPERR_GENERIC_ERROR;
    }
    
    std::vector<const char*> args;
    
    std::transform(arg_vec.begin(), arg_vec.end(), std::back_inserter(args),
                   std::mem_fun_ref(&std::string::c_str));
    args.push_back(nullptr);
    
    // Argument List prepared, now fork(), close file descriptors and execv()
    
    _childpid = fork();
    
    // If the fork failed, childpid is -1. So print out an error message.
    if (_childpid == -1) {
        gnash::log_error("fork() failed: %s", strerror(errno));
        return NPERR_OUT_OF_MEMORY_ERROR;
    }
    
    // If we are the parent and fork() worked, childpid is a positive integer.
    if (_childpid > 0) {
        // Close the child's end of the pipes.
        int fdstoclose[] = {p2c_controlpipe[0], p2c_pipe[0], c2p_pipe[1]};
        int num_failed = std::count_if(fdstoclose, fdstoclose+3, close);

        if (num_failed > 0) {
            // this is not really a fatal error, so continue best as we can
            gnash::log_error("%d fds failed to close: %s (error ignored).",
                             num_failed, strerror(errno));
        }

        gnash::log_debug("Forked successfully, child process PID is %d",
                         _childpid);

        if (!cookiefile.empty()) {
            cookiemap.insert(std::make_pair(_childpid, cookiefile));
            gnash::log_debug("Pid %d associated with cookiefile %s",
                _childpid, cookiefile);
        }

        setupIOChannel(c2p_pipe[0], (GIOFunc)handlePlayerRequestsWrapper,
                                    (GIOCondition)(G_IO_IN|G_IO_HUP));
        
        setupIOChannel(p2c_controlpipe[1], remove_handler, G_IO_HUP);

        return NPERR_NO_ERROR;
    }
    
    // This is the child scope.
    
    // FF3 uses jemalloc and it has problems after the fork(), do NOT
    // use memory functions (malloc()/free()/new/delete) after the fork()
    // in the child thread process
    ret = close (p2c_pipe[1]);
    
    // close standard input and direct read-fd1 to standard input
    ret = dup2 (p2c_pipe[0], fileno(stdin));

    if (ret == -1) {
        gnash::log_error("dup2() failed: %s", strerror(errno));
    }
    
    // Close all of the browser's file descriptors that we just inherited
    // (including p2c_pipe[0] that we just dup'd to fd 0), but obviously
    // not the file descriptors that we want the child to use.
    int dontclose[] = {c2p_pipe[1], c2p_pipe[0], p2c_controlpipe[0]};
    close_fds(dontclose);

    // Start the desired executable and go away.
    
    gnash::log_debug("Starting process: %s", boost::algorithm::join(arg_vec, " "));

    wait_for_gdb();

    execv(args[0], const_cast<char**>(&args.front()));

    // if execv returns, an error has occurred.
    perror("executing standalone gnash");
    
    exit (-1);
}





std::string
nsPluginInstance::getCurrentPageURL() const
{
    // Return:
    //  window.document.baseURI
    //
    // Was (bogus):
    //  window.document.location.href

    return getDocumentProp("baseURI");
}

void
processLog_error(const boost::format& fmt)
{
    std::cerr << "ERROR: " << fmt.str() << std::endl;
}

#if GNASH_PLUGIN_DEBUG > 1
void
processLog_debug(const boost::format& fmt)
{
    std::cout << "DEBUG: " << fmt.str() << std::endl;
}

void
processLog_trace(const boost::format& fmt)
{
    std::cout << "TRACE: " << fmt.str() << std::endl;
}
#else
void
processLog_debug(const boost::format& /* fmt */)
{ /* do nothing */ }

void
processLog_trace(const boost::format& /* fmt */)
{ /* do nothing */ }
#endif

} // end of gnash namespace

// Local Variables:
// mode: C++
// indent-tabs-mode: nil
// End:
