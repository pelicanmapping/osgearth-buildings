
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
_range( FLT_MAX ),
_index( 0L ),
_currentFeature( 0L )
{
    _defaultGeode = new osg::Geode();
    _externalModelsGroup = new osg::Group();
    _debugGroup = new osg::Group();
}

void
CompilerOutput::setLocalToWorld(const osg::Matrix& m)
{
    _local2world = m;
    _world2local.invert(m);
}

void
CompilerOutput::addDrawable(osg::Drawable* drawable)
{
    addDrawable( drawable, "" );
}

void
CompilerOutput::addDrawable(osg::Drawable* drawable, const std::string& tag)
{
    if ( !drawable )
        return;

    osg::ref_ptr<osg::Geode>& geode = _geodes[tag];
    if ( !geode.valid() )
    {
        geode = new osg::Geode();
    }
    geode->addDrawable( drawable );

    if ( _index && _currentFeature )
    {
        _index->tagDrawable( drawable, _currentFeature );
    }
}

void
CompilerOutput::addInstance(ModelResource* model, const osg::Matrix& matrix)
{
    _instances[model].push_back( matrix );

    //TODO: index it. the vector needs to be a vector of pair<matrix,feature>
}

osg::Node*
CompilerOutput::createSceneGraph(Session*                session,
                                 const CompilerSettings& settings,
                                 ProgressCallback*       progress) const
{
    OE_START_TIMER(total);

    osg::ref_ptr<StateSetCache> _sscache = new StateSetCache();

    // install the master matrix for this graph:
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform( getLocalToWorld() );

    // tagged geodes:
    if ( !_geodes.empty() )
    {
        // The Geode LOD holds each geode in its range.
        osg::LOD* geodeLOD = new osg::LOD();
        root->addChild( geodeLOD );

        for(TaggedGeodes::const_iterator g = _geodes.begin(); g != _geodes.end(); ++g)
        {
            const std::string& tag = g->first;
            const CompilerSettings::Bin* bin = settings.getBin(tag);
            float maxRange = bin? _range*bin->lodScale : FLT_MAX;
            geodeLOD->addChild( g->second.get(), 0.0f, maxRange );
        }
    }

    if ( _externalModelsGroup->getNumChildren() > 0 )
    {
        root->addChild( _externalModelsGroup.get() );
    }
    
    // Run an optimization pass before adding any debug data or models
    OE_START_TIMER(optimize);
    {
        // because the default merge limit is 10000 and there's no other way to change it
        osgUtil::Optimizer::MergeGeometryVisitor mgv;
        mgv.setTargetMaximumNumberOfVertices( 250000u );
        root->accept( mgv );
    
        // Note: FLATTEN_STATIC_TRANSFORMS is bad for geospatial data, kills precision
        osgUtil::Optimizer o;
        o.optimize( root,
            o.COMBINE_ADJACENT_LODS |
            o.SHARE_DUPLICATE_STATE |
            o.STATIC_OBJECT_DETECTION );
    }
    double optimizeTime = OE_GET_TIMER(optimize);

    OE_START_TIMER(shadergen);
    // shader generation pass (before models)
    Registry::instance()->shaderGenerator().run( root, "Buildings", _sscache.get() );
    double shaderGenTime = OE_GET_TIMER(shadergen);
    

    // install the model instances, creating one instance group for each model.
    OE_START_TIMER(instances);
    if ( session && session->getResourceCache() )
    {
        // group to hold all instanced models:
        osg::LOD* instances = new osg::LOD();

        // keeps one copy of each instanced model per resource:
        typedef std::map< ModelResource*, osg::ref_ptr<osg::Node> > ModelNodes;
        ModelNodes modelNodes;

        for(InstanceMap::const_iterator i = _instances.begin(); i != _instances.end(); ++i)
        {
            ModelResource* res = i->first.get();

            // look up or create the node corresponding to this instance:
            osg::ref_ptr<osg::Node>& modelNode = modelNodes[res];
            if ( !modelNode.valid() )
            {
                // first time, clone it and shadergen it.
                // TODO: check cloneOrCreate..why is it not const Resource?
                if ( session->getResourceCache()->cloneOrCreateInstanceNode(res, modelNode) )
                {
                    Registry::instance()->shaderGenerator().run( modelNode.get(), _sscache.get() );
                }
            }

            if ( modelNode.valid() )
            {
                OE_DEBUG << LC << "    " << res->name() << " = " << i->second.size() << "\n";

                osg::Group* modelGroup = new osg::Group();

                // Build a normal scene graph based on MatrixTransforms, and then convert it 
                // over to use instancing if it's available.
                const MatrixVector& mats = i->second;
                for(MatrixVector::const_iterator m = mats.begin(); m != mats.end(); ++m)
                {
                    osg::MatrixTransform* modelxform = new osg::MatrixTransform( *m );
                    modelxform->addChild( modelNode.get() );
                    modelGroup->addChild( modelxform );
                }

                // check for a display bin for this model resource:
                const CompilerSettings::Bin* bin = settings.getBin( res->tags() );
                float lodScale = bin ? bin->lodScale : 1.0f;

                float maxRange = std::max( _range*lodScale, modelGroup->getBound().radius() ); 
                instances->addChild( modelGroup, 0.0, maxRange );

                // fails gracefully if instancing is not available:
                DrawInstanced::convertGraphToUseDrawInstanced( modelGroup );
            }
        }
        
        // fails gracefully if instancing is not available:
        DrawInstanced::install( instances->getOrCreateStateSet() );

        // finally add all the instance groups.
        root->addChild( instances );
    }
    double instanceTime = OE_GET_TIMER(instances);

    if ( progress && progress->collectStats() )
    {
        progress->stats("out.optimize" ) = optimizeTime;
        progress->stats("out.shadergen") = shaderGenTime;
        progress->stats("out.instances") = instanceTime;
        progress->stats("out.total")     = OE_GET_TIMER(total);
    }

    return root.release();
}
