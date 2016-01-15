
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
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform( getLocalToWorld() );
    
    if ( getMainGeode()->getNumDrawables() > 0 )
        root->addChild( getMainGeode() );
    
    if ( getDetailGeode()->getNumDrawables() > 0 )
        root->addChild( getDetailGeode() ); // TODO: add LOD here

    if ( getDebugGroup()->getNumChildren() > 0 )
        root->addChild( getDebugGroup() );
    
    if ( session && session->getResourceCache() )
    {
        osg::Group* models = new osg::Group();
        models->setDataVariance( models->DYNAMIC ); // stop the optimizer
        ShaderGenerator::setIgnoreHint( models, true ); // stop the shader generator
        root->addChild( models );

        for(ModelPlacementMap::const_iterator i = _instances.begin(); i != _instances.end(); ++i)
        {
            osg::ref_ptr<osg::Node> modelNode;
            if ( session->getResourceCache()->getOrCreateInstanceNode(i->first.get(), modelNode) )
            {
                osg::Group* group = new osg::Group();
                models->setDataVariance( models->DYNAMIC ); // stop the optimizer
                ShaderGenerator::setIgnoreHint( models, true ); // stop the shader generator
                root->addChild( group );

                for(MatrixVector::const_iterator m = i->second.begin(); m != i->second.end(); ++m)
                {
                    osg::MatrixTransform* modelxform = new osg::MatrixTransform( *m );
                    modelxform->addChild( modelNode.get() );
                    group->addChild( modelxform );
                }
            }
        }
    }

    return root.release();
}
