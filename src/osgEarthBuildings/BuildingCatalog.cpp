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
#include "BuildingCatalog"
#include "Parapet"
#include "Roof"
#include "BuildingSymbol"

#include <osgEarth/XmlUtils>
#include <osgEarth/Containers>
#include <osgEarthSymbology/Style>

using namespace osgEarth;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

#define LC "[BuildingCatalog] "

BuildingCatalog::BuildingCatalog()
{
    //nop
}

bool
BuildingCatalog::createBuildings(Feature*        feature,
                                 Session*        session,
                                 BuildingVector& output) const
{
    Geometry* geometry = feature->getGeometry();

    if ( geometry && geometry->getComponentType() == Geometry::TYPE_POLYGON && geometry->isValid() )
    {
        // TODO: validate the 
        // Calculate a local reference frame for this building:
        osg::Vec2d center2d = geometry->getBounds().center2d();
        GeoPoint centerPoint( feature->getSRS(), center2d.x(), center2d.y(), 0.0, ALTMODE_ABSOLUTE );

        osg::Matrix local2world, world2local;
        centerPoint.createLocalToWorld( local2world );
        world2local.invert( local2world );

        // Transform feature geometry into the local frame. This way we can do all our
        // building creation in cartesian space.
        GeometryIterator iter(geometry, true);
        while(iter.hasMore())
        {
            Geometry* part = iter.next();
            for(Geometry::iterator i = part->begin(); i != part->end(); ++i)
            {
                osg::Vec3d world;
                feature->getSRS()->transformToWorld( *i, world );
                (*i) = world * world2local;
            }
        }

        // Next, iterate over the polygons and set up the Building object.
        GeometryIterator iter2( geometry, false );
        while(iter2.hasMore())
        {
            Polygon* footprint = dynamic_cast<Polygon*>(iter2.next());
            if ( footprint && footprint->isValid() )
            {
                // A footprint is the minumum info required to make a building.
                osg::ref_ptr<Building> building = createBuildingTemplate( feature, footprint, session );

                if ( building )
                {                   
                    //OE_INFO << "USING:\n" << building->getConfig().toJSON(true) << "\n\n\n";

                    // Install the reference frame of the footprint geometry:
                    building->setReferenceFrame( local2world );

                    // Do initial cleaning of the footprint and install is:
                    cleanPolygon( footprint );

                    // Apply the symbology:
                    applyStyle( feature, building.get(), session );

                    // Finally, build the internal structure from the footprint.
                    building->setFootprint( footprint );
                
                    if ( building->build() )
                    {
                        output.push_back( building.get() );
                    }
                    else
                    {
                        OE_WARN << "building::build() failed for some reason\n";
                    }
                }
            }
            else
            {
                OE_WARN << LC << "Feature " << feature->getFID() << " is not a polygon. Skipping..\n";
            }
        }
    }

    return true;
}

void
BuildingCatalog::cleanPolygon(Footprint* polygon) const
{
    polygon->open();

    polygon->removeDuplicates();

    polygon->rewind( Polygon::ORIENTATION_CCW );

    // TODO: remove colinear points? for skeleton?
}

bool
BuildingCatalog::applyStyle(Feature* feature, Building* building, Session* session) const
{
    if ( !feature || !building || !session ) return false;

    float height = 50.0f;
    unsigned numFloors = height/3.5f;

    const BuildingSymbol* sym = session->styles()->getDefaultStyle()->get<BuildingSymbol>();
    if ( sym )
    {
        if ( feature )
        {
            NumericExpression heightExpr = sym->height().get();
            height = feature->eval( heightExpr, session );
        }

        //// calculate the number of floors
        //if ( wallSkin )
        //{
        //    numFloors = (unsigned)std::max(1.0f, osg::round(height / wallSkin->imageHeight().get()));
        //}
        //else
        {
            numFloors = (unsigned)std::max(1.0f, osg::round(height / sym->floorHeight().get()));
        }
    }

    building->setHeight( height );
    //building->setNumFloors( numFloors );
    return true;
}

Building*
BuildingCatalog::createBuildingTemplate(Feature*   feature,
                                        Footprint* footprint,
                                        Session*   session) const
{
    if ( !_buildingsTemplates.empty() )
    {
        Random prng(feature->getFID());
        unsigned index = prng.next(_buildingsTemplates.size());
        return _buildingsTemplates.at(index).get()->clone();
    }
    else
    {
        return 0L;
    }
}

bool
BuildingCatalog::load(const URI& uri, const osgDB::Options* dbo, ProgressCallback* progress)
{
    osg::ref_ptr<XmlDocument> xml = XmlDocument::load(uri, dbo);
    if ( !xml.valid() )
    {
        if ( progress ) progress->reportError("File not found");
        return false;
    }

    Config conf = xml->getConfig();
    const Config* root = conf.find("buildings", true);

    if ( root )
        return load( *root, progress );
    else
        return false;
}

bool
BuildingCatalog::load(const Config& conf, ProgressCallback* progress)
{
    for(ConfigSet::const_iterator b = conf.children().begin(); b != conf.children().end(); ++b)
    {
        if ( b->empty() )
            continue;

        Building* building = new Building();

        //building->setTags();

        //building->setDefaultSkinTags();

        const Config* elevations = b->child_ptr("elevations");
        if ( elevations )
        {
            parseElevations( *elevations, 0L, building->getElevations(), progress );
        }

        _buildingsTemplates.push_back( building );
    }

    OE_INFO << LC << "Read " << _buildingsTemplates.size() << " building templates\n";

    return true;
}


bool
BuildingCatalog::parseElevations(const Config& conf, Elevation* parent, ElevationVector& output, ProgressCallback* progress)
{            
    for(ConfigSet::const_iterator e = conf.children().begin();
        e != conf.children().end();
        ++e)
    {
        Elevation* elevation = 0L;

        if ( e->value("type") == "parapet" )
        {
            Parapet* parapet = new Parapet();
            parapet->setWidth( e->value("width", parapet->getWidth()) );
            elevation = parapet;
        }
        else
        {
            elevation = new Elevation();
        }

        if ( parent )
            elevation->setParent( parent );

        optional<float> hp;
        if ( e->getIfSet( "height_percentage", hp) )
            elevation->setHeightPercentage( hp.get()*0.01f );

        if ( e->hasValue( "height" ) )
            elevation->setAbsoluteHeight( e->value("height", 15.0f) );

        if ( e->hasChild("inset") )
        {
            elevation->setInset( e->value("inset", 0.0f) );
        }

        elevation->setXOffset( e->value("xoffset", 0.0f) );
        elevation->setYOffset( e->value("yoffset", 0.0f) );

        if ( e->hasChild("roof") )
        {
            Roof* roof = parseRoof( e->child("roof"), progress );
            if ( roof )
                elevation->setRoof( roof );
        }

        output.push_back( elevation );

        const Config* children = e->child_ptr("elevations");
        if ( children )
        {
            parseElevations( *children, elevation, elevation->getElevations(), progress );
        }
    }

    return true;
}

Roof*
BuildingCatalog::parseRoof(const Config& conf, ProgressCallback* progress) const
{
    Roof* roof = new Roof();
    roof->setType( Roof::TYPE_FLAT );
    return roof;
}
