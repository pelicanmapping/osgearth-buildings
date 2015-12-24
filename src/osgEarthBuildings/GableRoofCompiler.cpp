
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
#include "GableRoofCompiler"
#include <osgEarthFeatures/Session>

using namespace osgEarth;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

#define LC "[GableRoofCompiler] "


bool
GableRoofCompiler::compile(const Building*    building,
                           const Elevation*   elevation, 
                           osg::Geode*        geode,
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

    osg::ref_ptr<osg::Geometry> geom = new osg::Geometry();
    geom->setUseVertexBufferObjects( true );
    geom->setUseDisplayList( false );

    osg::Vec3Array* verts = new osg::Vec3Array();
    geom->setVertexArray( verts );
    
    osg::Vec3Array* texCoords = 0L;
    if ( stateSet.valid() )
    {
        texCoords = new osg::Vec3Array();
        geom->setTexCoordArray( 0, texCoords );
        geom->setStateSet( stateSet.get() );
    }

    osg::Vec3Array* normals = new osg::Vec3Array();
    geom->setNormalArray( normals );
    geom->setNormalBinding( geom->BIND_PER_VERTEX );

    const osg::BoundingBox& aabb = elevation->getAxisAlignedBoundingBox();

    // rotate the AABB into its true coodinates so get the 4 corners:
    osg::Vec3f LL(aabb.xMin(), aabb.yMin(), aabb.zMin()); elevation->unrotate(LL);
    osg::Vec3f LR(aabb.xMax(), aabb.yMin(), aabb.zMin()); elevation->unrotate(LR);
    osg::Vec3f UL(aabb.xMin(), aabb.yMax(), aabb.zMin()); elevation->unrotate(UL);
    osg::Vec3f UR(aabb.xMax(), aabb.yMax(), aabb.zMin()); elevation->unrotate(UR);

    // calculate midpoints along the longer axis:
    osg::Vec3f LM = (LL+LR)*0.5; LM.z() += 2.0f;
    osg::Vec3f UM = (UL+UR)*0.5; UM.z() += 2.0f;

    // transform into the destination frame (before calculating normals)
    LL = LL * frame;
    LR = LR * frame;
    LM = LM * frame;
    UL = UL * frame;
    UR = UR * frame;
    UM = UM * frame;

    // make up some simple texture coords:
    osg::Vec3f texLL(0.0f, 0.0f, 0.0f);
    osg::Vec3f texLM(0.5f, 0.0f, 0.0f);
    osg::Vec3f texLR(1.0f, 0.0f, 0.0f);
    osg::Vec3f texUL(0.0f, 1.0f, 0.0f);
    osg::Vec3f texUM(0.5f, 1.0f, 0.0f);
    osg::Vec3f texUR(1.0f, 1.0f, 0.0f);

    // build the geometry with the proper normals:
    osg::Vec3f n;

    n = (LM-LL)^(UL-LL); n.normalize();
    verts->push_back(LL); normals->push_back(n); texCoords->push_back(texLL);
    verts->push_back(LM); normals->push_back(n); texCoords->push_back(texLM);
    verts->push_back(UL); normals->push_back(n); texCoords->push_back(texUL);
    verts->push_back(UL); normals->push_back(n); texCoords->push_back(texUL);
    verts->push_back(LM); normals->push_back(n); texCoords->push_back(texLM);
    verts->push_back(UM); normals->push_back(n); texCoords->push_back(texUM);
    
    n = (UR-LR)^(LM-LR); n.normalize();
    verts->push_back(LR); normals->push_back(n); texCoords->push_back(texLR);
    verts->push_back(UR); normals->push_back(n); texCoords->push_back(texUR);
    verts->push_back(LM); normals->push_back(n); texCoords->push_back(texLM);
    verts->push_back(LM); normals->push_back(n); texCoords->push_back(texLM);
    verts->push_back(UR); normals->push_back(n); texCoords->push_back(texUR);
    verts->push_back(UM); normals->push_back(n); texCoords->push_back(texUM);

    n = (UM-UL)^(UR-UL); n.normalize();
    verts->push_back(UL); normals->push_back(n); texCoords->push_back(texUL);
    verts->push_back(UM); normals->push_back(n); texCoords->push_back(texUM);
    verts->push_back(UR); normals->push_back(n); texCoords->push_back(texUR);

    n = (LM-LR)^(LL-LR); n.normalize();
    verts->push_back(LL); normals->push_back(n); texCoords->push_back(texLL);
    verts->push_back(LR); normals->push_back(n); texCoords->push_back(texLR);
    verts->push_back(LM); normals->push_back(n); texCoords->push_back(texLM);

    geom->addPrimitiveSet( new osg::DrawArrays(GL_TRIANGLES, 0, verts->size()) );

    geode->addDrawable( geom.get() );
    return true;
}
