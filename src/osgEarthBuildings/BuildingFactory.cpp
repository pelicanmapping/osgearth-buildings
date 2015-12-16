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
#include "BuildingFactory"
#include "BuildingSymbol"
#include <osgEarthSymbology/Geometry>

using namespace osgEarth;
using namespace osgEarth::Buildings;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;

#define LC "[BuildingFactory] "

BuildingFactory::BuildingFactory()
{
    //nop
}

bool
BuildingFactory::create(FeatureCursor*    input,
                        BuildingVector&   output,
                        ProgressCallback* progress)
{
    if ( !input )
        return false;

    // iterate over all the input features
    while( input->hasMore() )
    {
        // for each feature, check that it's a polygon
        Feature* feature = input->nextFeature();
        Geometry* geometry = feature->getGeometry();
        if ( geometry )
        {
            // use an iterator since it might be a multi-polygon. The second
            // argument keeps the polygon holes intact for now:
            GeometryIterator iter( geometry, false );
            while(iter.hasMore())
            {
                Polygon* footprint = dynamic_cast<Polygon*>(iter.next());
                if ( footprint && footprint->isValid() )
                {
                    // A footprint is the minumum info required to make a building.
                    Building* building = new Building();
                    output.push_back(building);

                    // make sure the polygon is all cleaned up
                    cleanPolygon( footprint );
                    building->setFootprint( footprint );
                }
                else
                {
                    OE_WARN << LC << "Not a polygon. Skipping.\n";
                }
            }
        }
    }

    return true;
}

void
BuildingFactory::cleanPolygon(Polygon* polygon)
{
    polygon->open();

    polygon->removeDuplicates();

    polygon->rewind( Polygon::ORIENTATION_CCW );

    // TODO: remove colinear points
}
