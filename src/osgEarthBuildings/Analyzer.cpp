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
#include "Analyzer"

#include <osgEarth/Progress>
#include <osgEarth/TileKey>
#include <osgEarth/ImageUtils>
#include <osg/BlendFunc>
#include <osg/BlendColor>
#include <osgUtil/Statistics>
#include <iostream>

#define LC "[Analyzer] "

using namespace osgEarth;
using namespace osgEarth::Buildings;

namespace
{
    struct FindTextures : public osgEarth::TextureAndImageVisitor
    {
        std::set<osg::Texture*> _textures;

        void apply(osg::Texture& texture) {
            if (_textures.find(&texture) == _textures.end()) {
                _textures.insert(&texture);
            }
        }

        void print(std::ostream& out) {
            out << "Textures (" << _textures.size() << ") : \n";
            for (std::set<osg::Texture*>::const_iterator i = _textures.begin(); i != _textures.end(); ++i) {
                out << "    " << std::hex << (uintptr_t)(*i) << " : " << std::dec << (*i)->getImage(0)->getFileName() << "\n";
            }
        }
    };

    struct ColorVisitor : public osg::NodeVisitor
    {
        std::set<osg::Drawable*> _visitedDrawables;

        int r;
        int g;
        int b;
        int brightness;

        ColorVisitor() : osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN)
        {
            r = g = b = brightness = 128;
        }

        virtual void apply(osg::Geode& geode)
        {
            for (unsigned int i = 0; i < geode.getNumDrawables(); i++)
            {
                if (_visitedDrawables.find(geode.getDrawable(i)) != _visitedDrawables.end())
                {
                    continue;
                }

                _visitedDrawables.insert(geode.getDrawable(i));
                applyColor(geode.getDrawable(i));
            }

            osg::NodeVisitor::apply(geode);
        }

        void applyColor(osg::Drawable* drawable)
        {
            r = (r + 3) % 255;
            g = (g + 13) % 255;
            b = (b + 25) % 255;
            brightness = (brightness + 1) % 3;

            // set blending function to use blend color
            osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc(GL_CONSTANT_COLOR, GL_ZERO);
            osg::ref_ptr<osg::BlendColor> bc = new osg::BlendColor(osg::Vec4(r / 255.0 + (brightness - 1) * 0.25, g / 255.0 + (brightness - 1) * 0.25, b / 255.0 + (brightness - 1) * 0.25, 1.0));

            int attributes = osg::StateAttribute::ON | osg::StateAttribute::PROTECTED;
            osg::StateSet* originalState = drawable->getOrCreateStateSet();
            osg::StateSet* state = static_cast<osg::StateSet*>(originalState->clone(osg::CopyOp::SHALLOW_COPY));

            drawable->setStateSet(state);
            state->setAttributeAndModes(blendFunc.get(), attributes);
            state->setAttributeAndModes(bc.get(), attributes);
        }
    };
}

void
Analyzer::analyze(osg::Node* node, ProgressCallback* progress, unsigned numFeatures, double totalTime, const TileKey& tileKey)
{
    if (!node) return;
    if (!progress) return;

    static Threading::Mutex s_analyzeMutex;
    Threading::ScopedMutexLock lock(s_analyzeMutex);
    
    std::cout
        << "...............................................................................\n"
        << "Key = " << tileKey.str()
        << " : Features = " << numFeatures
        << ", Time = " << (int)(1000.0*totalTime)
        << " ms, Avg = " << std::setprecision(3) << (1000.0*(totalTime / (double)numFeatures)) << " ms"
        << std::endl;

    // collect statistics about the resulting scene graph:
    if (progress->collectStats())
    {
        osgUtil::StatsVisitor stats;
        node->accept(stats);
        progress->stats("# unique stateSets") = stats._statesetSet.size();
        progress->stats("# stateSet refs") = stats._numInstancedStateSet;
        progress->stats("# drawables") = stats._drawableSet.size();

        // Prints out the stats details
        stats.print(std::cout);

        // Uncomment to see the number of primitivesets (i.e. number of calls to DrawElements) in each drawable
        //unsigned dc=0;
        //for (osgUtil::StatsVisitor::DrawableSet::const_iterator i = stats._drawableSet.begin(); i != stats._drawableSet.end(); ++i) {
        //    progress->stats(Stringify()<<"# primsets in drawable " << (dc++)) = (*i)->asGeometry()->getNumPrimitiveSets();
        //}

        FindTextures ft;
        node->accept(ft);
        ft.print(std::cout);
    }

    std::stringstream buf;
    buf << "Stats:\n";

    for (ProgressCallback::Stats::const_iterator i = progress->stats().begin(); i != progress->stats().end(); ++i)
    {
        if (i->first.at(0) == '#')
        {
            buf
                << "    "
                << std::setw(15) << i->first
                << std::setw(10) << i->second
                << std::endl;
        }
        else
        {
            buf
                << "    "
                << std::setw(15) << i->first
                << std::setw(6) << (int)(1000.0*i->second) << " ms"
                << std::setw(6) << (int)(100.0*i->second / totalTime) << "%"
                << std::endl;
        }
    }

    std::cout << buf.str() << std::endl;

    // clear them when we are done.
    progress->stats().clear();

    /*ColorVisitor cv;
    node->accept(cv);*/

}