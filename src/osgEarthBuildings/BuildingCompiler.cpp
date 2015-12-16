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
#include <osgEarth/Tessellator>

using namespace osgEarth;
using namespace osgEarth::Buildings;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;


BuildingCompiler::BuildingCompiler()
{
    // nop
}

osg::Node*
BuildingCompiler::compile(const BuildingVector& input,
                          ProgressCallback*     progress)
{
    osg::ref_ptr<osg::Group> root = new osg::Group();

    // TODO: run through buildings and establish a global reference frame
    osg::Geode* geode = new osg::Geode();
    root->addChild( geode );

    for(BuildingVector::const_iterator i = input.begin(); i != input.end(); ++i)
    {
        Building* building = i->get();

        osg::Geometry* geom = createSimpleFootprint(building);
        if ( geom )
        {
            geode->addDrawable( geom );
        }
    }

    return root.release();
}

osg::Geometry*
BuildingCompiler::createSimpleFootprint(Building* building) const
{
    Polygon* fp = building->getFootprint();

    osg::Geometry* geom = new osg::Geometry();
    geom->setUseVertexBufferObjects( true );
    geom->setUseDisplayList( false );

    osg::Vec3Array* v = new osg::Vec3Array();
    geom->setVertexArray( v );

    GeometryIterator iter(fp);
    while( iter.hasMore() )
    {
        int ptr = v->size();
        Geometry* poly = iter.next();
        for(Geometry::iterator p = poly->begin(); p != poly->end(); ++p)
        {
            v->push_back( (*p) * building->getReferenceFrame() );
        }
        geom->addPrimitiveSet( new osg::DrawArrays(GL_LINE_LOOP, ptr, poly->size()) );
    }

    return geom;
}
