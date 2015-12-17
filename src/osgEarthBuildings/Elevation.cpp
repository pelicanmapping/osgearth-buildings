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

bool
Elevation::build(const Footprint* footprint)
{
    _walls.clear();

    // prep for wall texture coordinate generation.
    double texWidthM  = _skin.valid() ? _skin->imageWidth().get() : 0.0;
    double texHeightM = _skin.valid() ? _skin->imageHeight().get() : 1.0;

    bool hasTexture = true; // TODO

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

        //double maxHeight = targetLen - minLoc.z();

        // Adjust the texture height so it is a multiple of the maximum height
        //double div = osg::round(maxHeight / texHeightM);
        //wall.texHeightAdjustedM = div > 0.0 ? maxHeight / div : maxHeight;

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
            
            //// figure out the rooftop texture coords before doing any transformation:
            //if ( roofSkin && srs )
            //{
            //    double xr, yr;

            //    if ( srs && srs->isGeographic() && roofProjSRS )
            //    {
            //        osg::Vec3d projRoofPt;
            //        srs->transform( corner->roof, roofProjSRS.get(), projRoofPt );
            //        xr = (projRoofPt.x() - roofBounds.xMin());
            //        yr = (projRoofPt.y() - roofBounds.yMin());
            //    }
            //    else
            //    {
            //        xr = (corner->roof.x() - roofBounds.xMin());
            //        yr = (corner->roof.y() - roofBounds.yMin());
            //    }

            //    corner->roofTexU = (cosR*xr - sinR*yr) / roofTexSpanX;
            //    corner->roofTexV = (sinR*xr + cosR*yr) / roofTexSpanY;
            //}

            //// transform into target SRS.
            //transformAndLocalize( corner->base, srs, corner->base, mapSRS, _world2local, makeECEF );
            //transformAndLocalize( corner->roof, srs, corner->roof, mapSRS, _world2local, makeECEF );

            // cache the length for later use.
            corner->height = (corner->upper - corner->lower).length();
        }

        // Step 2 - Insert intermediate Corners as needed to satisfy texturing
        // requirements (if necessary) and record each corner offset (horizontal distance
        // from the beginning of the part geometry to the corner.)
        double cornerOffset    = 0.0;
        double nextTexBoundary = texWidthM;

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

            osg::Vec3d base_vec = next_corner->lower - this_corner->lower;
            double span = base_vec.length();

            this_corner->offsetX = cornerOffset;

            if ( hasTexture )
            {
                base_vec /= span; // normalize
                osg::Vec3d roof_vec = next_corner->upper - this_corner->upper;
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
                    double advance = nextTexBoundary-cornerOffset;
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
        osg::Vec3d prev_vec;
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

            osg::Vec3d this_vec = next_corner->upper - this_corner->upper;
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
                osg::Vec3d vec = next_corner->upper - this_corner->upper;
                face.right.offsetX = face.left.offsetX + vec.length();
            }

            face.widthM = next_corner->offsetX - this_corner->offsetX;
        }
    }

    return true;
}
