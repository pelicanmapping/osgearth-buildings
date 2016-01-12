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
#include "BuildingPager"
#include <osgEarth/Registry>
#include <osgEarthSymbology/Query>
#include <osgUtil/Optimizer>

#define LC "[BuildingPager] "

using namespace osgEarth::Buildings;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;

#define OE_TEST OE_DEBUG


BuildingPager::BuildingPager(const Profile* profile) :
SimplePager( profile ),
_style     ( 0L )
{
    _stateSetCache = new StateSetCache();
    setAdditive( false );
}

void
BuildingPager::setLOD(unsigned lod)
{
    setMinLevel( lod );
    setMaxLevel( lod );
}

void
BuildingPager::setSession(Session* session)
{
    _session = session;

    if ( session )
    {
        _compiler = new BuildingCompiler(session);
        _style = session->styles() ? session->styles()->getDefaultStyle() : 0L;
    }
}

void
BuildingPager::setFeatureSource(FeatureSource* features)
{
    _features = features;
}

void
BuildingPager::setCatalog(BuildingCatalog* catalog)
{
    _catalog = catalog;
}

void
BuildingPager::setCacheBin(CacheBin* cacheBin, const CachePolicy& cp)
{
    _cacheBin = cacheBin;
    _cachePolicy = cp;
}

osg::Node*
BuildingPager::createNode(const TileKey& tileKey)
{
    if ( !_session.valid() || !_compiler.valid() || !_features.valid() )
    {
        OE_WARN << LC << "Misconfiguration error; make sure Session and FeatureSource are set\n";
        return 0L;
    }
    
    OE_START_TIMER(start);

    OE_TEST << LC << "createNode(" << tileKey.str() << ")\n";
    
    // Create a cursor to iterator over the feature data:
    Query query;
    query.tileKey() = tileKey;
    osg::ref_ptr<FeatureCursor> cursor = _features->createFeatureCursor( query );
    if ( !cursor.valid() || !cursor->hasMore() )
    {
        OE_WARN << LC << "Invalid or empty cursor for tile key (" << tileKey.str() << ")\n";
        return 0L;
    }
    
    OE_START_TIMER(factory_create);

    osg::ref_ptr<BuildingFactory> factory = new BuildingFactory();
    factory->setSession( _session.get() );
    factory->setCatalog( _catalog.get() );
    factory->setOutputSRS( _session->getMapSRS() );

    const Style* style =
        _session->styles() ? _session->styles()->getDefaultStyle() : 0L;

    BuildingVector buildings;
    if ( !factory->create(cursor.get(), tileKey.getExtent(), style, buildings) )
    {
        OE_WARN << LC << "Failed to create building data model\n";
        return 0L;
    }
    OE_NOTICE << LC << "Created " << buildings.size() << " buildings in " << std::setprecision(3) << OE_GET_TIMER(factory_create) << "s" << std::endl;

    // Create OSG model from buildings.
    OE_START_TIMER(compile);

    osg::ref_ptr<osg::Node> node = _compiler->compile(buildings);   

    OE_TEST << LC << "Compiled " << buildings.size() << " buildings in " << std::setprecision(3) << OE_GET_TIMER(compile) << "s" << std::endl;
    
    if ( !node.valid() )
    {
        OE_WARN << LC << "Compile failed";
        return 0L;
    }

    OE_START_TIMER(optimize);
    // Note: FLATTEN_STATIC_TRANSFORMS is bad for geospatial data
    osgUtil::Optimizer o;
    o.optimize( node, o.DEFAULT_OPTIMIZATIONS & (~o.FLATTEN_STATIC_TRANSFORMS) );
    OE_TEST << LC << "Optimized in " << std::setprecision(3) << OE_GET_TIMER(optimize) << "s" << std::endl;
    
    Registry::instance()->shaderGenerator().run( node.get(), "Buildings", _stateSetCache.get() );
   
    OE_TEST << LC << "Total time = " << OE_GET_TIMER(start) << "s" << std::endl;

    //todo
    return node.release();
}
