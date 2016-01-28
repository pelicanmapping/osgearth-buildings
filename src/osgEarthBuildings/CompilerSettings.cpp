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
#include "CompilerSettings"

using namespace osgEarth;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

CompilerSettings::CompilerSettings()
{
    //nop
}

CompilerSettings::CompilerSettings(const CompilerSettings& rhs) :
_bins( rhs._bins )
{
    //nop
}

CompilerSettings::Bin&
CompilerSettings::addBin()
{
    _bins.push_back(Bin());
    return _bins.back();
}

const CompilerSettings::Bin*
CompilerSettings::getBin(const std::string& tag) const
{
    for(Bins::const_iterator bin = _bins.begin(); bin != _bins.end(); ++bin)
    {
        if ( tag == bin->tag )
        {
            return &(*bin);
        }
    }
    return 0L;
}

const CompilerSettings::Bin*
CompilerSettings::getBin(const TagSet& tags) const
{
    for(Bins::const_iterator bin = _bins.begin(); bin != _bins.end(); ++bin)
    {
        if ( tags.find(bin->tag) != tags.end() )
        {
            return &(*bin);
        }
    }
    return 0L;
}


CompilerSettings::CompilerSettings(const Config& conf)
{
    const Config* bins = conf.child_ptr("bins");
    if ( bins )
    {
        for(ConfigSet::const_iterator b = bins->children().begin(); b != bins->children().end(); ++b )
        {
            Bin& bin = addBin();
            bin.tag = b->value("tag");
            bin.lodScale = b->value("lod_scale", 1.0f);
        }
    }
}

Config
CompilerSettings::getConfig() const
{
    Config conf("settings");
    if (!_bins.empty())
    {
        Config bins("bins");
        conf.add(bins);

        for(Bins::const_iterator b = _bins.begin(); b != _bins.end(); ++b)
        {
            Config bin("bin");
            bins.add(bin);
            
            if (!b->tag.empty())
                bin.set("tag", b->tag);
            
            bin.set("lodscale", b->lodScale);
        }
    }
    return conf;
}
