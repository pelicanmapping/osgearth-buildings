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
#include "Common"
#include "BuildingFactory"
#include "BuildingCompiler"

#include <osgDB/FileNameUtils>
#include <osgDB/Registry>
#include <osgUtil/Optimizer>

#include <osgEarth/Registry>
#include <osgEarthFeatures/FeatureSource>
#include <osgEarthDrivers/feature_ogr/OGRFeatureOptions>

#define LC "[Building Plugin] "

#define PLUGIN_EXTENTION "building"

using namespace osgEarth::Drivers;


namespace osgEarth { namespace Buildings
{
    struct FeaturePlugin : public osgDB::ReaderWriter
    {
        FeaturePlugin()
        {
            this->supportsExtension( PLUGIN_EXTENTION, "osgEarthBuildings Feature Plugin" );
        }

        const char* className()
        {
            return "osgEarthBuildings Feature Plugin";
        }

        bool acceptsExtension(const std::string& extension) const
        {
            return osgDB::equalCaseInsensitive( extension, PLUGIN_EXTENTION );
        }

        ReadResult readObject(const std::string& filename, const osgDB::Options* options) const
        {
            return readNode( filename, options );
        }

        ReadResult readNode(const std::string& filename, const osgDB::Options* options) const
        {
            if ( !acceptsExtension(osgDB::getFileExtension(filename)) )
                return ReadResult::FILE_NOT_HANDLED;

            OE_START_TIMER(start);

            std::string inputFile = osgDB::getNameLessExtension(filename);
            OE_INFO << LC << "Input = " << inputFile << "\n";

            // Try to open as a feature source:
            OGRFeatureOptions ogr;
            ogr.url() = inputFile;
            osg::ref_ptr<FeatureSource> fs = FeatureSourceFactory::create( ogr );
            if ( !fs.valid() )
            {
                OE_WARN << LC << "Failed to create feature soruce from input file\n";
                return ReadResult::FILE_NOT_FOUND;
            }
            fs->initialize(options);

            // Create a cursor to iterator over the feature data:
            osg::ref_ptr<FeatureCursor> cursor = fs->createFeatureCursor();
            if ( !cursor.valid() )
            {
                OE_WARN << LC << "Failed to open a cursor from input file\n";
                return ReadResult::ERROR_IN_READING_FILE;
            }            
            OE_INFO << LC << "Loaded feature data from " << inputFile << "\n";

            // Load a resource catalog.
            ResourceLibrary* reslib = new ResourceLibrary("", "repo/data/catalog/catalog.xml");
            if ( !reslib->initialize( options ) )
            {
                OE_WARN << LC << "Failed to load a resource library\n";
            }

            StyleSheet* sheet = new StyleSheet();
            sheet->addResourceLibrary( reslib );

            BuildingSymbol* sym = sheet->getDefaultStyle()->getOrCreate<BuildingSymbol>();
            sym->height() = NumericExpression("max(3.5*[story_ht_],1.0)");

            Session* session = new Session(0L);
            session->setStyles( sheet );
            session->setResourceCache( new ResourceCache(options) );

            // Create building data model from features:
            osg::ref_ptr<BuildingFactory> factory = new BuildingFactory( session );

            BuildingVector buildings;
            if ( !factory->create(cursor.get(), buildings) )
            {
                OE_WARN << LC << "Failed to create building data model\n";
                return ReadResult::ERROR_IN_READING_FILE;
            }
            OE_INFO << LC << "Created " << buildings.size() << " buildings in " << std::setprecision(3) << OE_GET_TIMER(start) << "s" << std::endl;

            // Create OSG model from buildings.
            OE_START_TIMER(compile);
            osg::ref_ptr<BuildingCompiler> compiler = new BuildingCompiler( session );
            osg::Node* node = compiler->compile(buildings);            
            OE_INFO << LC << "Compiled " << buildings.size() << " buildings in " << std::setprecision(3) << OE_GET_TIMER(compile) << "s" << std::endl;

            if ( node )
            {
                OE_START_TIMER(optimize);

                osgUtil::Optimizer::MergeGeometryVisitor mgv;
                mgv.setTargetMaximumNumberOfVertices(1024000);
                node->accept( mgv );

                OE_INFO << LC << "Optimized in " << std::setprecision(3) << OE_GET_TIMER(optimize) << "s" << std::endl;

                OE_INFO << LC << "Total time = " << OE_GET_TIMER(start) << "s" << std::endl;
                return node;
            }
            else
            {
                return ReadResult::ERROR_IN_READING_FILE;
            }
        }
    };

    REGISTER_OSGPLUGIN(PLUGIN_EXTENTION, FeaturePlugin);

} }
