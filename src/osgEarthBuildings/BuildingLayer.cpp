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
#include "BuildingLayer"
#include "BuildingCatalog"
#include "BuildingFactory"
#include "BuildingCompiler"
#include "BuildingPager"

#include <osgEarth/Registry>
#include <osgEarthFeatures/FeatureSourceIndexNode>

using namespace osgEarth;
using namespace osgEarth::Buildings;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;

#define LC "[BuildingLayer] "

REGISTER_OSGEARTH_LAYER(buildings, BuildingLayer);

//...................................................................

BuildingLayer::BuildingLayer() :
VisibleLayer(&_optionsConcrete),
_options(&_optionsConcrete)
{
    init();
}

BuildingLayer::BuildingLayer(const BuildingLayerOptions& options) :
VisibleLayer(&_optionsConcrete),
_options(&_optionsConcrete),
_optionsConcrete(options)
{
    init();
}

BuildingLayer::~BuildingLayer()
{
    //nop
}

void
BuildingLayer::init()
{
    VisibleLayer::init();

    // Callbacks for paged data
    _sgCallbacks = new SceneGraphCallbacks();

    // Create the root group
    _root = new osg::Group();
    _root->setName(getName());
}

void
BuildingLayer::setFeatureSource(FeatureSource* source)
{
    if (_featureSource != source)
    {
        if (source)
            OE_INFO << LC << "Setting feature source \"" << source->getName() << "\"\n";

        _featureSource = source;

        // make sure the source is not in an error state
        if (source && source->getStatus().isError())
        {
            setStatus(source->getStatus());
            return;
        }

        createSceneGraph();
    }
}

osg::Node*
BuildingLayer::getOrCreateNode()
{
    return _root.get();
}

const Status&
BuildingLayer::open()
{
    // Attempt to load the feature data source
    if (options().featureSource().isSet())
    {
        FeatureSource* fs = FeatureSourceFactory::create(options().featureSource().get());
        if (fs)
        {
            fs->setReadOptions(getReadOptions());
            fs->open();
            setFeatureSource(fs);
        }
        else
        {
            setStatus(Status(Status::ResourceUnavailable, "Cannot access feature source"));
        }
    }
    else
    {
        setStatus(Status(Status::ConfigurationError, "Missing required feature source"));
    }

    if (options().buildingCatalog().isSet())
    {
        _catalog = new BuildingCatalog();
        if (_catalog->load(options().buildingCatalog().get(), getReadOptions(), 0L) == false)
        {
            setStatus(Status(Status::ResourceUnavailable, "Cannot open building catalog"));
            _catalog = 0L;
        }
    }
    else
    {
        setStatus(Status(Status::ConfigurationError, "Missing required catalog"));
    }

    return VisibleLayer::open();
}

void
BuildingLayer::addedToMap(const Map* map)
{
    // Hang on to the Map reference
    _map = map;

    // Set up a feature session with a cache:
    _session = new Session(
        map, 
        options().styles().get(),
        _featureSource.get(),
        getReadOptions() );
    
    // Install a resource cache that we will use for instanced models,
    // but not for skins; b/c we want to cache skin statesets per tile. So there is
    // a separate resource cache in the CompilerOutput class for that.
    _session->setResourceCache( new ResourceCache() );

    // Recreate the scene graph
    createSceneGraph();
}

void
BuildingLayer::createSceneGraph()
{
    const Profile* profile = 0L;

    // reinitialize the graph:
    _root->removeChildren(0, _root->getNumChildren());

    // resolve observer reference:
    osg::ref_ptr<const Map> map;
    _map.lock(map);

    // assertion:
    if (!_featureSource.valid() || !_session.valid() || !map.valid())
    {
        //if (getStatus().isOK())
        //    setStatus(Status(Status::ServiceUnavailable, "Internal assertion failure, call support"));
        return;
    }
    
    // Try to page against the feature profile, otherwise fallback to the map
    if (_featureSource.valid())
    {
         profile = _featureSource->getFeatureProfile()->getProfile();
    }
    if (profile == 0L)
    {
        profile = _map->getProfile();
    }

    BuildingPager* pager = new BuildingPager( profile );
    pager->setElevationPool   ( _map->getElevationPool() );
    pager->setSession         ( _session.get() );
    pager->setFeatureSource   ( _featureSource.get() );
    pager->setCatalog         ( _catalog.get() );
    pager->setCompilerSettings( options().compilerSettings().get() );
    pager->setPriorityOffset  ( options().priorityOffset().get() );
    pager->setPriorityScale   ( options().priorityScale().get() );

    if (options().enableCancelation().isSet())
    {
        pager->setEnableCancelation(options().enableCancelation().get());
    }

    pager->build();

    if ( options().createIndex() == true )
    {
        // create a feature index.
        FeatureSourceIndex* index = new FeatureSourceIndex(
            _featureSource.get(),
            Registry::objectIndex(),
            FeatureSourceIndexOptions() );

        // ..and a node to house it.
        FeatureSourceIndexNode* inode = new FeatureSourceIndexNode( index );

        // tell the pager to generate an index
        pager->setIndex( inode );

        // install in the scene graph.
        inode->addChild( pager );
        _root->addChild( inode );
    }

    else
    {
        _root->addChild( pager );
    }
}

void
BuildingLayer::removedFromMap(const Map* map)
{
    // nop
}

const GeoExtent&
BuildingLayer::getExtent() const
{
    if (_featureSource.valid() && _featureSource->getFeatureProfile())
    {
        return _featureSource->getFeatureProfile()->getExtent();
    }

    osg::ref_ptr<const Map> map;
    if (_map.lock(map))
    {
        return map->getProfile()->getExtent();
    }

    return GeoExtent::INVALID;
}

#if 0




BuildingExtension::BuildingExtension(const BuildingOptions& options) :
BuildingOptions(options)
{
    //nop
}

BuildingExtension::~BuildingExtension()
{
    //nop
}

void
BuildingExtension::setDBOptions(const osgDB::Options* dbOptions)
{
    _readOptions = Registry::cloneOrCreateOptions(dbOptions);

    CacheSettings* oldSettings = CacheSettings::get(_readOptions.get());
    CacheSettings* newSettings = oldSettings ? new CacheSettings(*oldSettings) : new CacheSettings();

    // incorporate this object's cache policy, if it is set. By default, the buildings extension's 
    // caching is OFF and you must expressly turn it on.
    if (cachePolicy().isSet())
    {
        newSettings->integrateCachePolicy(cachePolicy());
    }
    else
    {
        OE_INFO << LC << "Cache policy not set; defaulting to READ ONLY.\n";
        //newSettings->cachePolicy() = CachePolicy::NO_CACHE;
        newSettings->cachePolicy() = CachePolicy::USAGE_READ_ONLY;
    }

    // finally, if cacheing is a go, make a bin.
    if (newSettings->isCacheEnabled())
    {
        Config conf = getConfig();
        conf.remove("cache_policy");
        std::string binName;
        if (cacheId().isSet() && !cacheId()->empty())
            binName = cacheId().get();
        else
            binName = hashToString(conf.toJSON(false));

        newSettings->setCacheBin(newSettings->getCache()->addBin(binName));

        OE_INFO << LC << "Cache bin is [" << binName << "]\n";
    }

    newSettings->store(_readOptions.get());
}

bool
BuildingExtension::connect(MapNode* mapNode)
{
    if ( !mapNode ) 
        return false;

    OE_START_TIMER(start);

    // Load the features source.
    osg::ref_ptr<FeatureSource> features = FeatureSourceFactory::create(featureOptions().get());
    if ( !features.valid() )
    {
        OE_WARN << LC << "Failed to create feature source\n";
        return false;
    }
    
    // Open the building feature source:
    const Status& status = features->open(_readOptions.get());
    if (status.isError())
    {
        OE_WARN << LC << "Failed to open feature data: " << status.message() << std::endl;
        return false;
    }

    // Set up a feature session with a cache:
    osg::ref_ptr<Session> session = new Session( mapNode->getMap(), styles().get(), features.get(), _readOptions.get() );
    
    // Install a resource cache that we will use for instanced models,
    // but not for skins; b/c we want to cache skin statesets per tile. So there is
    // a separate resource cache in the CompilerOutput class for that.
    session->setResourceCache( new ResourceCache() );

    // Load the building catalog:
    osg::ref_ptr<BuildingCatalog> catalog = new BuildingCatalog();
    if ( !catalog->load(buildingCatalog().get(), _readOptions.get(), 0L) )
    {
        OE_WARN << LC << "Failed to load the buildings catalog\n";
        catalog = 0L;
    }

    // Try to page against the feature profile, otherwise fallback to the map
    const Profile* featureProfile = features->getFeatureProfile()->getProfile();
    if (!featureProfile)
    {
        featureProfile = mapNode->getMap()->getProfile();
    }

    OE_INFO << LC << CacheSettings::get(_readOptions.get())->toString() << "\n";

    BuildingPager* pager = new BuildingPager( featureProfile );
    pager->setElevationPool   ( mapNode->getMap()->getElevationPool() );
    pager->setSession         ( session.get() );
    pager->setFeatureSource   ( features.get() );
    pager->setCatalog         ( catalog.get() );
    pager->setCompilerSettings( compilerSettings().get() );
    pager->setPriorityOffset  ( priorityOffset().get() );
    pager->setPriorityScale   ( priorityScale().get() );

    if (enableCancelation().isSet())
        pager->setEnableCancelation(enableCancelation().get());

    pager->build();

    if ( createIndex() == true )
    {
        // create a feature index.
        FeatureSourceIndex* index = new FeatureSourceIndex(
            features.get(),
            Registry::objectIndex(),
            FeatureSourceIndexOptions() );

        // ..and a node to house it.
        FeatureSourceIndexNode* inode = new FeatureSourceIndexNode( index );

        // tell the pager to generate an index
        pager->setIndex( inode );

        // install in the scene graph.
        inode->addChild( pager );
        mapNode->addChild( inode );
        _root = inode;
    }

    else
    {
        // install in the scene graph.
        mapNode->addChild( pager ); 
        _root = pager;
    }

    // store the pager pointer for the getter.
    _pager = pager;

    return true;
}

bool
BuildingExtension::disconnect(MapNode* mapNode)
{
    //TODO, fix me.
    if ( mapNode && _root.valid() )
        mapNode->removeChild( _root.get() );
    return true;
}

//.........................................................

#include <osgDB/FileNameUtils>

#undef  LC
#define LC "[BuildingPlugin] "

namespace osgEarth { namespace Buildings
{
    class BuildingPlugin : public osgDB::ReaderWriter
    {
    public: // Plugin stuff

        BuildingPlugin() {
            supportsExtension( "osgearth_buildings", "osgEarth Buildings Extension Plugin" );
        }
        
        const char* className() {
            return "osgEarth Buildings Extension Plugin";
        }

        virtual ~BuildingPlugin() { }

        ReadResult readObject(const std::string& filename, const osgDB::Options* dbOptions) const
        {
          if ( !acceptsExtension(osgDB::getLowerCaseFileExtension(filename)) )
                return ReadResult::FILE_NOT_HANDLED;

          OE_INFO << LC << "Loaded buildings extension!\n";

          return ReadResult( new BuildingExtension(Extension::getConfigOptions(dbOptions)) );
        }
    };

    REGISTER_OSGPLUGIN(osgearth_buildings, BuildingPlugin);
} }


#endif