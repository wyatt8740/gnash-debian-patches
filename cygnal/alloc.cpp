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

/// This defines thread safe new/delete operators
#include <new>
#include <mutex>

static std::mutex mem_mutex;

#if 0
// Wrap new in a mutex, because it is not thread safe.
void *
operator new (std::size_t bytes) throw (std::bad_alloc) {
    std::lock_guard<std::mutex> lock(mem_mutex);
    void *ptr = malloc (bytes);
    return ptr;
}

// Wrap delete in a mutex, because it is not thread safe.
void
operator delete (void* vptr) throw () {
    std::lock_guard<std::mutex> lock(mem_mutex);
    free (vptr);
}
#endif

// local Variables:
// mode: C++
// indent-tabs-mode: t
// End:
