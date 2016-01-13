
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
#include "FlatRoofCompiler"
#include <osgEarth/Tessellator>
#include <osgEarth/Random>
#include <osgEarthFeatures/Session>
#include <osgEarthSymbology/MeshConsolidator>
#include <osgUtil/Tessellator>
#include <osg/MatrixTransform>
#include <osg/ComputeBoundsVisitor>
#include <osg/Program>

using namespace osgEarth;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

#define LC "[FlatRoofCompiler] "

namespace
{
    static bool s_debug = ::getenv("OSGEARTH_BUILDINGS_DEBUG") != 0L;

    osg::Node* createModelBoxGeom(const osg::Vec3d* box, const osg::Matrix& frame, float z)
    {
        osg::Vec3Array* v = new osg::Vec3Array();
        for(int i=0; i<4; ++i) v->push_back( box[i] + osg::Vec3(0,0,z+0.25) );

        osg::Vec4Array* c = new osg::Vec4Array();
        c->push_back(osg::Vec4(1,0,0,1));

        osg::Vec3Array* n = new osg::Vec3Array();
        n->push_back(osg::Vec3(0,0,1));

        osg::Geometry* g = new osg::Geometry();
        g->setUseVertexBufferObjects(true);
        g->setUseDisplayList(false);

        g->setVertexArray( v );
        g->setColorArray( c );
        g->setColorBinding( g->BIND_OVERALL );
        g->setNormalArray( n );
        g->setNormalBinding( g->BIND_OVERALL );
        g->addPrimitiveSet( new osg::DrawArrays(GL_LINE_LOOP, 0, 4) );

        osg::Geode* d = new osg::Geode();
        d->addDrawable( g );

        osg::MatrixTransform* m = new osg::MatrixTransform(frame);
        m->addChild( d );
        m->getOrCreateStateSet()->setAttribute(new osg::Program(),1);
        return m;
    }
}

bool
FlatRoofCompiler::compile(const Building*    building,
                          const Elevation*   elevation, 
                          osg::Geode*        geode,
                          osg::Group*        models,
                          const osg::Matrix& world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;
    if ( !elevation ) return false;
    if ( !elevation->getRoof() ) return false;

    const Roof* roof = elevation->getRoof();

    // precalculate the frame transformation; combining these will
    // prevent any precision loss during the transform.
    osg::Matrix frame = building->getReferenceFrame() * world2local;

    bool genColors = false;   // TODO

    // find a texture:
    SkinResource* skin = roof->getSkinResource();
    osg::ref_ptr<osg::StateSet> stateSet;
    if ( skin )
    {
        if ( _session->getResourceCache() )
        {
            _session->getResourceCache()->getOrCreateStateSet(skin, stateSet);
        }
    }

    // Build a flat roof.
    osg::ref_ptr<osg::Geometry> geom = new osg::Geometry();
    geom->setUseVertexBufferObjects( true );
    geom->setUseDisplayList( false );

    osg::Vec3Array* verts = new osg::Vec3Array();
    geom->setVertexArray( verts );

    osg::Vec4Array* colors = 0L;
    if ( genColors )
    {
        colors = new osg::Vec4Array();
        geom->setColorArray( colors );
        geom->setColorBinding( geom->BIND_PER_VERTEX );
    }

    osg::Vec3Array* texCoords = 0L;
    if ( stateSet.valid() )
    {
        texCoords = new osg::Vec3Array();
        geom->setTexCoordArray( 0, texCoords );
        geom->setStateSet( stateSet.get() );
    }

    //osg::Vec4Array* anchors = 0L;
    //if ( _gpuClamping )
    //{
    //    // fake out the OSG tessellator. It does not preserve attrib arrays in the Tessellator.
    //    // so we will put them in one of the texture arrays and copy them to an attrib array 
    //    // after tessellation. #osghack
    //    anchors = new osg::Vec4Array();
    //    roof->setTexCoordArray(1, anchors);
    //}

    float roofZ = 0.0f;

    // Create a series of line loops that the tessellator can reorganize into polygons.
    unsigned vertptr = 0;
    for(Elevation::Walls::const_iterator wall = elevation->getWalls().begin();
        wall != elevation->getWalls().end();
        ++wall)
    {
        unsigned elevptr = vertptr;
        for(Elevation::Faces::const_iterator f = wall->faces.begin(); f != wall->faces.end(); ++f)
        {
            // Only use source verts; we skip interim verts inserted by the 
            // structure building since they are co-linear anyway and thus we don't
            // need them for the roof line.
            if ( f->left.isFromSource )
            {
                verts->push_back( f->left.upper );
                roofZ = f->left.upper.z();

                if ( colors )
                {
                    colors->push_back( roof->getColor() );
                }

                if ( texCoords )
                {
                    texCoords->push_back( osg::Vec3f(f->left.roofUV.x(), f->left.roofUV.y(), (float)0.0f) );
                }

#if 0
                if ( anchors )
                {
                    float 
                        x = structure.baseCentroid.x(),
                        y = structure.baseCentroid.y(), 
                        vo = structure.verticalOffset;

                    if ( flatten )
                    {
                        anchors->push_back( osg::Vec4f(x, y, vo, Clamping::ClampToAnchor) );
                    }
                    else
                    {
                        anchors->push_back( osg::Vec4f(x, y, vo + f->left.height, Clamping::ClampToGround) );
                    }
                }
#endif

                ++vertptr;
            }
        }
        geom->addPrimitiveSet( new osg::DrawArrays(GL_LINE_LOOP, elevptr, vertptr-elevptr) );
    } 

    osg::Vec3Array* normal = new osg::Vec3Array(verts->size());
    geom->setNormalArray( normal );
    geom->setNormalBinding( osg::Geometry::BIND_PER_VERTEX );
    normal->assign( verts->size(), osg::Vec3(0,0,1) );
    
    // Tessellate the roof lines into polygons.
    osgEarth::Tessellator oeTess;
    if (!oeTess.tessellateGeometry(*geom))
    {
        //fallback to osg tessellator
        OE_DEBUG << LC << "Falling back on OSG tessellator (" << geom->getName() << ")" << std::endl;

        osgUtil::Tessellator tess;
        tess.setTessellationType( osgUtil::Tessellator::TESS_TYPE_GEOMETRY );
        tess.setWindingType( osgUtil::Tessellator::TESS_WINDING_ODD );
        tess.retessellatePolygons( *geom );
        MeshConsolidator::convertToTriangles( *geom );
    }

#if 0
    // Move the anchors to the correct place. :)
    if ( _gpuClamping )
    {
        osg::Vec4Array* a = static_cast<osg::Vec4Array*>(roof->getTexCoordArray(1));
        if ( a )
        {
            roof->setVertexAttribArray    ( Clamping::AnchorAttrLocation, a );
            roof->setVertexAttribBinding  ( Clamping::AnchorAttrLocation, osg::Geometry::BIND_PER_VERTEX );
            roof->setVertexAttribNormalize( Clamping::AnchorAttrLocation, false );
            roof->setTexCoordArray(1, 0L);
        }
    }
#endif

    // Transform into the final frame:
    for(osg::Vec3Array::iterator v = verts->begin(); v != verts->end(); ++v)
        (*v) = (*v) * frame;

    geode->addDrawable( geom.get() );
    
    // Load models:
    ModelResource* model = roof->getModelResource();
    if ( model && roof->hasModelBox() )
    {
        osg::ref_ptr<osg::Node> node;
        if ( _session->getResourceCache() )
        {
            _session->getResourceCache()->getOrCreateInstanceNode(model, node);
        }

        if ( node.valid() )
        {
            const osg::Vec3d* modelBox = roof->getModelBox();
            osg::Vec3d rbox[4];
            osg::BoundingBox space;
            for(int i=0; i<4; ++i) { 
                rbox[i] = modelBox[i];
                roof->getParent()->rotate(rbox[i]);
                space.expandBy(rbox[i]);
            }

            osg::ComputeBoundsVisitor cb;
            node->accept( cb );

#if 0
            for(int i=0; i<4; ++i)
            {
                osg::Vec3d p = modelBox[i];
                p.z() = roofZ - cb.getBoundingBox().zMin();
                osg::MatrixTransform* xform = new osg::MatrixTransform();
                xform->setMatrix( roof->getParent()->getRotation() * frame * osg::Matrix::translate(p) );
                xform->addChild( node.get() );
                models->addChild( xform );
            }
#else
            // calculate a pseudo-random offset.
            float spaceWidth = space.xMax() - space.xMin();
            float spaceHeight = space.yMax() - space.yMin();

            float modelWidth = cb.getBoundingBox().xMax() - cb.getBoundingBox().xMin();
            float modelHeight = cb.getBoundingBox().yMax() - cb.getBoundingBox().yMin();
            
            if ( modelWidth < spaceWidth && modelHeight < spaceHeight )
            {
                Random prng(building->getUID());
                float x = prng.next();
                float y = prng.next();

                float maxOffsetX = spaceWidth-modelWidth;
                float maxOffsetY = spaceHeight-modelHeight;
                float dx = prng.next()*maxOffsetX - 0.5*maxOffsetX;
                float dy = prng.next()*maxOffsetY - 0.5*maxOffsetY;

                osg::Vec3d p = space.center() + osg::Vec3d(dx, dy, 0);
                roof->getParent()->unrotate( p );
                p.z() = roofZ - cb.getBoundingBox().zMin();

                osg::MatrixTransform* xform = new osg::MatrixTransform();
                xform->setMatrix( roof->getParent()->getRotation() * frame * osg::Matrix::translate(p) );
                xform->addChild( node.get() );
                models->addChild( xform );
            }
#endif

            if ( s_debug )
            {
                models->addChild( createModelBoxGeom(modelBox, frame, roofZ) );
            }

        }
        else
        {
            OE_WARN << LC << "Model resource set, but couldn't find model\n";
        }
    }

    return true;
}
