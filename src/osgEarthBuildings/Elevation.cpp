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
#include "Elevation"
#include <osgEarthSymbology/Geometry>

using namespace osgEarth;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

Elevation::Elevation() :
_height   ( 15.0f ),
_numFloors( 4.0f )
{
    //nop
}

float
Elevation::getRotation(const Footprint* footprint) const
{
    // looks for the longest segment in the footprint and
    // returns the angle of that segment relative to north.
    Segment n;
    double  maxLen2 = 0.0;
    ConstSegmentIterator i( footprint, true );
    while( i.hasMore() )
    {
        Segment s = i.next();
        double len2 = (s.second - s.first).length2();
        if ( len2 > maxLen2 ) 
        {
            maxLen2 = len2;
            n = s;
        }
    }

    const osg::Vec3d& p1 = n.first.x() < n.second.x() ? n.first : n.second;
    const osg::Vec3d& p2 = n.first.x() < n.second.x() ? n.second : n.first;

    return atan2( p2.x()-p1.x(), p2.y()-p1.y() );
}

bool
Elevation::build(const Footprint* footprint)
{
    _walls.clear();

    // prep for wall texture coordinate generation.
    float texWidthM  = _skinResource.valid() ? _skinResource->imageWidth().get()  : 0.0f;
    float texHeightM = _skinResource.valid() ? _skinResource->imageHeight().get() : 1.0f;
    
    bool hasTexture = true; // TODO

    // calcluate the bounds and the dominant rotation of the shape
    // based on the longest side.
    Bounds bounds = footprint->getBounds();

    // roof data:
    osg::Vec2f roofTexSpan;
    float sinR, cosR;
    SkinResource* roofSkin = _roof.valid() ? _roof->getSkinResource() : 0L;
    if ( roofSkin )
    {
        float roofRotation = getRotation( footprint );
            
        sinR = sinf(roofRotation);
        cosR = cosf(roofRotation);

        roofTexSpan.x() = roofSkin->imageWidth().isSet() ? *roofSkin->imageWidth() : roofSkin->imageHeight().isSet() ? *roofSkin->imageHeight() : 10.0;
        if ( roofTexSpan.x() <= 0.0 )
            roofTexSpan.x() = 10.0;

        roofTexSpan.y() = roofSkin->imageHeight().isSet() ? *roofSkin->imageHeight() : roofSkin->imageWidth().isSet() ? *roofSkin->imageWidth() : 10.0;
        if ( roofTexSpan.y() <= 0.0 )
            roofTexSpan.y() = 10.0;
    }

    ConstGeometryIterator iter( footprint );
    while( iter.hasMore() )
    {
        const Geometry* part = iter.next();

        // skip a part that's too small (invalid)
        if (part->size() < 2)
            continue;

        // add a new wall.
        _walls.push_back( Wall() );
        Wall& wall = _walls.back();

        // Step 1 - Create the real corners and transform them into our target SRS.
        Corners corners;
        for(Geometry::const_iterator m = part->begin(); m != part->end(); ++m)
        {
            Corners::iterator corner = corners.insert(corners.end(), Corner());
            
            // mark as "from source", as opposed to being inserted by the algorithm.
            corner->isFromSource = true;
            corner->lower = *m;

            // extrude:
            corner->upper.set( corner->lower.x(), corner->lower.y(), _height );

            // resolve UV coordinates based on dominant rotation:
            if ( roofSkin )
            {
                float xr = corner->upper.x() - bounds.xMin();
                float yr = corner->upper.y() - bounds.yMin();            
                corner->roofUV.set((cosR*xr - sinR*yr) / roofTexSpan.x(), (sinR*xr + cosR*yr) / roofTexSpan.y());
            }

            // cache the length for later use.
            corner->height = (corner->upper - corner->lower).length();
        }

        // Step 2 - Insert intermediate Corners as needed to satisfy texturing
        // requirements (if necessary) and record each corner offset (horizontal distance
        // from the beginning of the part geometry to the corner.)
        float cornerOffset    = 0.0;
        float nextTexBoundary = texWidthM;

        for(Corners::iterator c = corners.begin(); c != corners.end(); ++c)
        {
            Corners::iterator this_corner = c;

            Corners::iterator next_corner = c;
			bool isLastEdge = false;
			if ( ++next_corner == corners.end() )
			{
				isLastEdge = true;
				next_corner = corners.begin();
			}

            osg::Vec3f base_vec = next_corner->lower - this_corner->lower;
            float span = base_vec.length();

            this_corner->offsetX = cornerOffset;

            if ( hasTexture )
            {
                base_vec /= span; // normalize
                osg::Vec3f roof_vec = next_corner->upper - this_corner->upper;
                roof_vec.normalize();

                while(texWidthM > 0.0 && nextTexBoundary < cornerOffset+span)
                {
                    // insert a new fake corner.
					Corners::iterator new_corner;

                    if ( isLastEdge )
                    {
						corners.push_back(Corner());
						new_corner = c;
						new_corner++;
                    }
                    else
                    {
						new_corner = corners.insert(next_corner, Corner());
					}

                    new_corner->isFromSource = false;
                    float advance = nextTexBoundary-cornerOffset;
                    new_corner->lower = this_corner->lower + base_vec*advance;
                    new_corner->upper = this_corner->upper + roof_vec*advance;
                    new_corner->height = (new_corner->upper - new_corner->lower).length();
                    new_corner->offsetX = cornerOffset + advance;
                    nextTexBoundary += texWidthM;

                    // advance the main iterator
                    c = new_corner;
                }
            }

            cornerOffset += span;
        }

        // Step 3 - Calculate the angle of each corner.
        osg::Vec3f prev_vec;
        for(Corners::iterator c = corners.begin(); c != corners.end(); ++c)
        {
            Corners::const_iterator this_corner = c;

            Corners::const_iterator next_corner = c;
            if ( ++next_corner == corners.end() )
                next_corner = corners.begin();

            if ( this_corner == corners.begin() )
            {
                Corners::const_iterator prev_corner = corners.end();
                --prev_corner;
                prev_vec = this_corner->upper - prev_corner->upper;
                prev_vec.normalize();
            }

            osg::Vec3f this_vec = next_corner->upper - this_corner->upper;
            this_vec.normalize();
            if ( c != corners.begin() )
            {
                c->cosAngle = prev_vec * this_vec;
            }
        }

        // Step 4 - Create faces connecting each pair of corner posts.
        Faces& faces = wall.faces;
        for(Corners::const_iterator c = corners.begin(); c != corners.end(); ++c)
        {
            Corners::const_iterator this_corner = c;

            Corners::const_iterator next_corner = c;
            if ( ++next_corner == corners.end() )
                next_corner = corners.begin();

            faces.push_back(Face());
            Face& face = faces.back();
            face.left  = *this_corner;
            face.right = *next_corner;

            // recalculate the final offset on the last face
            if ( next_corner == corners.begin() )
            {
                osg::Vec3f vec = next_corner->upper - this_corner->upper;
                face.right.offsetX = face.left.offsetX + vec.length();
            }

            face.widthM = next_corner->offsetX - this_corner->offsetX;
        }
    }

    return true;
}
