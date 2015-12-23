
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
#include "Parapet"

#define LC "[Parapet] "

using namespace osgEarth;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

Parapet::Parapet() :
_width( 1.0f )
{
    // parapet always has a roof.
    _roof = new Roof();
    _roof->setType( Roof::TYPE_FLAT );
    _numFloors = 1u;
}

Parapet::Parapet(const Parapet& rhs) :
Elevation( rhs ),
_width   ( rhs._width )
{
}

Elevation*
Parapet::clone() const
{
    return new Parapet(*this);
}

bool
Parapet::build(const Footprint* footprint)
{
    // copy the outer ring of the footprint. Ignore any holes.
    osg::ref_ptr<Footprint> newFootprint = new Footprint( &footprint->asVector() );
    osg::ref_ptr<Geometry> buffered;

    // applly a negative buffer to the outer ring:
    BufferParameters bp(BufferParameters::CAP_DEFAULT, BufferParameters::JOIN_MITRE);
    if ( newFootprint->buffer(-getWidth(), buffered, bp) )
    {
        Ring* ring = dynamic_cast<Ring*>( buffered.get() );
        if ( ring )
        {
            // rewind the new geometry CW and add it as a hole:
            ring->rewind(Geometry::ORIENTATION_CW);
            newFootprint->getHoles().push_back( ring );
        }
    }

    return Elevation::build( newFootprint.get() );
}

Config
Parapet::getConfig() const
{
    Config conf = Elevation::getConfig();
    conf.add("type", "parapet");
    conf.add("width", getWidth());
    return conf;
}