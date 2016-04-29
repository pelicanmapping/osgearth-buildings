
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
#include "CompilerOutput"
#include <osg/LOD>
#include <osg/MatrixTransform>
#include <osg/ProxyNode>
#include <osgUtil/Optimizer>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/DrawInstanced>
#include <osgEarth/Registry>
#include <osgEarth/ImageUtils>
#include <osgDB/WriteFile>
#include <set>

using namespace osgEarth;
using namespace osgEarth::Buildings;

#define LC "[CompilerOutput] "

#define GEODES_ROOT           "_oeb_geo"
#define EXTERNALS_ROOT        "_oeb_ext"
#define INSTANCES_ROOT        "_oeb_inr"
#define INSTANCE_MODEL_GROUP  "_oeb_img"
#define INSTANCE_MODEL        "_oeb_inm"
#define DEBUG_ROOT            "_oeb_deb"


CompilerOutput::CompilerOutput() :
_range( FLT_MAX ),
_index( 0L ),
_currentFeature( 0L ),
_globalMutex( 0L )
{
    _externalModelsGroup = new osg::Group();
    _externalModelsGroup->setName(EXTERNALS_ROOT);

    _debugGroup = new osg::Group();
    _debugGroup->setName(DEBUG_ROOT);

    _resourceCache = new ResourceCache();
}

void
CompilerOutput::setLocalToWorld(const osg::Matrix& m)
{
    _local2world = m;
    _world2local.invert(m);
}

void
CompilerOutput::addDrawable(osg::Drawable* drawable)
{
    addDrawable( drawable, "" );
}

void
CompilerOutput::addDrawable(osg::Drawable* drawable, const std::string& tag)
{
    if ( !drawable )
        return;

    osg::ref_ptr<osg::Geode>& geode = _geodes[tag];
    if ( !geode.valid() )
    {
        geode = new osg::Geode();
    }
    geode->addDrawable( drawable );

    if ( _index && _currentFeature )
    {
        _index->tagDrawable( drawable, _currentFeature );
    }
}

void
CompilerOutput::addInstance(ModelResource* model, const osg::Matrix& matrix)
{
    _instances[model].push_back( matrix );

    //TODO: index it. the vector needs to be a vector of pair<matrix,feature>
}

std::string
CompilerOutput::createCacheKey() const
{
#if 1
    if (_key.valid())
    {
        return Stringify() << _key.getLOD() << "_" << _key.getTileX() << "_" << _key.getTileY();
    }
#else
    if (_key.valid())
    {
        unsigned x, y;
        _key.getProfile()->getNumTiles(_key.getLOD(), x, y);
        unsigned xbins = std::min(x, 32u);
        unsigned ybins = std::min(y, 32u);
        unsigned xbin = _key.getTileX() / xbins;
        unsigned ybin = _key.getTileY() / ybins;
        std::string key2 = _key.str();
        osgEarth::replaceIn(key2, "/", "_");
        return Stringify() << _key.getLOD() << "_" << xbin << "_" << ybin << "/" << key2;
    }
#endif
    else if (!_name.empty())
    {
        return _name;
    }
    else
    {
        return "";
    }
}

osg::Node*
CompilerOutput::readFromCache(CacheBin* cacheBin, const CachePolicy& policy, const osgDB::Options* readOptions, ProgressCallback* progress) const
{
    //return 0L;
    if ( !cacheBin ) return 0L;

    std::string cacheKey = createCacheKey();
    if (cacheKey.empty())
        return 0L;

    // read from the cache.
    osg::ref_ptr<osg::Node> node;

    osgEarth::ReadResult result = cacheBin->readObject(cacheKey, readOptions);
    if (result.succeeded())
    {
        if (policy.isExpired(result.lastModifiedTime()))
        {
            OE_DEBUG << LC << "Tile " << _name << " is cached but expired.\n";
            return 0L;
        }

        OE_INFO << LC << "Loaded " << _name << " from the cache (key = " << cacheKey << ")\n";
        return result.releaseNode();
    }

    else
    {
        return 0L;
    }
}

namespace
{
#define IMAGE_PREFIX "i_"

    struct WriteImagesToCache : public osgEarth::TextureAndImageVisitor
    {
        CacheBin*         _bin;
        osgDB::Options*   _writeOptions;
        Threading::Mutex& _mutex;

        WriteImagesToCache(CacheBin* bin, osgDB::Options* writeOptions, Threading::Mutex& mutex)
            : TextureAndImageVisitor(), _bin(bin), _writeOptions(writeOptions), _mutex(mutex)
        {
            setTraversalMode( TRAVERSE_ALL_CHILDREN );
            setNodeMaskOverride( ~0L );
        }

        void apply(osg::Image& image)
        {
            std::string path = image.getFileName();
            if (path.empty())
            {
                OE_WARN << LC << "ERROR image with blank filename.\n";
            }

            if (!osgEarth::startsWith(path, IMAGE_PREFIX))
            {
                // take a plugin-global mutex to avoid two threads altering the image
                // at the same time
                Threading::ScopedMutexLock lock(_mutex);

                if (!osgEarth::startsWith(path, IMAGE_PREFIX))
                {
                    std::string cacheKey = Stringify() << IMAGE_PREFIX << std::hex << osgEarth::hashString(path);

                    // TODO: adding the .osgb here works with the file system cache only.
                    // We need to use a pseudoloader to route this load to a cache bin
                    image.setFileName(cacheKey + ".osgearth_cachebin");
                    image.setWriteHint(osg::Image::WriteHint::EXTERNAL_FILE);

                    CacheBin::RecordStatus rs = _bin->getRecordStatus(cacheKey);
                    if (rs != CacheBin::STATUS_OK)
                    {
                        // The OSGB serializer won't actually write the image data without this:
                        osg::ref_ptr<osgDB::Options> dbo = osg::clone(_writeOptions);
                        dbo->setPluginStringData("WriteImageHint", "IncludeData");

                        OE_INFO << LC << "Writing image \"" << path << "\" to the cache as \"" << cacheKey << "\"\n";

                        if (!_bin->write(cacheKey, &image, dbo.get()))
                        {
                            OE_WARN << LC << "...error, write failed!\n";
                        }
                    }
                    else
                    {
                        //OE_INFO << LC << "..Image \"" << path << "\" already cached\n";
                    }
                }
            }
        }
    };
}

void
CompilerOutput::writeToCache(osg::Node* node, CacheBin* cacheBin, ProgressCallback* progress) const
{
    if ( !node || !cacheBin )
        return;

    std::string cacheKey = createCacheKey();
    if (cacheKey.empty())
        return;

    prepareForCaching(node);

    osg::ref_ptr<osgDB::Options> writeOptions = new osgDB::Options();
    writeOptions->setPluginStringData("Compressor", "zlib");
    writeOptions->setPluginStringData("WriteImageHint", "UseExternal");

    // Write any referenced images to the cache:
    WriteImagesToCache writeImages(cacheBin, writeOptions.get(), *_globalMutex);
    node->accept(writeImages);

    // Write the node to the cache:
    cacheBin->write(cacheKey, node, writeOptions.get());

    OE_INFO << LC << "Wrote " << _name << " to cache (key = " << cacheKey << ")\n";
}

osg::Node*
CompilerOutput::createSceneGraph(Session*                session,
                                 const CompilerSettings& settings,
                                 const osgDB::Options*   readOptions,
                                 ProgressCallback*       progress) const
{
    OE_START_TIMER(total);

    // install the master matrix for this graph:
    osg::ref_ptr<osg::MatrixTransform> root = new osg::MatrixTransform( getLocalToWorld() );

    // tagged geodes:
    if ( !_geodes.empty() )
    {
        // The Geode LOD holds each geode in its range.
        osg::LOD* geodeLOD = new osg::LOD();
        geodeLOD->setName(GEODES_ROOT);
        root->addChild( geodeLOD );

        for(TaggedGeodes::const_iterator g = _geodes.begin(); g != _geodes.end(); ++g)
        {
            const std::string& tag = g->first;
            const CompilerSettings::Bin* bin = settings.getBin(tag);
            float maxRange = bin? _range*bin->lodScale : FLT_MAX;
            geodeLOD->addChild( g->second.get(), 0.0f, maxRange );
        }
    }

    if ( _externalModelsGroup->getNumChildren() > 0 )
    {
        root->addChild( _externalModelsGroup.get() );
    }
    
    // Run an optimization pass before adding any debug data or models
    // NOTE: be careful; don't mess with state during optimization.
    OE_START_TIMER(optimize);
    {
        // because the default merge limit is 10000 and there's no other way to change it
        osgUtil::Optimizer::MergeGeometryVisitor mgv;
        mgv.setTargetMaximumNumberOfVertices( 250000u );
        root->accept( mgv );
    }
    double optimizeTime = OE_GET_TIMER(optimize);

    
    // install the model instances, creating one instance group for each model.
    OE_START_TIMER(instances);
    {
        // group to hold all instanced models:
        osg::LOD* instances = new osg::LOD();
        instances->setName(INSTANCES_ROOT);

        // keeps one copy of each instanced model per resource:
        //typedef std::map< ModelResource*, osg::ref_ptr<osg::ProxyNode> > ModelNodes;
        typedef std::map< ModelResource*, osg::ref_ptr<osg::Node> > ModelNodes;
        ModelNodes modelNodes;

        for(InstanceMap::const_iterator i = _instances.begin(); i != _instances.end(); ++i)
        {
            ModelResource* res = i->first.get();

            // look up or create the node corresponding to this instance:
            osg::ref_ptr<osg::Node>& modelNode = modelNodes[res];
            if (!modelNode.valid())
            {                
                // Instance models use the GLOBAL resource cache, so that an instance model gets
                // loaded only once. Then it's cloned for each tile. That way the shader generator
                // will never touch live data. (Note that texture images are memory-cached in the
                // readOptions.)
                if (!session->getResourceCache()->cloneOrCreateInstanceNode(res, modelNode, readOptions))
                {
                    OE_WARN << LC << "Failed to materialize resource " << res->uri()->full() << "\n";
                }
            }

            if ( modelNode.valid() )
            {
                modelNode->setName(INSTANCE_MODEL);

                osg::Group* modelGroup = new osg::Group();
                modelGroup->setName(INSTANCE_MODEL_GROUP);

                // Build a normal scene graph based on MatrixTransforms, and then convert it 
                // over to use instancing if it's available.
                const MatrixVector& mats = i->second;
                for(MatrixVector::const_iterator m = mats.begin(); m != mats.end(); ++m)
                {
                    osg::MatrixTransform* modelxform = new osg::MatrixTransform( *m );
                    modelxform->addChild( modelNode.get() );
                    modelGroup->addChild( modelxform );
                }

                // check for a display bin for this model resource:
                const CompilerSettings::Bin* bin = settings.getBin( res->tags() );
                float lodScale = bin ? bin->lodScale : 1.0f;

                float maxRange = std::max( _range*lodScale, modelGroup->getBound().radius() ); 
                instances->addChild( modelGroup, 0.0, maxRange );
            }
        }
              
        // finally add all the instance groups.
        root->addChild( instances );
    }
    double instanceTime = OE_GET_TIMER(instances);

    if ( progress && progress->collectStats() )
    {
        progress->stats("out.optimize" ) = optimizeTime;
        progress->stats("out.instances") = instanceTime;
        progress->stats("out.total")     = OE_GET_TIMER(total);
    }

    return root.release();
}

namespace
{
    /**
     * Performs all the shader component installation on the scene graph. 
     * Once this is done the model is ready to render. This happens as an
     * isolated operation because we cannot write a graph with shader 
     * components to the cache.
     */
    struct PostProcessNodeVisitor : public osg::NodeVisitor
    {
        osg::ref_ptr<StateSetCache> _sscache;
        unsigned _models, _instanceGroups, _geodes;

        PostProcessNodeVisitor() : osg::NodeVisitor()
        {
            setTraversalMode(TRAVERSE_ALL_CHILDREN);
            setNodeMaskOverride(~0);

            _sscache = new StateSetCache();

            _models = 0;
            _instanceGroups = 0;
            _geodes = 0;
        }

        void apply(osg::Node& node)
        {
            if (node.getName() == GEODES_ROOT)
            {
                _geodes++;
                Registry::instance()->shaderGenerator().run(&node, "Building geodes", _sscache.get());
            }
            
            else if (node.getName() == INSTANCE_MODEL_GROUP)
            {
                _instanceGroups++;
                DrawInstanced::convertGraphToUseDrawInstanced(node.asGroup());
            }

            else if (node.getName() == INSTANCES_ROOT)
            {
                DrawInstanced::install(node.getOrCreateStateSet());
            }

            else if (node.getName() == INSTANCE_MODEL)
            {
                _models++;
                Registry::instance()->shaderGenerator().run(&node, "Resource Model", _sscache.get());
            }

            traverse(node);
        }
    };
}

void
CompilerOutput::postProcess(osg::Node* graph, ProgressCallback* progress) const
{
    if (!graph) return;

    // We do this in a separate step because we cannot cache the node once 
    // shader components are added to it.
    PostProcessNodeVisitor ppnv;
    graph->accept(ppnv);

    //OE_INFO << "Post Process (" << _name << ") IGs=" << ppnv._instanceGroups << ", MODELS=" << ppnv._models << ", GEODES=" << ppnv._geodes << "\n";
}

namespace
{
    /**
     * Visitor that makes sure a scene graph is suitable for caching to OSG format.
     * It will remove any UserDataContainers, and it will mark any textures so they
     * do not unref shared images after applying them.
     */
    struct PrepareForCaching : public osg::NodeVisitor
    {
        osg::ref_ptr<StateSetCache> _sscache;
        unsigned _textures;
        unsigned _userDataClears;

        PrepareForCaching() : osg::NodeVisitor()
        {
            setTraversalMode(TRAVERSE_ALL_CHILDREN);
            setNodeMaskOverride(~0);
            _textures = 0;
            _userDataClears = 0;
        }

        void apply(osg::Node& node)
        {
            apply(node.getStateSet());
            applyUserData(node);
            traverse(node);
        }

        void apply(osg::Geode& geode)
        {
            for (unsigned i = 0; i < geode.getNumDrawables(); ++i)
            {
                apply(geode.getDrawable(i));
            }
            apply(static_cast<osg::Node&>(geode));
        }

        void apply(osg::Drawable* drawable)
        {
            if (!drawable) return;
            apply(drawable->getStateSet());
            applyUserData(*drawable);
        }

        void apply(osg::StateSet* ss)
        {
            if (!ss) return;

            osg::StateSet::AttributeList& a0 = ss->getAttributeList();
            for (osg::StateSet::AttributeList::iterator i = a0.begin(); i != a0.end(); ++i)
            {
                osg::StateAttribute* sa = i->second.first.get();
                applyUserData(*sa);
            }

            // Disable the texture image-unref feature so we can share the resource 
            // across cached tiles.
            osg::StateSet::TextureAttributeList& a = ss->getTextureAttributeList();
            for (osg::StateSet::TextureAttributeList::iterator i = a.begin(); i != a.end(); ++i)
            {       
                osg::StateSet::AttributeList& b = *i;
                for (osg::StateSet::AttributeList::iterator j = b.begin(); j != b.end(); ++j)
                {
                    osg::StateAttribute* sa = j->second.first.get();
                    if (sa)
                    {
                        osg::Texture* tex = dynamic_cast<osg::Texture*>(sa);
                        if (tex)
                        {              
                            tex->setUnRefImageDataAfterApply(false);               

                            // OSG's DatabasePager attaches "marker objects" to Textures' UserData when it runs a
                            // FindCompileableGLObjectsVisitor. This operation is not thread-safe; it doesn't
                            // account for the possibility that the texture may already be in use elsewhere.
                            //
                            // To prevent a threading violation, and the ensuing crash that reliably occurs
                            // in Release mode (but not Debug for whatever reason) we are forced to make a
                            // shallow clone of the Texture object and use that for serialization instead of
                            // the original, since the original may change in the middle of the process.
                            // We then replace the original with our close locally and serialize it safely.
                            //
                            // This "hack" prevents a crash in OSG 3.4.0 when trying to modify and then write
                            // serialize the scene graph containing these shared texture objects.
                            // Kudos to Jason B for figuring this one out.

                            osg::Texture* texClone = osg::clone(tex, osg::CopyOp::SHALLOW_COPY);
                            if ( texClone )
                            {
                                for (unsigned k = 0; k < texClone->getNumImages(); ++k)
                                {
                                    osg::Image* image = texClone->getImage(k);
                                    if ( image )
                                    {
                                        applyUserData(*image);
                                    }
                                }

                                applyUserData(*texClone);

                                j->second.first = texClone;
                            }
                            else
                            {
                                OE_WARN << LC << "Texture clone failed.\n";
                            }
                        }
                        else
                        {
                            applyUserData(*sa);
                        }
                    }
                }
            }

            applyUserData(*ss);
        }

        void applyUserData(osg::Object& object)
        {
            if (object.getUserData())
            {
                _userDataClears++;
            }
            object.setUserDataContainer(0L);
        }
    };
}

void
CompilerOutput::prepareForCaching(osg::Node* node) const
{
    if (!node) return;

    PrepareForCaching v;
    node->accept(v);

    //OE_INFO << "Prep Cache (" << _name << ") TEX=" << v._textures << ", clears = " << v._userDataClears << "\n";
}