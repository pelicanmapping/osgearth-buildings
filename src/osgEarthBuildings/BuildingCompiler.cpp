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

BuildingCompiler::BuildingCompiler(Session* session) :
_session( session )
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
        addElevations( geode, building, building->getElevations(), world2local );
    }

    return root.release();
}

bool
BuildingCompiler::addBuilding(osg::Geode*        geode,
                              const Building*    building,
                              const osg::Matrix& world2local) const
{
    return addSimpleFootprint(geode, building, world2local);
}

bool
BuildingCompiler::addSimpleFootprint(osg::Geode*        geode,
                                     const Building*    building,
                                     const osg::Matrix& world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;

    osg::Geometry* geom = new osg::Geometry();
    geom->setUseVertexBufferObjects( true );
    geom->setUseDisplayList( false );

    osg::Vec3Array* v = new osg::Vec3Array();
    geom->setVertexArray( v );

    GeometryIterator iter( building->getFootprint() );
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
BuildingCompiler::addElevations(osg::Geode*            geode,
                                const Building*        building,
                                const ElevationVector& elevations,
                                const osg::Matrix&     world2local) const
{
    if ( !geode ) return false;
    if ( !building ) return false;

    // precalculate the frame transformation:
    osg::Matrix frame = building->getReferenceFrame() * world2local;

    // Iterator over each Elevation in this building:
    for(ElevationVector::const_iterator e = elevations.begin();
        e != elevations.end();
        ++e)
    {
        const Elevation* elevation = e->get();
        
        // make sure there's something to build:
        const Elevation::Walls& walls = elevation->getWalls();
        if ( walls.size() == 0 )
        {
            OE_DEBUG << LC << "Elevation has no walls; skipping.\n";
            continue;
        }
        
        SkinResource* skin = elevation->getSkinResource();
        bool useTexture = skin != 0L;
        float texWidth = 0.0f;
        float texHeight = 0.0f;
        osg::Vec2f texScale(1.0f, 1.0f);
        osg::Vec2f texBias (0.0f, 0.0f);
        float texLayer = 0.0f;

        osg::ref_ptr<osg::StateSet> stateSet;
        if ( skin )
        {
            if ( _session->getResourceCache() )
            {
                _session->getResourceCache()->getOrCreateStateSet(skin, stateSet);
            }

            texWidth = skin->imageWidth().get();
            texHeight = skin->imageHeight().get();
            texScale.set(skin->imageScaleS().get(), skin->imageScaleT().get());
            texBias.set(skin->imageBiasS().get(), skin->imageBiasT().get());
            texLayer = skin->imageLayer().get();
        }

        bool genColors  = false;
        bool genNormals = false;

        //TODO
        Color upperWallColor = elevation->getColor();
        Color lowerWallColor = elevation->getColor();
        if ( !skin )
            lowerWallColor = upperWallColor.brightness(0.95f);

        osg::ref_ptr<osg::Geometry> geom = new osg::Geometry();
        geom->setUseVertexBufferObjects( true );
        geom->setUseDisplayList( false );

        if ( stateSet.valid() )
        {
            geom->setStateSet( stateSet.get() );
        }

        // Count the total number of verts.
        unsigned totalNumVerts = 0;
        for(Elevation::Walls::const_iterator wall = walls.begin(); wall != walls.end(); ++wall)
        {
            totalNumVerts += (6 * wall->getNumPoints());
        }

        totalNumVerts *= elevation->getNumFloors();
        OE_DEBUG << LC << "Extrusion: total verts in elevation = " << totalNumVerts << "\n";

        // preallocate all attribute arrays.
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
        if ( skin )
        {
            texCoords = new osg::Vec3Array();
            geom->setTexCoordArray( 0, texCoords );
        }

        osg::Vec3Array* normals = 0L;
        if ( genNormals )
        {
            normals = new osg::Vec3Array();
            geom->setNormalArray( normals );
            geom->setNormalBinding( geom->BIND_PER_VERTEX );
        }

        unsigned vertPtr = 0;
        //TODO
        float  floorHeight = elevation->getHeight() / (float)elevation->getNumFloors();

        OE_DEBUG << LC << "...elevation has " << walls.size() << " walls\n";

        // Each elevation is a collection of walls. One outer wall and
        // zero or more inner walls (where there were holes in the original footprint).
        for(Elevation::Walls::const_iterator wall = walls.begin(); wall != walls.end(); ++wall)
        {
            // 6 verts per face total (3 triangles) per floor
            //unsigned numWallVerts = 6 * wall->getNumPoints() * elevation->getNumFloors();

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
            //de->reserveElements( numWallVerts );
            geom->addPrimitiveSet( de );

            OE_DEBUG << LC << "..elevation has " << elevation->getNumFloors() << " floors\n";

            float numFloorsF = (float)elevation->getNumFloors();
            
            for(unsigned flr=0; flr < elevation->getNumFloors(); ++flr)
            {
                float lowerZ = (float)flr * floorHeight;
    
                OE_DEBUG << LC << "...wall has " << wall->faces.size() << " faces\n";
                for(Elevation::Faces::const_iterator f = wall->faces.begin(); f != wall->faces.end(); ++f, vertPtr += 4) //+= 6)
                {
                    osg::Vec3d Lvec = f->left.upper - f->left.lower; Lvec.normalize();
                    osg::Vec3d Rvec = f->right.upper - f->right.lower; Rvec.normalize();

                    float upperZ = lowerZ + floorHeight;

                    osg::Vec3d LL = (f->left.lower  + Lvec*lowerZ) * frame;
                    osg::Vec3d UL = (f->left.lower  + Lvec*upperZ) * frame;
                    osg::Vec3d LR = (f->right.lower + Rvec*lowerZ) * frame;
                    osg::Vec3d UR = (f->right.lower + Rvec*upperZ) * frame;

                    // set the 6 wall verts. // TODO: optimize down to four.
                    verts->push_back( UL );
                    verts->push_back( LL );
                    verts->push_back( LR );
                    verts->push_back( UR );
           
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
                    if ( colors )
                    {
                        colors->push_back( upperWallColor );
                        colors->push_back( lowerWallColor );
                        colors->push_back( lowerWallColor );
                        colors->push_back( upperWallColor );
                    }

                    if ( texCoords )
                    {
                        // Calculate the texture coordinates at each corner. The structure builder
                        // will have spaced the verts correctly for this to work.
                        float uL = fmod( f->left.offsetX,  texWidth ) / texWidth;
                        float uR = fmod( f->right.offsetX, texWidth ) / texWidth;

                        // Correct for the case in which the rightmost corner is exactly on a
                        // texture boundary.
                        if ( uR < uL || (uL == 0.0 && uR == 0.0))
                            uR = 1.0f;

                        osg::Vec2f texLL( uL, 0.0f );
                        osg::Vec2f texLR( uR, 0.0f );
                        osg::Vec2f texUL( uL, 1.0f );
                        osg::Vec2f texUR( uR, 1.0f );

                        texUL = texBias + osg::componentMultiply(texUL, texScale);
                        texUR = texBias + osg::componentMultiply(texUR, texScale);
                        texLL = texBias + osg::componentMultiply(texLL, texScale);
                        texLR = texBias + osg::componentMultiply(texLR, texScale);
                        
                        texCoords->push_back( osg::Vec3f(texUL.x(), texUL.y(), texLayer) );
                        texCoords->push_back( osg::Vec3f(texLL.x(), texLL.y(), texLayer) );
                        texCoords->push_back( osg::Vec3f(texLR.x(), texLR.y(), texLayer) );
                        texCoords->push_back( osg::Vec3f(texUR.x(), texUR.y(), texLayer) );
                    }

                    // build the triangles.
                    de->addElement( vertPtr+0 );
                    de->addElement( vertPtr+1 );
                    de->addElement( vertPtr+2 );
                    de->addElement( vertPtr+0 );
                    de->addElement( vertPtr+2 );
                    de->addElement( vertPtr+3 );

                } // faces loop

            } // floors loop

        } // walls loop

        // TODO - temporary, doesn't smooth disconnected edges
        osgUtil::SmoothingVisitor::smooth( *geom.get(), 15.0f );

        geode->addDrawable( geom.get() );

        if ( elevation->getRoof() )
        {
            addRoof( geode, building, elevation, world2local );
        }

        if ( !elevation->getElevations().empty() )
        {
            addElevations( geode, building, elevation->getElevations(), world2local );
        }

    } // elevations loop

    return true;
}

bool
BuildingCompiler::addRoof(osg::Geode* geode, const Building* building, const Elevation* elevation, const osg::Matrix& world2local) const
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

    return true;
}
