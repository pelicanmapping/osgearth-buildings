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


namespace
{
    // Callback to force building threads onto the high-latency pager queue.
    struct HighLatencyFileLocationCallback : public osgDB::FileLocationCallback
    {
        Location fileLocation(const std::string& filename, const osgDB::Options* options)
        {
            return REMOTE_FILE;
        }

        bool useFileCache() const { return false; }
    };
}


BuildingPager::BuildingPager(const Profile* profile) :
SimplePager( profile ),
_index     ( 0L )
{
    _stateSetCache = new StateSetCache();

    // Replace tiles with higher LODs.
    setAdditive( false );

    // Force building generation onto the high latency queue.
    setFileLocationCallback( new HighLatencyFileLocationCallback() );
}

void
BuildingPager::setSession(Session* session)
{
    _session = session;

    if ( session )
    {
        _compiler = new BuildingCompiler(session);

        // Analyze the styles to determine the min and max LODs.
        // Styles are named by LOD.
        if ( _session->styles() )
        {
            optional<unsigned> minLOD(0u), maxLOD(0u);
            for(unsigned i=0; i<30; ++i)
            {
                std::string styleName = Stringify() << i;
                const Style* style = _session->styles()->getStyle(styleName, false);
                if ( style )
                {
                    if ( !minLOD.isSet() )
                    {
                        minLOD = i;
                    }
                    else if ( !maxLOD.isSet() )
                    {
                        maxLOD = i;
                    }
                }
            }
            if ( minLOD.isSet() && !maxLOD.isSet() )
                maxLOD = minLOD.get();

            setMinLevel( minLOD.get() );
            setMaxLevel( maxLOD.get() );

            OE_INFO << LC << "Min level = " << getMinLevel() << "; max level = " << getMaxLevel() << std::endl;
        }
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

void
BuildingPager::setCompilerSettings(const CompilerSettings& settings)
{
    _compilerSettings = settings;
}

void BuildingPager::setIndex(FeatureIndexBuilder* index)
{
    _index = index;
}

#if 0
osg::Node*
BuildingPager::createNode(const TileKey& tileKey, ProgressCallback* progress)
{
    if ( !_session.valid() || !_compiler.valid() || !_features.valid() )
    {
        OE_WARN << LC << "Misconfiguration error; make sure Session and FeatureSource are set\n";
        return 0L;
    }

    progress->collectStats() = true;
    
    std::string activityName("Buildings " + tileKey.str());
    Registry::instance()->startActivity(activityName);

    OE_START_TIMER(total);

    OE_TEST << LC << tileKey.str() << ": createNode(" << tileKey.str() << ")\n";

    osg::ref_ptr<osg::Node> node;
    
    // Create a cursor to iterator over the feature data:
    Query query;
    query.tileKey() = tileKey;
    osg::ref_ptr<FeatureCursor> cursor = _features->createFeatureCursor( query );
    if ( cursor.valid() && cursor->hasMore() )
    {
        osg::ref_ptr<BuildingFactory> factory = new BuildingFactory();
        factory->setSession( _session.get() );
        factory->setCatalog( _catalog.get() );
        factory->setOutputSRS( _session->getMapSRS() );

        std::string styleName = Stringify() << tileKey.getLOD();
        const Style* style = _session->styles() ? _session->styles()->getStyle(styleName) : 0L;

        BuildingVector buildings;
        if ( factory->create(cursor.get(), tileKey.getExtent(), style, buildings, progress) )
        {
            // Create OSG model from buildings.
            CompilerOutput output;
            output.setIndex( _index );

            if ( _compiler->compile(buildings, output, progress) )
            {
                // set the distance at which details become visible.
                osg::BoundingSphere tileBound = getBounds( tileKey );
                output.setRange( tileBound.radius() * getRangeFactor() );

                node = output.createSceneGraph(_session.get(), _compilerSettings, progress);
            }
            else
            {
                OE_INFO << LC << "Tile " << tileKey.str() << " was canceled " << progress->message() << "\n";
            }
        }
        else
        {
            OE_INFO << LC << "Tile " << tileKey.str() << " was canceled " << progress->message() << "\n";
        }
    }

    Registry::instance()->endActivity(activityName);


    float totalTime = OE_GET_TIMER(total);

    // STATS:
    if ( progress && progress->collectStats() )
    {
        std::stringstream buf;
        buf << "Key = " << tileKey.str() << " : TIME = " << (int)(1000.0*totalTime) << " ms" << std::endl;

        for(ProgressCallback::Stats::const_iterator i = progress->stats().begin(); i != progress->stats().end(); ++i)
        { 
            buf
                << "    " 
                << std::setw(15) << i->first
                << std::setw(6) << (int)(1000.0*i->second) << " ms"
                << std::setw(6) << (int)(100.0*i->second/totalTime) << "%"
                << std::endl;
        }

        OE_INFO << LC << buf.str() << std::endl;
    }

    //todo
    return node.release();
}

#else

osg::Node*
BuildingPager::createNode(const TileKey& tileKey, ProgressCallback* progress)
{
    if ( !_session.valid() || !_compiler.valid() || !_features.valid() )
    {
        OE_WARN << LC << "Misconfiguration error; make sure Session and FeatureSource are set\n";
        return 0L;
    }

    progress->collectStats() = true;
    OE_START_TIMER(total);
    unsigned numFeatures = 0;
    
    std::string activityName("Buildings " + tileKey.str());
    Registry::instance()->startActivity(activityName);

    // result:
    osg::ref_ptr<osg::Node> node;
    
    // Create a cursor to iterator over the feature data:
    Query query;
    query.tileKey() = tileKey;
    osg::ref_ptr<FeatureCursor> cursor = _features->createFeatureCursor( query );
    if ( cursor.valid() && cursor->hasMore() )
    {
        osg::ref_ptr<BuildingFactory> factory = new BuildingFactory();

        factory->setSession( _session.get() );
        factory->setCatalog( _catalog.get() );
        factory->setOutputSRS( _session->getMapSRS() );

        std::string styleName = Stringify() << tileKey.getLOD();
        const Style* style = _session->styles() ? _session->styles()->getStyle(styleName) : 0L;
        
        bool canceled = false;

        // Holds all the final output.
        CompilerOutput output;
        output.setIndex( _index );

        while( cursor->hasMore() && !canceled )
        {
            Feature* feature = cursor->nextFeature();
            numFeatures++;

            BuildingVector buildings;
            if ( !factory->create(feature, tileKey.getExtent(), style, buildings, progress) )
                canceled = true;

            if ( !canceled && !buildings.empty() )
            {
                if ( output.getLocalToWorld().isIdentity() )
                {
                    output.setLocalToWorld( buildings.front()->getReferenceFrame() );
                }

                // for indexing, if enabled:
                output.setCurrentFeature( feature );

                for(BuildingVector::iterator b = buildings.begin(); b != buildings.end() && !canceled; ++b)
                {
                    if ( !_compiler->compile(buildings, output, progress) )
                        canceled = true;
                }
            }
        }

        if ( !canceled )
        {
            // set the distance at which details become visible.
            osg::BoundingSphere tileBound = getBounds( tileKey );
            output.setRange( tileBound.radius() * getRangeFactor() );
            node = output.createSceneGraph(_session.get(), _compilerSettings, progress);
        }
        else
        {
            OE_INFO << LC << "Tile " << tileKey.str() << " was canceled " << progress->message() << "\n";
        }
    }

    Registry::instance()->endActivity(activityName);

    double totalTime = OE_GET_TIMER(total);

    // STATS:
    if ( progress && progress->collectStats() && !progress->stats().empty() && numFeatures > 0)
    {
        std::stringstream buf;
        buf << "Key = " << tileKey.str() 
            << " : Features = " << numFeatures 
            << ", Time = " << (int)(1000.0*totalTime) 
            << " ms, Avg = " << std::setprecision(3) << (1000.0*(totalTime/(double)numFeatures)) << " ms"
            << std::endl;

        for(ProgressCallback::Stats::const_iterator i = progress->stats().begin(); i != progress->stats().end(); ++i)
        { 
            buf
                << "    " 
                << std::setw(15) << i->first
                << std::setw(6) << (int)(1000.0*i->second) << " ms"
                << std::setw(6) << (int)(100.0*i->second/totalTime) << "%"
                << std::endl;
        }

        OE_INFO << LC << buf.str() << std::endl;

        // clear them when we are done.
        progress->stats().clear();
    }

    //todo
    return node.release();
}

#endif