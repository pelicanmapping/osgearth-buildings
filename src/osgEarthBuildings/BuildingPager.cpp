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
#include <osgUtil/Statistics>
#include <osg/Version>

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

    // Callback that culls unused stuff from the ObjectCache.
    // Unfortunately we cannot use this in OSG < 3.5.1 because of an OSG threading bug;
    // https://github.com/openscenegraph/OpenSceneGraph/commit/5b17e3bc2a0c02cf84d891bfdccf14f170ee0ec8 
    struct TendArtCacheCallback : public osg::NodeCallback
    {
        TendArtCacheCallback(osgDB::ObjectCache* cache) : _cache(cache) { }

        void operator()(osg::Node* node, osg::NodeVisitor* nv)
        {
            if (nv->getFrameStamp())
            {
                _cache->updateTimeStampOfObjectsInCacheWithExternalReferences(nv->getFrameStamp()->getReferenceTime());
                _cache->removeExpiredObjectsInCache(10.0);
            }
            traverse(node, nv);
        }        
        
        osg::ref_ptr<osgDB::ObjectCache> _cache;
    };

    struct ArtCache : public osgDB::ObjectCache
    {
        unsigned size() const { return this->_objectCache.size(); }
    };
}


BuildingPager::BuildingPager(const Profile* profile) :
SimplePager( profile ),
_index     ( 0L )
{
    // Replace tiles with higher LODs.
    setAdditive( false );

    // Force building generation onto the high latency queue.
    setFileLocationCallback( new HighLatencyFileLocationCallback() );

    _profile = ::getenv("OSGEARTH_BUILDINGS_PROFILE") != 0L;

    // An object cache for shared resources like textures, atlases, and instanced models.
    _artCache = new ArtCache(); //osgDB::ObjectCache();

    // Texture object cache
    _texCache = new TextureCache();

#if OSG_VERSION_GREATER_OR_EQUAL(3,5,1)
    // Read this to see why the version check exists:
    // https://github.com/openscenegraph/OpenSceneGraph/commit/5b17e3bc2a0c02cf84d891bfdccf14f170ee0ec8

    // This callack expires unused items from the art cache periodically
    this->addCullCallback(new TendArtCacheCallback(_artCache.get()));
#endif
}

void
BuildingPager::setSession(Session* session)
{
    _session = session;

    if ( session )
    {
        _compiler = new BuildingCompiler(session);

        _clamper = new TerrainClamper();
        _clamper->setSession( session );

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

    // Apply the range factor from the settings:
    if (_compilerSettings.rangeFactor().isSet())
    {
        this->setRangeFactor(_compilerSettings.rangeFactor().get());
    }
}

void BuildingPager::setIndex(FeatureIndexBuilder* index)
{
    _index = index;
}

bool
BuildingPager::cacheReadsEnabled() const
{
    return _cacheBin.valid() && _cachePolicy.isCacheReadable();
}

bool
BuildingPager::cacheWritesEnabled() const
{
    return _cacheBin.valid() && _cachePolicy.isCacheWriteable();
}

osg::Node*
BuildingPager::createNode(const TileKey& tileKey, ProgressCallback* progress)
{
    if ( !_session.valid() || !_compiler.valid() || !_features.valid() )
    {
        OE_WARN << LC << "Misconfiguration error; make sure Session and FeatureSource are set\n";
        return 0L;
    }

    //OE_INFO << LC << "Art cache size = " << ((ArtCache*)_artCache.get())->size() << "\n";

    if ( progress )
        progress->collectStats() = _profile;

    OE_START_TIMER(total);
    unsigned numFeatures = 0;
    
    std::string activityName("Buildings " + tileKey.str());
    Registry::instance()->startActivity(activityName);

    // result:
    osg::ref_ptr<osg::Node> node;


    // I/O Options to use throughout the build process.
    // Install an "art cache" in the read options so that images can be 
    // shared throughout the creation process. This is critical for sharing 
    // textures and especially for texture atlas usage.
    osg::ref_ptr<osgDB::Options> readOptions = new osgDB::Options(); // = osgEarth::Registry::instance()->cloneOrCreateOptions(_session->getDBOptions());
    readOptions->setObjectCache(_artCache.get());
    readOptions->setObjectCacheHint(osgDB::Options::CACHE_IMAGES);

    // TESTING:
    //OE_INFO << LC << "Art cache size = " << ((MyObjectCache*)(_artCache.get()))->getSize() << "\n";
    
    // install the cache bin in the read options, so we can resolve external references
    // to images, etc. stored in the same cache bin
    if (_cacheBin.valid())
    {
        _cacheBin->put(readOptions.get());
    }

    // Holds all the final output.
    CompilerOutput output;
    output.setName(tileKey.str());
    output.setTileKey(tileKey);
    output.setIndex(_index);
    output.setTextureCache(_texCache.get());

    bool canceled = false;
    bool caching = true;

    // Try to load from the cache.
    if (cacheReadsEnabled() && !canceled)
    {
        OE_START_TIMER(readCache);

        node = output.readFromCache(_cacheBin.get(), _cachePolicy, readOptions, progress);

        if (progress && progress->collectStats())
            progress->stats("pager.readCache") = OE_GET_TIMER(readCache);
    }

    bool fromCache = node.valid();

    canceled = canceled || (progress && progress->isCanceled());

    if (!node.valid() && !canceled)
    {
        // Create a cursor to iterator over the feature data:
        Query query;
        query.tileKey() = tileKey;
        osg::ref_ptr<FeatureCursor> cursor = _features->createFeatureCursor(query);
        if (cursor.valid() && cursor->hasMore() && !canceled)
        {
            osg::ref_ptr<BuildingFactory> factory = new BuildingFactory();

            factory->setSession(_session.get());
            factory->setCatalog(_catalog.get());
            factory->setClamper(_clamper.get());
            factory->setOutputSRS(_session->getMapSRS());

            std::string styleName = Stringify() << tileKey.getLOD();
            const Style* style = _session->styles() ? _session->styles()->getStyle(styleName) : 0L;

            // Prepare the terrain envelope, for clamping.
            // TODO: review the LOD selection..
            OE_START_TIMER(envelope);
            osg::ref_ptr<TerrainEnvelope> envelope = factory->getClamper()->createEnvelope(tileKey.getExtent(), tileKey.getLOD());
            if (progress && progress->collectStats())
                progress->stats("pager.envelope") = OE_GET_TIMER(envelope);

            while (cursor->hasMore() && !canceled)
            {
                Feature* feature = cursor->nextFeature();
                numFeatures++;

                BuildingVector buildings;
                if (!factory->create(feature, tileKey.getExtent(), envelope.get(), style, buildings, readOptions, progress))
                {
                    canceled = true;
                }

                if (!canceled && !buildings.empty())
                {
                    if (output.getLocalToWorld().isIdentity())
                    {
                        output.setLocalToWorld(buildings.front()->getReferenceFrame());
                    }

                    // for indexing, if enabled:
                    output.setCurrentFeature(feature);

                    for (BuildingVector::iterator b = buildings.begin(); b != buildings.end() && !canceled; ++b)
                    {
                        if (!_compiler->compile(buildings, output, readOptions, progress))
                        {
                            canceled = true;
                        }
                    }
                }
            }

            if (!canceled)
            {
                // set the distance at which details become visible.
                osg::BoundingSphere tileBound = getBounds(tileKey);
                output.setRange(tileBound.radius() * getRangeFactor());
                node = output.createSceneGraph(_session.get(), _compilerSettings, readOptions, progress);
            }
            else
            {
                //OE_INFO << LC << "Tile " << tileKey.str() << " was canceled " << progress->message() << "\n";
            }
        }

        // This can go here now that we can serialize DIs and TBOs.
        if (node.valid() && !canceled)
        {
            OE_START_TIMER(postProcess);

            output.postProcess(node.get(), progress);

            if (progress && progress->collectStats())
                progress->stats("pager.postProcess") = OE_GET_TIMER(postProcess);
        }

        if (node.valid() && cacheWritesEnabled() && !canceled)
        {
            OE_START_TIMER(writeCache);

            output.writeToCache(node, _cacheBin.get(), progress);

            if (progress && progress->collectStats())
                progress->stats("pager.writeCache") = OE_GET_TIMER(writeCache);
        }
    }
    
#if 0
    if (node.valid() && !canceled)
    {
        OE_START_TIMER(postProcess);

        output.postProcess(node.get(), progress);

        if (progress && progress->collectStats())
            progress->stats("pager.postProcess") = OE_GET_TIMER(postProcess);
    }
#endif

    Registry::instance()->endActivity(activityName);

    double totalTime = OE_GET_TIMER(total);

    // collect statistics about the resulting scene graph:
    if (node.valid() && progress->collectStats())
    {
        osgUtil::StatsVisitor stats;
        node->accept(stats);
        progress->stats("# unique stateSets") = stats._statesetSet.size();
        progress->stats("# stateSet refs") = stats._numInstancedStateSet;
        progress->stats("# drawables") = stats._drawableSet.size();
    }

    // STATS:
    if ( progress && progress->collectStats() && !progress->stats().empty() && (fromCache ||numFeatures > 0))
    {
        std::stringstream buf;
        buf << "Key = " << tileKey.str() 
            << " : Features = " << numFeatures 
            << ", Time = " << (int)(1000.0*totalTime) 
            << " ms, Avg = " << std::setprecision(3) << (1000.0*(totalTime/(double)numFeatures)) << " ms"
            << std::endl;

        for(ProgressCallback::Stats::const_iterator i = progress->stats().begin(); i != progress->stats().end(); ++i)
        { 
            if (i->first.front() == '#')
            {
                buf
                    << "    " 
                    << std::setw(15) << i->first
                    << std::setw(10) << i->second
                    << std::endl;
            }
            else
            {
                buf
                    << "    " 
                    << std::setw(15) << i->first
                    << std::setw(6) << (int)(1000.0*i->second) << " ms"
                    << std::setw(6) << (int)(100.0*i->second/totalTime) << "%"
                    << std::endl;
            }
        }

        OE_INFO << LC << buf.str() << std::endl;

        // clear them when we are done.
        progress->stats().clear();
    }

    if (canceled)
    {
        OE_INFO << LC << "Building tile " << tileKey.str() << " - canceled\n";
        return 0L;
    }
    else
    {
        return node.release();
    }
}
