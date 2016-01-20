/* -*-c++-*- */
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
#include "Building"
#include "BuildingVisitor"

#define LC "[Building] "

using namespace osgEarth::Buildings;

Building::Building() :
_zoning    ( Zoning::ZONING_UNKNOWN ),
_minHeight ( 0.0f ),
_maxHeight ( FLT_MAX ),
_minArea   ( 0.0f ),
_maxArea   ( FLT_MAX )
{
}

Building::Building(const Building& rhs) :
_zoning    ( rhs._zoning ),
_minHeight ( rhs._minHeight ),
_maxHeight ( rhs._maxHeight ),
_minArea   ( rhs._minArea ),
_maxArea   ( rhs._maxArea )
{
    for(ElevationVector::const_iterator e = rhs.getElevations().begin(); e != rhs.getElevations().end(); ++e)
        _elevations.push_back( e->get()->clone() );
}

Building*
Building::clone() const
{
    return new Building(*this);
}

#if 0
void
Building::setGeometry(Polygon* geometry)
{
    _footprint = geometry;

    if ( _footprint.valid() )
    {
        // calculate the rotation data.
        // looks for the longest segment in the footprint and
        // returns the angle of that segment relative to north.
        Segment n;
        double  maxLen2 = 0.0;
        ConstSegmentIterator i( _footprint.get(), true );
        while( i.hasMore() )
        {
            Segment s = i.next();
            double len2 = (s.second - s.first).length2();
            if ( len2 > maxLen2 ) 
            {
                maxLen2 = len2;
                n = s;
            }
        }

        const osg::Vec3d& p1 = n.first.x() < n.second.x() ? n.first : n.second;
        const osg::Vec3d& p2 = n.first.x() < n.second.x() ? n.second : n.first;

        float r = atan2( p2.x()-p1.x(), p2.y()-p1.y() );
        _sinR = sinf( r );
        _cosR = cosf( r );

        _longEdgeRotatedMidpoint = (p1+p2)*0.5;
        _longEdgeRotatedInsideNormal = (n.second-n.first)^osg::Vec3d(0,0,-1);
        _longEdgeRotatedInsideNormal.normalize();
        
        // compute the axis-aligned bbox
        _aabb.init();
        for(Geometry::const_iterator i = _footprint->begin(); i != _footprint->end(); ++i)
        {
            osg::Vec3f v(i->x(), i->y(), 0.0f);
            rotate( v );
            _aabb.expandBy( v );
        }
    }
}
#endif

void
Building::setHeight(float height)
{
    for(ElevationVector::iterator e = _elevations.begin(); e != _elevations.end(); ++e)
    {
        e->get()->setHeight( height );
    }
}

bool
Building::build(const Polygon* footprint)
{
    if ( !footprint || !footprint->isValid() )
        return false;

    for(ElevationVector::iterator e = _elevations.begin(); e != _elevations.end(); ++e)
    {
        e->get()->build( footprint );
    }

    return true;
}

void
Building::accept(BuildingVisitor& bv)
{
    bv.apply( this );
}

Config
Building::getConfig() const
{
    Config conf("building");
    if ( !getElevations().empty() )
    {
        Config evec("elevations");
        for(ElevationVector::const_iterator e = getElevations().begin(); e != getElevations().end(); ++e)
            evec.add("elevation", e->get()->getConfig());
        conf.add(evec);
    }
    return conf;
}
