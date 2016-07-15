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

CompilerSettings::CompilerSettings() :
_rangeFactor  ( 6.0f ),
_useClustering( false )
{
    //nop
}

CompilerSettings::CompilerSettings(const CompilerSettings& rhs) :
_rangeFactor( rhs._rangeFactor ),
_useClustering( rhs._useClustering ),
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


CompilerSettings::CompilerSettings(const Config& conf) :
_rangeFactor( 6.0f )
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
    conf.getIfSet("range_factor", _rangeFactor);
    conf.getIfSet("clustering", _useClustering);
    conf.getIfSet("max_verts_per_cluster", _maxVertsPerCluster);
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
    
    conf.addIfSet("range_factor", _rangeFactor);
    conf.addIfSet("clustering", _useClustering);
    conf.addIfSet("max_verts_per_cluster", _maxVertsPerCluster);

    return conf;
}
