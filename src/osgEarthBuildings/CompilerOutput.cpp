
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
#include "CompilerOutput"
#include <osg/MatrixTransform>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/DrawInstanced>
#include <osgEarth/Registry>

using namespace osgEarth;
using namespace osgEarth::Buildings;

#define LC "[CompilerOutput] "

CompilerOutput::CompilerOutput()
{
    _mainGeode = new osg::Geode();
    _detailGeode = new osg::Geode();
    _debugGroup = new osg::Group();
}

void
CompilerOutput::addInstance(ModelResource* model, const osg::Matrix& matrix)
{
    _instances[model].push_back( matrix );
}

osg::Node*
CompilerOutput::createSceneGraph(Session* session) const
{
    // install the master matrix for this graph:
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform( getLocalToWorld() );
    
    // add the main geode.
    if ( getMainGeode()->getNumDrawables() > 0 )
        root->addChild( getMainGeode() );
    
    // add the detail geode with a closer LOD range.
    if ( getDetailGeode()->getNumDrawables() > 0 )
        root->addChild( getDetailGeode() ); // TODO: add LOD here

    // debug information.
    if ( getDebugGroup()->getNumChildren() > 0 )
        root->addChild( getDebugGroup() );
    
    // install the model instances, creating one instance group for each model.
    if ( session && session->getResourceCache() )
    {
        osg::Group* instances = new osg::Group();
        instances->setDataVariance( instances->DYNAMIC ); // block the optimizer
        ShaderGenerator::setIgnoreHint( instances, true ); // block the shader generator

        for(ModelPlacementMap::const_iterator i = _instances.begin(); i != _instances.end(); ++i)
        {
            osg::ref_ptr<osg::Node> modelNode;
            if ( session->getResourceCache()->cloneOrCreateInstanceNode(i->first.get(), modelNode) )
            {
                Registry::instance()->shaderGenerator().run( modelNode.get(), session->getStateSetCache() );

                osg::Group* group = new osg::Group();
                instances->addChild( group );

                // Build a normal scene graph based on MatrixTransforms, and then convert it 
                // over to use instancing if it's available.
                for(MatrixVector::const_iterator m = i->second.begin(); m != i->second.end(); ++m)
                {
                    osg::MatrixTransform* modelxform = new osg::MatrixTransform( *m );
                    modelxform->addChild( modelNode.get() );
                    group->addChild( modelxform );
                }

                // fails gracefully if instancing is not available:
                DrawInstanced::convertGraphToUseDrawInstanced( group );
            }
        }
        
        // fails gracefully if instancing is not available:
        DrawInstanced::install( instances->getOrCreateStateSet() );

        // finally add all the instance groups.
        root->addChild( instances );
    }

    return root.release();
}
