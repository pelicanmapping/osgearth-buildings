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

#include <osgEarth/Registry>
#include <osgEarth/ShaderGenerator>

#include <osgUtil/Optimizer>

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

    // Create a cursor to iterator over the feature data:
    osg::ref_ptr<FeatureCursor> cursor = features->createFeatureCursor();
    if ( !cursor.valid() )
    {
        OE_WARN << LC << "Failed to open a cursor from input file\n";
        return false;
    }

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

    BuildingVector buildings;
    if ( !factory->create(cursor.get(), buildings) )
    {
        OE_WARN << LC << "Failed to create building data model\n";
        return false;
    }
    OE_INFO << LC << "Created " << buildings.size() << " buildings in " << std::setprecision(3) << OE_GET_TIMER(start) << "s" << std::endl;

    // Create OSG model from buildings.
    OE_START_TIMER(compile);
    osg::ref_ptr<BuildingCompiler> compiler = new BuildingCompiler( session );
    osg::ref_ptr<osg::Node> node = compiler->compile(buildings);            
    OE_INFO << LC << "Compiled " << buildings.size() << " buildings in " << std::setprecision(3) << OE_GET_TIMER(compile) << "s" << std::endl;
    
    if ( !node.valid() )
    {
        OE_WARN << LC << "Failed to compile node\n";
        return false;
    }

    OE_START_TIMER(optimize);
#if 1
    // Note: FLATTEN_STATIC_TRANSFORMS is bad for geospatial data
    osgUtil::Optimizer o;
    o.optimize( node, o.DEFAULT_OPTIMIZATIONS & (~o.FLATTEN_STATIC_TRANSFORMS) );
                
    node->setDataVariance( node->DYNAMIC ); // keeps the OSG optimizer from 
#else
    osgUtil::Optimizer::MergeGeometryVisitor mgv;
    mgv.setTargetMaximumNumberOfVertices(1024000);
    node->accept( mgv );
    node->setDataVariance( node->DYNAMIC );
#endif

    OE_INFO << LC << "Optimized in " << std::setprecision(3) << OE_GET_TIMER(optimize) << "s" << std::endl;
    OE_INFO << LC << "Total time = " << OE_GET_TIMER(start) << "s" << std::endl;

    Registry::instance()->shaderGenerator().run( node.get() );

    _root = new osg::Group();
    mapNode->addChild( node.get() );
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
