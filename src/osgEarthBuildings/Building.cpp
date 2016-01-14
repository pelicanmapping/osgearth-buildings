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
_zoning   ( Zoning::ZONING_UNKNOWN ),
_minHeight( 0.0f ),
_maxHeight( FLT_MAX ),
_minArea  ( 0.0f ),
_maxArea  ( FLT_MAX )
{
}

Building::Building(const Building& rhs) :
_zoning   ( rhs._zoning ),
_minHeight( rhs._minHeight ),
_maxHeight( rhs._maxHeight ),
_minArea  ( rhs._minArea ),
_maxArea  ( rhs._maxArea )
{
    for(ElevationVector::const_iterator e = rhs.getElevations().begin(); e != rhs.getElevations().end(); ++e)
        _elevations.push_back( e->get()->clone() );
}

Building*
Building::clone() const
{
    return new Building(*this);
}

void
Building::setHeight(float height)
{
    for(ElevationVector::iterator e = _elevations.begin(); e != _elevations.end(); ++e)
    {
        e->get()->setHeight( height );
    }
}

bool
Building::build()
{
    if ( !_footprint.valid() ) return false;

    for(ElevationVector::iterator e = _elevations.begin(); e != _elevations.end(); ++e)
    {
        e->get()->build( getFootprint() );
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
