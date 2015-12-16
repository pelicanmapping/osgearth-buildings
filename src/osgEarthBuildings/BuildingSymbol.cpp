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
#include "BuildingSymbol"

using namespace osgEarth;
using namespace osgEarth::Symbology;
using namespace osgEarth::Buildings;

OSGEARTH_REGISTER_SIMPLE_SYMBOL(building, BuildingSymbol);

BuildingSymbol::BuildingSymbol(const Config& conf) :
Symbol          ( conf ),
_metersPerStory ( 3.5f )
{
    mergeConfig(conf);
}

BuildingSymbol::BuildingSymbol(const BuildingSymbol& rhs,const osg::CopyOp& copyop) :
Symbol         (rhs, copyop),
_metersPerStory( rhs._metersPerStory )
{
    //nop
}

Config 
BuildingSymbol::getConfig() const
{
    Config conf = Symbol::getConfig();
    conf.key() = "building";
    conf.addIfSet   ( "meters_per_story", _metersPerStory );
    conf.addObjIfSet( "height",           _heightExpr );
    return conf;
}

void 
BuildingSymbol::mergeConfig( const Config& conf )
{
    conf.getIfSet   ( "meters_per_story", _metersPerStory );
    conf.getObjIfSet( "height",           _heightExpr );
}

void
BuildingSymbol::parseSLD(const Config& c, Style& style)
{
    BuildingSymbol defaults;

    if ( match(c.key(), "building-meters-per-story") ) {
        style.getOrCreate<BuildingSymbol>()->metersPerStory() = as<float>(c.value(), *defaults.metersPerStory());
    }
    else if ( match(c.key(), "building-height") ) {
        style.getOrCreate<BuildingSymbol>()->height() = !c.value().empty() ? NumericExpression(c.value()) : *defaults.height();
    }
}
