
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
 * Copyright 2008-2016 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include "Roof"
#include "Elevation"
#include <osgEarthSymbology/Geometry>

using namespace osgEarth;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

Roof::Roof() :
_parent( 0L )
{
    //nop
}

Config
Roof::getConfig() const
{
    Config conf("roof");
    return conf;
}

namespace
{
    struct Line
    {
        bool intersectRaysXY(
            const osg::Vec3d& p0, const osg::Vec3d& d0,
            const osg::Vec3d& p1, const osg::Vec3d& d1,
            osg::Vec3d& out_p,
            double&     out_u,
            double&     out_v) const
        {
            static const double epsilon = 0.001;

            double det = d0.y()*d1.x() - d0.x()*d1.y();
            if ( osg::equivalent(det, 0.0, epsilon) )
                return false; // parallel

            out_u = (d1.x()*(p1.y()-p0.y())+d1.y()*(p0.x()-p1.x()))/det;
            out_v = (d0.x()*(p1.y()-p0.y())+d0.y()*(p0.x()-p1.x()))/det;
            out_p = p0 + d0*out_u;
            return true;
        }

        osg::Vec3d _a, _b;

        Line(const osg::Vec3d& p0, const osg::Vec3d& p1) : _a(p0), _b(p1) { }

        bool intersectLine(const Line& rhs, osg::Vec3d& out) const {
            double u, v;
            osg::Vec3d temp;
            bool ok = intersectRaysXY(_a, (_b-_a), rhs._a, (rhs._b-rhs._a), temp, u, v);
            out.set( temp.x(), temp.y(), temp.z() );
            return ok;
        }

        bool intersectSegment(const Line& rhs, osg::Vec3d& out) const {
            double u, v;
            osg::Vec3d temp;
            bool ok = intersectRaysXY(_a, (_b-_a), rhs._a, (rhs._b-rhs._a), temp, u, v);
            out.set( temp.x(), temp.y(), temp.z() );
            return ok && u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0;
        }
        bool intersect(const Line& rhs, osg::Vec3d& out) const {
            double u, v;
            return intersectRaysXY(_a, (_b-_a), rhs._a, (rhs._b-rhs._a), out, u, v);
        }
    };
}

bool
Roof::findRectangle(const Footprint* fp, osg::BoundingBox& output) const
{
    osg::ref_ptr<Geometry> g = fp->clone();

    // rotate into axis-aligned space
    for(Geometry::iterator i = g->begin(); i != g->end(); ++i)
        getParent()->rotate( *i );

    // step 1. find candidate center point.
    osg::Vec3d y0 = getParent()->getLongEdgeRotatedMidpoint();
    osg::Vec3d n  = getParent()->getLongEdgeRotatedInsideNormal();

    Line bisectorY(y0, y0+osg::Vec3d(0,1,0));
    ConstSegmentIterator siY( g.get(), true );
    osg::Vec3d y1;
    while(siY.hasMore()) 
    {
        Segment s = siY.next();
        if ( Line(s.first, s.second).intersectLine(bisectorY, y1) )
            break;
    }

    osg::Vec3d p = (y0+y1)/2.0;

    Line bisectorX(p, p+osg::Vec3d(1,0,0));
    ConstSegmentIterator siX(g.get(), true);
    osg::Vec3d x0, x1;
    int num = 0;
    while(siX.hasMore() && num < 2)
    {
        Segment s = siX.next();
        if ( num == 0 ) {
            if ( Line(s.first, s.second).intersectLine(bisectorX, p0) )
                ++num;
        }
        else {
            if ( Line(s.first, s.second).intersectLine(bisectorY, x1) )
                ++num;
        }
    }
    
    p = (x0+x1)/2.0;

    // step 3. calc max width and height from center point.
    double maxWidth  = std::min( (p-x0).length(), (p-x1).length() );
    double maxHeight = std::min( (p-y0).length(), (p-y1).length() );

    // step 4. start with a square, then iterate over boxes of varying aspect ratios
    double maxArea = 0.0;

    // start with a single AR:
    double ar = 1.0;

    


    return true;
}
