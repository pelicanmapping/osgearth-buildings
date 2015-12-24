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
#include "BuildingCompiler"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/MatrixTransform>
#include <osgUtil/SmoothingVisitor>
#include <osgUtil/Tessellator>
#include <osgEarth/Tessellator>
#include <osgEarthSymbology/MeshConsolidator>

#define LC "[BuildingCompiler] "

using namespace osgEarth;
using namespace osgEarth::Buildings;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;


BuildingCompiler::BuildingCompiler(Session* session) :
_session( session )
{
    _elevationCompiler = new ElevationCompiler( session );
    _flatRoofCompiler = new FlatRoofCompiler( session );
    _gableRoofCompiler = new GableRoofCompiler( session );
}

osg::Node*
BuildingCompiler::compile(Building*         building,
                          ProgressCallback* progress)
{
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform();

    osg::Geode* geode = new osg::Geode();
    root->addChild( geode );

    addSimpleFootprint( geode, building, osg::Matrix::identity() );

    return root.release();
}

osg::Node*
BuildingCompiler::compile(const BuildingVector& input,
                          ProgressCallback*     progress)
{    
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform();

    // Use the first building as our global reference frame. In usual practice,
    // we will probably have a anchor point for a group of buildings (a tile)
    // that we can pass in here and use.
    osg::Matrix local2world, world2local;
    if ( !input.empty() )
    {
        local2world = input.front()->getReferenceFrame();
    }
    world2local.invert(local2world);
    root->setMatrix( local2world );

    osg::Geode* geode = new osg::Geode();
    root->addChild( geode );

    for(BuildingVector::const_iterator i = input.begin(); i != input.end(); ++i)
    {
        Building* building = i->get();
        //addSimpleFootprint( geode, building, world2local );
        addElevations( geode, building, building->getElevations(), world2local );
    }

    return root.release();
}

bool
BuildingCompiler::addBuilding(osg::Geode*        geode,
                              const Building*    building,
                              const osg::Matrix& world2local) const
{
    return addSimpleFootprint(geode, building, world2local);
}

bool
BuildingCompiler::addSimpleFootprint(osg::Geode*        geode,
                                     const Building*    building,
                                     const osg::Matrix& world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;

    osg::Geometry* geom = new osg::Geometry();
    geom->setUseVertexBufferObjects( true );
    geom->setUseDisplayList( false );

    osg::Vec3Array* v = new osg::Vec3Array();
    geom->setVertexArray( v );

    GeometryIterator iter( building->getFootprint() );
    while( iter.hasMore() )
    {
        int ptr = v->size();
        Geometry* poly = iter.next();
        for(Geometry::iterator p = poly->begin(); p != poly->end(); ++p)
        {
            v->push_back( (*p) * (building->getReferenceFrame() * world2local) );
        }
        geom->addPrimitiveSet( new osg::DrawArrays(GL_LINE_LOOP, ptr, poly->size()) );
    }

    geode->addDrawable( geom );

    return true;
}

bool
BuildingCompiler::addElevations(osg::Geode*            geode,
                                const Building*        building,
                                const ElevationVector& elevations,
                                const osg::Matrix&     world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;

    // Iterator over each Elevation in this building:
    for(ElevationVector::const_iterator e = elevations.begin();
        e != elevations.end();
        ++e)
    {
        const Elevation* elevation = e->get();
     
        _elevationCompiler->compile(building, elevation, geode, world2local);

        if ( elevation->getRoof() )
        {
            addRoof( geode, building, elevation, world2local );
        }

        if ( !elevation->getElevations().empty() )
        {
            addElevations( geode, building, elevation->getElevations(), world2local );
        }

    } // elevations loop

    return true;
}

bool
BuildingCompiler::addRoof(osg::Geode* geode, const Building* building, const Elevation* elevation, const osg::Matrix& world2local) const
{
    if ( elevation && elevation->getRoof() )
    {
        if ( elevation->getRoof()->getType() == Roof::TYPE_GABLE )
        {
            if ( elevation->getAxisAlignedBoundingBox().radius() < 20.0f )
            {
                return _gableRoofCompiler->compile(building, elevation, geode, world2local);
            }
        }

        return _flatRoofCompiler->compile(building, elevation, geode, world2local);
    }
    return false;
}
