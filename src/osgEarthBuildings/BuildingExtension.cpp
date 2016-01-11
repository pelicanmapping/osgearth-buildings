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

//#include <osgEarth/Registry>
//#include <osgEarth/ShaderGenerator>
//#include <osgUtil/Optimizer>

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
    osg::ref_ptr<Session> session = new Session(0L);
    session->setStyles( _options.styles().get() );
    session->setResourceCache( new ResourceCache(_dbo.get()) );

    // Load the building catalog:
    osg::ref_ptr<BuildingCatalog> cat = new BuildingCatalog();
    if ( !cat->load(_options.buildingCatalog().get(), _dbo.get(), 0L) )
    {
        OE_WARN << LC << "Failed to load the buildings catalog\n";
        cat = 0L;
    }

    // Create building data model from features:
    osg::ref_ptr<BuildingFactory> factory = new BuildingFactory( session );
    factory->setCatalog( cat.get() );
    factory->setOutputSRS( mapNode->getMapSRS() );

    // Create a compiler that converts building models into nodes:
    osg::ref_ptr<BuildingCompiler> compiler = new BuildingCompiler( session );

    BuildingPager* pager = new BuildingPager( mapNode->getMap()->getProfile() );
    pager->setFeatureSource( features.get() );
    pager->setFactory      ( factory.get() );
    pager->setCompiler     ( compiler.get() );
    pager->setLOD          ( _options.lod().get() );
    pager->build();

    mapNode->addChild( pager );
    return true;
}

bool
BuildingExtension::disconnect(MapNode* mapNode)
{
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
