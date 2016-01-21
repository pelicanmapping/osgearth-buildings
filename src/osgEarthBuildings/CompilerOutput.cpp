
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
#include <osg/LOD>
#include <osg/MatrixTransform>
#include <osgUtil/Optimizer>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/DrawInstanced>
#include <osgEarth/Registry>

using namespace osgEarth;
using namespace osgEarth::Buildings;

#define LC "[CompilerOutput] "

CompilerOutput::CompilerOutput() :
_detailRangeFactor( 0.0f )
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
    osg::ref_ptr<StateSetCache> _sscache = new StateSetCache();

    // install the master matrix for this graph:
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform( getLocalToWorld() );
    
    // add the main geode.
    if ( getMainGeode()->getNumDrawables() > 0 )
        root->addChild( getMainGeode() );
    
    // add the detail geode with a closer LOD range.
    osg::LOD* detailLOD = 0L;
    if ( getDetailGeode()->getNumDrawables() > 0 )
    {
        if ( _detailRangeFactor != 0.0f )
        {
            detailLOD = new osg::LOD();
            detailLOD->addChild( getDetailGeode(), 0.0f, FLT_MAX );
            root->addChild( detailLOD );
        }
        else
        {
            root->addChild( getDetailGeode() ); // TODO: add LOD here
        }
    }
    
    // Run an optimization pass before adding any debug data or models
    {
        // because the default merge limit is 10000 and there's no other way to change it
        osgUtil::Optimizer::MergeGeometryVisitor mgv;
        mgv.setTargetMaximumNumberOfVertices( 250000u );
        root->accept( mgv );
    
        // Note: FLATTEN_STATIC_TRANSFORMS is bad for geospatial data, kills precision
        osgUtil::Optimizer o;
        o.optimize( root, o.DEFAULT_OPTIMIZATIONS & (~o.FLATTEN_STATIC_TRANSFORMS) & (~o.MERGE_GEOMETRY) );
    }

    // debug information.
    if ( getDebugGroup()->getNumChildren() > 0 )
        root->addChild( getDebugGroup() );

    // shader generation pass (before models)
    Registry::instance()->shaderGenerator().run( root, "Buildings", _sscache.get() );
    

    // install the model instances, creating one instance group for each model.
    if ( false && session && session->getResourceCache() )
    {
        // group to hold all instanced models:
        osg::Group* instances = new osg::Group();

        // keeps one copy of each instanced model per resource:
        typedef std::map< ModelResource*, osg::ref_ptr<osg::Node> > ModelNodes;
        ModelNodes modelNodes;

        for(ModelPlacementMap::const_iterator i = _instances.begin(); i != _instances.end(); ++i)
        {
            // look up or create the node corresponding to this instance:
            osg::ref_ptr<osg::Node>& modelNode = modelNodes[i->first.get()];
            if ( !modelNode.valid() )
            {
                // first time, clone it and shadergen it.
                if ( session->getResourceCache()->cloneOrCreateInstanceNode(i->first.get(), modelNode) )
                {
                    Registry::instance()->shaderGenerator().run( modelNode.get(), _sscache.get() );
                }
            }

            if ( modelNode.valid() )
            {
                osg::Group* modelGroup = new osg::Group();
                instances->addChild( modelGroup );

                // Build a normal scene graph based on MatrixTransforms, and then convert it 
                // over to use instancing if it's available.
                for(MatrixVector::const_iterator m = i->second.begin(); m != i->second.end(); ++m)
                {
                    osg::MatrixTransform* modelxform = new osg::MatrixTransform( *m );
                    modelxform->addChild( modelNode.get() );
                    modelGroup->addChild( modelxform );
                }

                // fails gracefully if instancing is not available:
                DrawInstanced::convertGraphToUseDrawInstanced( modelGroup );
            }
        }
        
        // fails gracefully if instancing is not available:
        DrawInstanced::install( instances->getOrCreateStateSet() );

        // finally add all the instance groups.
        root->addChild( instances );
    }

    if ( detailLOD )
    {
        detailLOD->setRange(0, 0.0f, root->getBound().radius() * _detailRangeFactor );
    }

    return root.release();
}
