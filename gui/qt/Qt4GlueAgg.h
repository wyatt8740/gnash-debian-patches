//
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010,
//   2011 Free Software Foundation, Inc
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

#ifndef GNASH_KDE4_AGG_GLUE_H
#define GNASH_KDE4_AGG_GLUE_H


#ifdef HAVE_CONFIG_H
#include "gnashconfig.h"
#endif

#include "Qt4Glue.h"

#include <memory>
#include <QImage>
#include <QPainter>
#include "snappingrange.h"

class QRect;

namespace gnash
{

class Qt4AggGlue : public Qt4Glue
{
  public:
    Qt4AggGlue();
    ~Qt4AggGlue();
    
    bool init(int argc, char **argv[]);
    void prepDrawingArea(DrawingWidget *drawing_area);
    Renderer* createRenderHandler();
    void initBuffer(int width, int height);
    void resize(int width, int height);
    void render();
    void render(const QRect& updateRect);

  private:
    int _width;
    int _height;
    std::unique_ptr<unsigned char[]> _offscreenbuf;
    Renderer* _renderer; // We don't own this pointer.
    std::unique_ptr<QImage> _image;
    std::unique_ptr<QPainter> _painter;
};


}

#endif
