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
#include "BuildingCompiler"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/MatrixTransform>
#include <osgUtil/SmoothingVisitor>
#include <osgUtil/Tessellator>
#include <osgEarth/Tessellator>
#include <osgEarthSymbology/MeshConsolidator>

#define LC "[BuildingCompiler] "

using namespace osgEarth;
using namespace osgEarth::Buildings;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;


BuildingCompiler::BuildingCompiler()
{
    // nop
}

osg::Node*
BuildingCompiler::compile(Building*         building,
                          ProgressCallback* progress)
{
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform();

    osg::Geode* geode = new osg::Geode();
    root->addChild( geode );

    addSimpleFootprint( geode, building, osg::Matrix::identity() );

    return root.release();
}

osg::Node*
BuildingCompiler::compile(const BuildingVector& input,
                          ProgressCallback*     progress)
{    
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform();

    // Use the first building as our global reference frame. In usual practice,
    // we will probably have a anchor point for a group of buildings (a tile)
    // that we can pass in here and use.
    osg::Matrix local2world, world2local;
    if ( !input.empty() )
    {
        local2world = input.front()->getReferenceFrame();
    }
    world2local.invert(local2world);
    root->setMatrix( local2world );

    osg::Geode* geode = new osg::Geode();
    root->addChild( geode );

    for(BuildingVector::const_iterator i = input.begin(); i != input.end(); ++i)
    {
        Building* building = i->get();
        //addSimpleFootprint( geode, building, world2local );
        addElevations( geode, building, world2local );
        addRoof( geode, building, world2local );
    }

    return root.release();
}

bool
BuildingCompiler::addBuilding(osg::Geode*        geode,
                              Building*          building,
                              const osg::Matrix& world2local) const
{
    return addSimpleFootprint(geode, building, world2local);
}

bool
BuildingCompiler::addSimpleFootprint(osg::Geode*        geode,
                                     Building*          building,
                                     const osg::Matrix& world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;

    Polygon* fp = building->getFootprint();

    osg::Geometry* geom = new osg::Geometry();
    geom->setUseVertexBufferObjects( true );
    geom->setUseDisplayList( false );

    osg::Vec3Array* v = new osg::Vec3Array();
    geom->setVertexArray( v );

    GeometryIterator iter(fp);
    while( iter.hasMore() )
    {
        int ptr = v->size();
        Geometry* poly = iter.next();
        for(Geometry::iterator p = poly->begin(); p != poly->end(); ++p)
        {
            v->push_back( (*p) * (building->getReferenceFrame() * world2local) );
        }
        geom->addPrimitiveSet( new osg::DrawArrays(GL_LINE_LOOP, ptr, poly->size()) );
    }

    geode->addDrawable( geom );

    return true;
}

bool
BuildingCompiler::addElevations(osg::Geode*        geode,
                                Building*          building,
                                const osg::Matrix& world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;

    // precalculate the frame transformation:
    osg::Matrix frame = building->getReferenceFrame() * world2local;

    // Iterator over each Elevation in this building:
    for(ElevationVector::iterator e = building->getElevations().begin();
        e != building->getElevations().end();
        ++e)
    {
        const Elevation* elevation = e->get();

        double texWidthM   = 1.0; // TODO wallSkin ? *wallSkin->imageWidth()  : 1.0;
        double texHeightM  = 1.0; // TODOwallSkin ? *wallSkin->imageHeight() : 1.0;
        bool   genColors   = true; // TODO (!wallSkin || wallSkin->texEnvMode() != osg::TexEnv::DECAL) && !_makeStencilVolume;
        bool   useTexture  = false; // TODO
        bool   genNormals  = false; // TODO

        //TODO
        osg::Vec4 lowerWallColor(0.5f, 0.6f, 0.5f, 1.0f);
        osg::Vec4 upperWallColor(1.0f, 1.0f, 1.0f, 1.0f);

        osg::ref_ptr<osg::Geometry> geom = new osg::Geometry();
        geom->setUseVertexBufferObjects( true );
        geom->setUseDisplayList( false );
        
        const Elevation::Walls& walls = elevation->getWalls();

        // Count the total number of verts.
        unsigned totalNumVerts = 0;
        for(Elevation::Walls::const_iterator wall = walls.begin(); wall != walls.end(); ++wall)
        {
            totalNumVerts += (6 * wall->getNumPoints());
        }
        OE_DEBUG << LC << "Extrusion: total verts in elevation = " << totalNumVerts << "\n";

        // preallocate all attribute arrays.
        osg::Vec3Array* verts = new osg::Vec3Array( totalNumVerts );
        geom->setVertexArray( verts );

        osg::Vec4Array* colors = 0L;
        if ( genColors )
        {
            colors = new osg::Vec4Array( totalNumVerts );
            geom->setColorArray( colors );
            geom->setColorBinding( geom->BIND_PER_VERTEX );
        }

        osg::Vec2Array* texCoords = 0L;
        if ( useTexture )
        {
            texCoords = new osg::Vec2Array( totalNumVerts );
            geom->setTexCoordArray( 0, texCoords );
        }

        osg::Vec3Array* normals = 0L;
        if ( genNormals )
        {
            normals = new osg::Vec3Array( totalNumVerts );
            geom->setNormalArray( normals );
            geom->setNormalBinding( geom->BIND_PER_VERTEX );
        }

        unsigned vertPtr = 0;

        OE_DEBUG << LC << "...elevation has " << walls.size() << " walls\n";

        // Each elevation is a collection of walls. One outer wall and
        // zero or more inner walls (where there were holes in the original footprint).
        for(Elevation::Walls::const_iterator wall = walls.begin(); wall != walls.end(); ++wall)
        {
            //bool madeGeom = true;

            // 6 verts per face total (3 triangles)
            unsigned numWallVerts = 6 * wall->getNumPoints();
    
            //// Scale and bias:
            //osg::Vec2f scale, bias;
            //float layer;
            //if ( useTexture )
            //{
            //    bias.set (wallSkin->imageBiasS().get(),  wallSkin->imageBiasT().get());
            //    scale.set(wallSkin->imageScaleS().get(), wallSkin->imageScaleT().get());
            //    layer = (float)wallSkin->imageLayer().get();
            //}

            // create all the OSG geometry components
            //osg::Vec3Array* verts = new osg::Vec3Array( numWallVerts );
            //geom->setVertexArray( verts );
    
            //osg::Vec3Array* tex = 0L;
            //if ( wallSkin )
            //{ 
            //    tex = new osg::Vec3Array( numWallVerts );
            //    walls->setTexCoordArray( 0, tex );
            //}

            //osg::Vec4Array* anchors = 0L;
    
            //// If GPU clamping is in effect, create clamping attributes.
            //if ( _gpuClamping )
            //{
            //    anchors = new osg::Vec4Array( numWallVerts );
            //    walls->setVertexAttribArray    ( Clamping::AnchorAttrLocation, anchors );
            //    walls->setVertexAttribBinding  ( Clamping::AnchorAttrLocation, osg::Geometry::BIND_PER_VERTEX );
            //    walls->setVertexAttribNormalize( Clamping::AnchorAttrLocation, false );
            //}

            osg::DrawElements* de = 
                totalNumVerts > 0xFFFF ? (osg::DrawElements*) new osg::DrawElementsUInt  ( GL_TRIANGLES ) :
                totalNumVerts > 0xFF   ? (osg::DrawElements*) new osg::DrawElementsUShort( GL_TRIANGLES ) :
                                         (osg::DrawElements*) new osg::DrawElementsUByte ( GL_TRIANGLES );

            // pre-allocate for speed
            de->reserveElements( numWallVerts );
            geom->addPrimitiveSet( de );

            OE_DEBUG << LC << "...wall has " << wall->faces.size() << " faces\n";
            for(Elevation::Faces::const_iterator f = wall->faces.begin(); f != wall->faces.end(); ++f, vertPtr += 6)
            {
                osg::Vec3f LL = f->left.lower  * frame;
                osg::Vec3f LR = f->right.lower * frame;
                osg::Vec3f UR = f->right.upper * frame;
                osg::Vec3f UL = f->left.upper  * frame;

                // set the 6 wall verts.
                (*verts)[vertPtr+0] = UL;
                (*verts)[vertPtr+1] = LL;
                (*verts)[vertPtr+2] = LR;
                (*verts)[vertPtr+3] = LR;
                (*verts)[vertPtr+4] = UR;
                (*verts)[vertPtr+5] = UL;
            
#if 0
                if ( anchors )
                {
                    float x = structure.baseCentroid.x(), y = structure.baseCentroid.y(), vo = structure.verticalOffset;

                    (*anchors)[vertPtr+1].set( x, y, vo, Clamping::ClampToGround );
                    (*anchors)[vertPtr+2].set( x, y, vo, Clamping::ClampToGround );
                    (*anchors)[vertPtr+3].set( x, y, vo, Clamping::ClampToGround );

                    (*anchors)[vertPtr+0].set( x, y, vo, Clamping::ClampToAnchor );
                    (*anchors)[vertPtr+4].set( x, y, vo, Clamping::ClampToAnchor );
                    (*anchors)[vertPtr+5].set( x, y, vo, Clamping::ClampToAnchor );
                }
#endif

                // Assign wall polygon colors.
                if ( genColors )
                {
                    (*colors)[vertPtr+0] = upperWallColor;
                    (*colors)[vertPtr+1] = lowerWallColor;
                    (*colors)[vertPtr+2] = lowerWallColor;
                    (*colors)[vertPtr+3] = lowerWallColor;
                    (*colors)[vertPtr+4] = upperWallColor;
                    (*colors)[vertPtr+5] = upperWallColor;
                }

#if 0
                // Calculate texture coordinates:
                if ( useTexture )
                {
                    // Calculate left and right corner V coordinates:
                    double hL = (f->left.upper - f->left.lower).length();
                    double hR = (f->right.upper - f->right.lower).length();
                
                    // Calculate the texture coordinates at each corner. The structure builder
                    // will have spaced the verts correctly for this to work.
                    float uL = fmod( f->left.offsetX, texWidthM ) / texWidthM;
                    float uR = fmod( f->right.offsetX, texWidthM ) / texWidthM;

                    // Correct for the case in which the rightmost corner is exactly on a
                    // texture boundary.
                    if ( uR < uL || (uL == 0.0 && uR == 0.0))
                        uR = 1.0f;

                    osg::Vec2f texBaseL( uL, 0.0f );
                    osg::Vec2f texBaseR( uR, 0.0f );
                    osg::Vec2f texRoofL( uL, hL/elev->texHeightAdjustedM );
                    osg::Vec2f texRoofR( uR, hR/elev->texHeightAdjustedM );

                    texRoofL = bias + osg::componentMultiply(texRoofL, scale);
                    texRoofR = bias + osg::componentMultiply(texRoofR, scale);
                    texBaseL = bias + osg::componentMultiply(texBaseL, scale);
                    texBaseR = bias + osg::componentMultiply(texBaseR, scale);

                    (*tex)[vertptr+0].set( texRoofL.x(), texRoofL.y(), layer );
                    (*tex)[vertptr+1].set( texBaseL.x(), texBaseL.y(), layer );
                    (*tex)[vertptr+2].set( texBaseR.x(), texBaseR.y(), layer );
                    (*tex)[vertptr+3].set( texBaseR.x(), texBaseR.y(), layer );
                    (*tex)[vertptr+4].set( texRoofR.x(), texRoofR.y(), layer );
                    (*tex)[vertptr+5].set( texRoofL.x(), texRoofL.y(), layer );
                }
#endif

                for(int i=0; i<6; ++i)
                {
                    de->addElement( vertPtr+i );
                }
            }
        }

        // TODO - temporary, doesn't smooth disconnected edges
        osgUtil::SmoothingVisitor::smooth( *geom.get(), 15.0f );

        OE_DEBUG << LC "...adding geometry\n";
        geode->addDrawable( geom.get() );
    }

    return true;
}

bool
BuildingCompiler::addRoof(osg::Geode* geode, Building* building, const osg::Matrix& world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;

    // check for a roof
    if ( !building->getRoof() )
        return false;

    // check for at least one elevation
    if ( building->getElevations().empty() )
        return false;

    // precalculate the frame transformation:
    osg::Matrix frame = building->getReferenceFrame() * world2local;

    // base the roof on the last elevation
    Elevation* elevation = building->getElevations().back().get();
    if ( !elevation )
        return false;

    bool genColors = true;   // TODO
    bool useTexture = false; // TODO
    osg::Vec4 roofColor(1.0f, 0.9f, 0.8f, 1.0f); //TODO

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
    if ( useTexture )
    {
        texCoords = new osg::Vec3Array();
        geom->setTexCoordArray(0, texCoords);
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

                if ( colors )
                {
                    colors->push_back( roofColor );
                }

                if ( texCoords )
                {
                    texCoords->push_back( osg::Vec3f(f->left.roofTexU, f->left.roofTexV, (float)0.0f) );
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

    for(osg::Vec3Array::iterator v = verts->begin(); v != verts->end(); ++v)
        (*v) = (*v) * frame;

    geode->addDrawable( geom.get() );

    return true;
}
