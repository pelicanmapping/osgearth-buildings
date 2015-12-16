/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2014 Pelican Mapping
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

#include <osgViewer/Viewer>
#include <osgEarth/Notify>
#include <osgEarthUtil/EarthManipulator>
#include <osgEarthUtil/ExampleResources>
#include <osgearthAerodrome/AerodromeFactory>
#include <osgearthAerodrome/AerodromeRenderer>
#include <osgEarthFeatures/BuildGeometryFilter>

#define LC "[viewer] "

using namespace osgEarth;
using namespace osgEarth::Util;
using namespace osgEarth::Aerodrome;

class RedLineRenderer : public AerodromeRenderer
{
public:
  RedLineRenderer() : AerodromeRenderer() { }

  virtual void apply(LinearFeatureNode& node) //override
  {
      osg::ref_ptr<osgEarth::Features::Feature> feature = node.getFeature();

      osg::ref_ptr<osg::Node> geom;

      osg::ref_ptr<osg::Vec3dArray> geomPoints = feature->getGeometry()->createVec3dArray();
      if (geomPoints.valid() && geomPoints->size() >= 2)
      {
          // localize geometry vertices
          std::vector<osg::Vec3d> featurePoints;
          for (int i=0; i < geomPoints->size(); i++)
          {
              featurePoints.push_back(osg::Vec3d((*geomPoints)[i].x(), (*geomPoints)[i].y(), _elevation));
          }

          osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array();
          transformAndLocalize(featurePoints, _map->getSRS(), verts, 0L);

          osg::ref_ptr< Feature > clone = new Feature(*feature, osg::CopyOp::DEEP_COPY_ALL);
          clone->getGeometry()->clear();

          for(int i=0; i<verts->size(); ++i)
          {
              (*verts)[i].z() = 0.0;

              clone->getGeometry()->push_back((*verts)[i].x(), (*verts)[i].y(), 0.0);
          }

          // setup the style
          Style style;
          style.getOrCreate<LineSymbol>()->stroke()->color() = Color(1.0f, 0.0f, 0.0f, 0.8f);
          style.getOrCreate<LineSymbol>()->stroke()->width() = 1.0f;
          style.getOrCreate<LineSymbol>()->stroke()->widthUnits() = Units::METERS;

          style.getOrCreate<AltitudeSymbol>()->clamping() = AltitudeSymbol::CLAMP_NONE;

          // use the BuildGeometryFilter to render the linear feature
          BuildGeometryFilter filter( style );

          FeatureList workingSet;
          workingSet.push_back(clone);

          FilterContext context;
          osg::Node* filterNode = filter.push( workingSet, context );

          if (filterNode)
          {
              osg::MatrixTransform* mt = new osg::MatrixTransform();
              mt->setMatrix(_local2world);
              mt->addChild(filterNode);

              geom = mt;
          }
      }

      if (geom.valid())
      {
          node.addChild(geom);
      }
  }
};

int
usage(const char* name)
{
    OE_DEBUG 
        << "\nUsage: " << name << " file.earth" << std::endl
        << MapNodeHelper().usage() << std::endl;

    return 0;
}

int
main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);

    // help?
    if ( arguments.read("--help") )
        return usage(argv[0]);

    if ( arguments.read("--stencil") )
        osg::DisplaySettings::instance()->setMinimumNumStencilBits( 8 );

    // create a viewer:
    osgViewer::Viewer viewer(arguments);

    // Tell the database pager to not modify the unref settings
    viewer.getDatabasePager()->setUnrefImageDataAfterApplyPolicy( false, false );

    // install our default manipulator (do this before calling load)
    viewer.setCameraManipulator( new EarthManipulator() );

    // override default renderer
    //osgEarth::Aerodrome::AerodromeFactory::setDefaultRenderer(new RedLineRenderer());  // UNCOMMENT THIS LINE TO TEST A CUSTOM RENDERER

    // load an earth file, and support all or our example command-line options
    // and earth file <external> tags    
    osg::Group* node = MapNodeHelper().load( arguments, &viewer );
    if ( node )
    {
        viewer.getCamera()->setNearFarRatio(0.00002);
        viewer.getCamera()->setSmallFeatureCullingPixelSize(-1.0f);

        viewer.setSceneData( node );

        return viewer.run();
    }
    else
    {
        return usage(argv[0]);
    }
}
