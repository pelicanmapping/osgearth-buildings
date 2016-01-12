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
#include "BuildingExtension"
#include "BuildingCatalog"
#include "BuildingFactory"
#include "BuildingCompiler"
#include "BuildingPager"

#include <osgEarth/Registry>

using namespace osgEarth;
using namespace osgEarth::Buildings;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;

#define LC "[BuildingExtension] "


BuildingExtension::BuildingExtension()
{
    //nop
}

BuildingExtension::BuildingExtension(const BuildingOptions& options) :
_options(options)
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
    _dbo = dbOptions;
}

bool
BuildingExtension::connect(MapNode* mapNode)
{
    if ( !mapNode ) 
        return false;

    OE_START_TIMER(start);

    // Load the features source.
    osg::ref_ptr<FeatureSource> features = FeatureSourceFactory::create(_options.featureOptions().get());
    if ( !features.valid() )
    {
        OE_WARN << LC << "Failed to create feature source\n";
        return false;
    }
    
    // fire it up
    features->initialize(_dbo.get());

    // Set up a feature session with a cache:
    osg::ref_ptr<Session> session = new Session( mapNode->getMap(), _options.styles().get(), features, _dbo.get() );
    session->setResourceCache( new ResourceCache(_dbo.get()) );

    // Load the building catalog:
    osg::ref_ptr<BuildingCatalog> catalog = new BuildingCatalog();
    if ( !catalog->load(_options.buildingCatalog().get(), _dbo.get(), 0L) )
    {
        OE_WARN << LC << "Failed to load the buildings catalog\n";
        catalog = 0L;
    }

    // Open a cache bin, if a cache is active.
    initializeCaching();

    BuildingPager* pager = new BuildingPager( features->getFeatureProfile()->getProfile() );
    pager->setSession      ( session.get() );
    pager->setFeatureSource( features.get() );
    pager->setCatalog      ( catalog.get() );
    pager->setLOD          ( _options.lod().get() );
    pager->setCacheBin     ( _cacheBin.get(), _cachePolicy.get() );
    pager->build();

    mapNode->addChild( pager );    
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

void
BuildingExtension::initializeCaching()
{
    if ( _dbo.valid() )
    {
        // check for an incoming cache policy:
        CachePolicy::fromOptions(_dbo.get(), _cachePolicy);

        // check for an overriding cache policy in this extension:
        if ( _options.cachePolicy().isSet() )
        {
            _cachePolicy->mergeAndOverride( _options.cachePolicy() );
        }

        // finally resolve with global overrides:
        Registry::instance()->resolveCachePolicy( _cachePolicy );

        if ( _cachePolicy != CachePolicy::NO_CACHE )
        {
            Cache* cache = Cache::get(_dbo.get());
            if ( cache )
            {
                Config conf = _options.getConfig();
                conf.remove( "cache_policy" );
                std::string hash = Stringify() << "buildings." << hashString(conf.toJSON(false));
                _cacheBin = cache->addBin( hash );
            }
        }                
    }
    else
    {
        OE_WARN << LC << "DBO not set; no caching\n";
    }
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
