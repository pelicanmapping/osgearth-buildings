
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

namespace
{
    void makeBox(const osg::Vec3f& LL, const osg::Vec3f& UR, osg::Vec3Array* v, osg::Vec3Array* t)
    {
        osg::Vec3f
            BLL(LL), BLR(UR.x(),LL.y(),LL.z()), BUL(LL.x(),UR.y(),LL.z()), BUR(UR.x(),UR.y(),LL.z()),
            TLL(BLL.x(),BLL.y(),UR.z()), TLR(BLR.x(),BLR.y(),UR.z()), TUL(BUL.x(),BUL.y(),UR.z()), TUR(BUR.x(),BUR.y(),UR.z());

        // cap:
        v->push_back(TLL); v->push_back(TLR); v->push_back(TUL);
        v->push_back(TUL); v->push_back(TLR); v->push_back(TUR);

        //sides:
        v->push_back(BLL); v->push_back(BLR); v->push_back(TLL);
        v->push_back(TLL); v->push_back(BLR); v->push_back(TLR);
        
        v->push_back(BLR); v->push_back(BUR); v->push_back(TLR);
        v->push_back(TLR); v->push_back(BUR); v->push_back(TUR);

        v->push_back(BUR); v->push_back(BUL); v->push_back(TUR);
        v->push_back(TUR); v->push_back(BUL); v->push_back(TUL);

        v->push_back(BUL); v->push_back(BLL); v->push_back(TUL);
        v->push_back(TUL); v->push_back(BLL); v->push_back(TLL);

        //TODO.. just plaster it on for now
        for(int i=0; i<30; ++i)
            t->push_back(osg::Vec3f(0,0,0));
    }
}

GableRoofCompiler::GableRoofCompiler(Session* session) :
_session( session )
{
    // build the unit-space template.
    osg::Vec3f LL(0, 0, 0), LM(0.5, 0, 2), LR(1, 0, 0),
               UL(0, 1, 0), UM(0.5, 1, 2), UR(1, 1, 0);

    osg::Vec3f texLL(0, 0, 0), texLM(0.5, 0, 0), texLR(1, 0, 0),
               texUL(0, 1, 0), texUM(0.5, 1, 0), texUR(1, 1, 0);

    // set the Y values here if you want the roofs to overhang the gables.
    // this is cool, but creates a backfacing polygon which might now work
    // wekk with shadows.
    osg::Vec3f UO(0,  0, 0),
               LO(0, -0, 0);

    _verts     = new osg::Vec3Array();
    _texCoords = new osg::Vec3Array();

    // first side:
    _verts->push_back(LL+LO); _texCoords->push_back(texLL);
    _verts->push_back(LM+LO); _texCoords->push_back(texLM);
    _verts->push_back(UL+UO); _texCoords->push_back(texUL);
    _verts->push_back(UL+UO); _texCoords->push_back(texUL);
    _verts->push_back(LM+LO); _texCoords->push_back(texLM);
    _verts->push_back(UM+UO); _texCoords->push_back(texUM);
    
    // second side:
    _verts->push_back(LR+LO); _texCoords->push_back(texLR);
    _verts->push_back(UR+UO); _texCoords->push_back(texUR);
    _verts->push_back(LM+LO); _texCoords->push_back(texLM);
    _verts->push_back(LM+LO); _texCoords->push_back(texLM);
    _verts->push_back(UR+UO); _texCoords->push_back(texUR);
    _verts->push_back(UM+UO); _texCoords->push_back(texUM);

    // south gable:
    _verts->push_back(UL); _texCoords->push_back(texUL);
    _verts->push_back(UM); _texCoords->push_back(texUM);
    _verts->push_back(UR); _texCoords->push_back(texUR);

    // north gable:
    _verts->push_back(LL); _texCoords->push_back(texLL);
    _verts->push_back(LR); _texCoords->push_back(texLR);
    _verts->push_back(LM); _texCoords->push_back(texLM);

    // add a chimney :)
    makeBox(osg::Vec3f(0.2, 0.2, 0.0), osg::Vec3f(0.3, 0.3, 2.5), _verts.get(), _texCoords.get());
}

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

    // copy the unit-space template:
    osg::Vec3Array* verts = new osg::Vec3Array(_verts->begin(), _verts->end());
    geom->setVertexArray( verts );
    
    osg::Vec3Array* texCoords = 0L;
    if ( stateSet.valid() )
    {
        // copy the texture coordinates (though this may not be strictly necessary)
        texCoords = new osg::Vec3Array(_texCoords->begin(), _texCoords->end());
        geom->setTexCoordArray( 0, texCoords );
        geom->setStateSet( stateSet.get() );
    }

    osg::Vec3Array* normals = new osg::Vec3Array();
    normals->reserve( verts->size() );
    geom->setNormalArray( normals );
    geom->setNormalBinding( geom->BIND_PER_VERTEX );

    // the AABB gives us the information to scale+bias the unit template 
    // to the proper size and shape:
    const osg::BoundingBox& aabb = elevation->getAxisAlignedBoundingBox();
    osg::Vec3f scale(aabb.xMax()-aabb.xMin(), aabb.yMax()-aabb.yMin(), 1.0f);
    osg::Vec3f bias (aabb.xMin(), aabb.yMin(), aabb.zMin());

    // scale and bias the geometry, rotate it back to its actual location,
    // and transform into the final coordinate frame.
    for(int i=0; i<verts->size(); ++i)
    {
        osg::Vec3f& v = (*verts)[i];
        v = osg::componentMultiply(v, scale) + bias;
        elevation->unrotate( v );
        v = v * frame;
    }

    // calculate normals (after transforming the vertices)
    for(int i=0; i<verts->size(); i+=3)
    {
        osg::Vec3f n = ((*verts)[i+2]-(*verts)[i+1]) ^ ((*verts)[i]-(*verts)[i+1]);
        n.normalize();
        normals->push_back(n);
        normals->push_back(n);
        normals->push_back(n);
    }

    // and finally the triangles.
    geom->addPrimitiveSet( new osg::DrawArrays(GL_TRIANGLES, 0, verts->size()) );

    geode->addDrawable( geom.get() );
    return true;
}
