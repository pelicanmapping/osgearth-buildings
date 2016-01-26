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
    _customRoofCompiler = new CustomRoofCompiler( session );
}

bool
BuildingCompiler::compile(const BuildingVector& input,
                          CompilerOutput&       output,
                          ProgressCallback*     progress)
{
    // Use the first building as our global reference frame. In usual practice,
    // we will probably have a anchor point for a group of buildings (a tile)
    // that we can pass in here and use.
    osg::Matrix local2world, world2local;
    if ( !input.empty() )
    {
        local2world = input.front()->getReferenceFrame();
    }
    world2local.invert(local2world);

    output.setLocalToWorld( local2world );

    for(BuildingVector::const_iterator i = input.begin(); i != input.end(); ++i)
    {
        Building* building = i->get();

        if ( building->modelURI().isSet() )
        {
            addExternalModel( output, building, world2local, progress );
        }
        else
        {
            addElevations( output, building, building->getElevations(), world2local );
        }
    }

    return true;
}

bool
BuildingCompiler::addExternalModel(CompilerOutput&    output,
                                   const Building*    building,
                                   const osg::Matrix& world2local,
                                   ProgressCallback*  progress) const
{
    osg::ref_ptr<osg::Node> node = building->getModelURI().getNode(_session->getDBOptions(), progress);
    if ( node.valid() )
    {
        osg::MatrixTransform* xform = new osg::MatrixTransform( building->getReferenceFrame() * world2local );
        xform->addChild( node.get() );
        output.getExternalModelsGroup()->addChild( xform );
    }
    return node.valid();
}

bool
BuildingCompiler::addElevations(CompilerOutput&        output,
                                const Building*        building,
                                const ElevationVector& elevations,
                                const osg::Matrix&     world2local) const
{
    if ( !building ) return false;

    // Iterator over each Elevation in this building:
    for(ElevationVector::const_iterator e = elevations.begin();
        e != elevations.end();
        ++e)
    {
        const Elevation* elevation = e->get();
     
        _elevationCompiler->compile( output, building, elevation, world2local );

        if ( elevation->getRoof() )
        {
            addRoof( output, building, elevation, world2local );
        }

        if ( !elevation->getElevations().empty() )
        {
            addElevations( output, building, elevation->getElevations(), world2local );
        }

    } // elevations loop

    return true;
}

bool
BuildingCompiler::addRoof(CompilerOutput& output, const Building* building, const Elevation* elevation, const osg::Matrix& world2local) const
{
    if ( elevation && elevation->getRoof() )
    {
        if ( elevation->getRoof()->getType() == Roof::TYPE_GABLE )
        {
            return _gableRoofCompiler->compile(output, building, elevation, world2local);
        }
        else if ( elevation->getRoof()->getType() == Roof::TYPE_CUSTOM )
        {
            return _customRoofCompiler->compile(output, building, elevation, world2local);
        }
        else
        {
            return _flatRoofCompiler->compile(output, building, elevation, world2local);
        }
    }
    return false;
}
