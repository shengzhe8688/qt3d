/****************************************************************************
**
** Copyright (C) 2014 Klaralvdalens Datakonsult AB (KDAB).
** Copyright (C) 2016 The Qt Company Ltd and/or its subsidiary(-ies).
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "renderview_p.h"
#include <Qt3DRender/qmaterial.h>
#include <Qt3DRender/qrenderaspect.h>
#include <Qt3DRender/qrendertarget.h>
#include <Qt3DRender/qlight.h>
#include <Qt3DRender/private/sphere_p.h>

#include <Qt3DRender/private/cameraselectornode_p.h>
#include <Qt3DRender/private/framegraphnode_p.h>
#include <Qt3DRender/private/layerfilternode_p.h>
#include <Qt3DRender/private/qparameter_p.h>
#include <Qt3DRender/private/cameralens_p.h>
#include <Qt3DRender/private/rendercommand_p.h>
#include <Qt3DRender/private/effect_p.h>
#include <Qt3DRender/private/entity_p.h>
#include <Qt3DRender/private/renderer_p.h>
#include <Qt3DRender/private/nodemanagers_p.h>
#include <Qt3DRender/private/layer_p.h>
#include <Qt3DRender/private/renderlogging_p.h>
#include <Qt3DRender/private/renderpassfilternode_p.h>
#include <Qt3DRender/private/renderpass_p.h>
#include <Qt3DRender/private/geometryrenderer_p.h>
#include <Qt3DRender/private/renderstateset_p.h>
#include <Qt3DRender/private/techniquefilternode_p.h>
#include <Qt3DRender/private/viewportnode_p.h>
#include <Qt3DRender/private/buffermanager_p.h>
#include <Qt3DRender/private/geometryrenderermanager_p.h>

#include <Qt3DRender/private/stringtoint_p.h>
#include <Qt3DRender/qparametermapping.h>
#include <Qt3DCore/qentity.h>
#include <QtGui/qsurface.h>
#include <algorithm>

#include <QDebug>
#if defined(QT3D_RENDER_VIEW_JOB_TIMINGS)
#include <QElapsedTimer>
#endif

QT_BEGIN_NAMESPACE

namespace Qt3DRender {
namespace Render {


namespace  {

const int qNodeIdTypeId = qMetaTypeId<Qt3DCore::QNodeId>();

const int MAX_LIGHTS = 8;

const QString LIGHT_POSITION_NAME = QStringLiteral(".position");
const QString LIGHT_TYPE_NAME = QStringLiteral(".type");
const QString LIGHT_COLOR_NAME = QStringLiteral(".color");
const QString LIGHT_INTENSITY_NAME = QStringLiteral(".intensity");

int LIGHT_COUNT_NAME_ID = 0;
int LIGHT_POSITION_NAMES[MAX_LIGHTS];
int LIGHT_TYPE_NAMES[MAX_LIGHTS];
int LIGHT_COLOR_NAMES[MAX_LIGHTS];
int LIGHT_INTENSITY_NAMES[MAX_LIGHTS];
QString LIGHT_STRUCT_NAMES[MAX_LIGHTS];

// TODO: Should we treat lack of layer data as implicitly meaning that an
// entity is in all layers?
bool isEntityInLayers(const Entity *entity, const QVector<int> &filterLayerIds)
{
    if (filterLayerIds.isEmpty())
        return true;

    QList<Layer *> entityLayers = entity->renderComponents<Layer>();
    Q_FOREACH (const Layer *entityLayer, entityLayers) {
        if (entityLayer->isEnabled()) {
            Q_FOREACH (const int layerId, entityLayer->layerIds())
                if (filterLayerIds.contains(layerId))
                    return true;
        }
    }
    return false;
}

bool isEntityFrustumCulled(const Entity *entity, const Plane *planes)
{
    const Sphere *s = entity->worldBoundingVolumeWithChildren();
    for (int i = 0; i < 6; ++i) {
        if (QVector3D::dotProduct(s->center(), planes[i].normal) + planes[i].d < -s->radius())
            return true;
    }
    return false;
}

} // anonymouse namespace

bool wasInitialized = false;
RenderView::StandardUniformsPFuncsHash RenderView::ms_standardUniformSetters;
QStringList RenderView::ms_standardAttributesNames = RenderView::initializeStandardAttributeNames();



RenderView::StandardUniformsPFuncsHash RenderView::initializeStandardUniformSetters()
{
    RenderView::StandardUniformsPFuncsHash setters;

    setters.insert(StringToInt::lookupId(QStringLiteral("modelMatrix")), &RenderView::modelMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("viewMatrix")), &RenderView::viewMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("projectionMatrix")), &RenderView::projectionMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("modelView")), &RenderView::modelViewMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("modelViewProjection")), &RenderView::modelViewProjectionMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("mvp")), &RenderView::modelViewProjectionMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("inverseModelMatrix")), &RenderView::inverseModelMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("inverViewMatrix")), &RenderView::inverseViewMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("inverseProjectionMatrix")), &RenderView::inverseProjectionMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("inverseModelView")), &RenderView::inverseModelViewMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("inverseModelViewProjection")), &RenderView::inverseModelViewProjectionMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("modelNormalMatrix")), &RenderView::modelNormalMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("modelViewNormal")), &RenderView::modelViewNormalMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("viewportMatrix")), &RenderView::viewportMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("inverseViewportMatrix")), &RenderView::inverseViewportMatrix);
    setters.insert(StringToInt::lookupId(QStringLiteral("time")), &RenderView::time);
    setters.insert(StringToInt::lookupId(QStringLiteral("eyePosition")), &RenderView::eyePosition);

    return setters;
}

QStringList RenderView::initializeStandardAttributeNames()
{
    QStringList attributesNames;

    attributesNames << QAttribute::defaultPositionAttributeName();
    attributesNames << QAttribute::defaultTextureCoordinateAttributeName();
    attributesNames << QAttribute::defaultNormalAttributeName();
    attributesNames << QAttribute::defaultColorAttributeName();
    attributesNames << QAttribute::defaultTangentAttributeName();

    return attributesNames;
}

QUniformValue RenderView::modelMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue(model));
}

QUniformValue RenderView::viewMatrix(const QMatrix4x4 &) const
{
    return QUniformValue(QVariant::fromValue(*m_data->m_viewMatrix));
}

QUniformValue RenderView::projectionMatrix(const QMatrix4x4 &) const
{
    return QUniformValue(QVariant::fromValue(m_data->m_renderCamera->projection()));
}

QUniformValue RenderView::modelViewMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue(*m_data->m_viewMatrix * model));
}

QUniformValue RenderView::modelViewProjectionMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue(*m_data->m_viewProjectionMatrix * model));
}

QUniformValue RenderView::inverseModelMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue(model.inverted()));
}

QUniformValue RenderView::inverseViewMatrix(const QMatrix4x4 &) const
{
    return QUniformValue(QVariant::fromValue(m_data->m_viewMatrix->inverted()));
}

QUniformValue RenderView::inverseProjectionMatrix(const QMatrix4x4 &) const
{
    QMatrix4x4 projection;
    if (m_data->m_renderCamera)
        projection = m_data->m_renderCamera->projection();
    return QUniformValue(QVariant::fromValue(projection.inverted()));
}

QUniformValue RenderView::inverseModelViewMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue((*m_data->m_viewMatrix * model).inverted()));
}

QUniformValue RenderView::inverseModelViewProjectionMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue((*m_data->m_viewProjectionMatrix * model).inverted(0)));
}

QUniformValue RenderView::modelNormalMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue(model.normalMatrix()));
}

QUniformValue RenderView::modelViewNormalMatrix(const QMatrix4x4 &model) const
{
    return QUniformValue(QVariant::fromValue((*m_data->m_viewMatrix * model).normalMatrix()));
}

// TODO: Move this somewhere global where GraphicsContext::setViewport() can use it too
static QRectF resolveViewport(const QRectF &fractionalViewport, const QSize &surfaceSize)
{
    return QRectF(fractionalViewport.x() * surfaceSize.width(),
                  (1.0 - fractionalViewport.y() - fractionalViewport.height()) * surfaceSize.height(),
                  fractionalViewport.width() * surfaceSize.width(),
                  fractionalViewport.height() * surfaceSize.height());
}

QUniformValue RenderView::viewportMatrix(const QMatrix4x4 &model) const
{
    // TODO: Can we avoid having to pass the model matrix in to these functions?
    Q_UNUSED(model);
    QMatrix4x4 viewportMatrix;
    viewportMatrix.viewport(resolveViewport(*m_viewport, m_surfaceSize));
    return QUniformValue(QVariant::fromValue(viewportMatrix));

}

QUniformValue RenderView::inverseViewportMatrix(const QMatrix4x4 &model) const
{
    Q_UNUSED(model);
    QMatrix4x4 viewportMatrix;
    viewportMatrix.viewport(resolveViewport(*m_viewport, m_surfaceSize));
    QMatrix4x4 inverseViewportMatrix = viewportMatrix.inverted();
    return QUniformValue(QVariant::fromValue(inverseViewportMatrix));

}

QUniformValue RenderView::time(const QMatrix4x4 &model) const
{
    Q_UNUSED(model);
    qint64 time = m_renderer->time();
    float t = time / 1000000000.0f;
    return QUniformValue(QVariant(t));
}

QUniformValue RenderView::eyePosition(const QMatrix4x4 &model) const
{
    Q_UNUSED(model);
    return QUniformValue(QVariant::fromValue(m_data->m_eyePos));
}

RenderView::RenderView()
    : m_renderer(Q_NULLPTR)
    , m_devicePixelRatio(1.)
    , m_allocator(Q_NULLPTR)
    , m_data(Q_NULLPTR)
    , m_clearColor(Q_NULLPTR)
    , m_viewport(Q_NULLPTR)
    , m_surface(Q_NULLPTR)
    , m_clearBuffer(QClearBuffer::None)
    , m_stateSet(Q_NULLPTR)
    , m_noDraw(false)
    , m_compute(false)
    , m_frustumCulling(false)
{
    m_workGroups[0] = 1;
    m_workGroups[1] = 1;
    m_workGroups[2] = 1;

    if (Q_UNLIKELY(!wasInitialized)) {
        // Needed as we can control the init order of static/global variables across compile units
        // and this hash relies on the static StringToInt class
        wasInitialized = true;
        RenderView::ms_standardUniformSetters = RenderView::initializeStandardUniformSetters();
        LIGHT_COUNT_NAME_ID = StringToInt::lookupId(QStringLiteral("lightCount"));
        for (int i = 0; i < MAX_LIGHTS; ++i) {
            LIGHT_STRUCT_NAMES[i] = QStringLiteral("lights[") + QString::number(i) + QLatin1Char(']');
            LIGHT_POSITION_NAMES[i] = StringToInt::lookupId(LIGHT_STRUCT_NAMES[i] + LIGHT_POSITION_NAME);
            LIGHT_TYPE_NAMES[i] = StringToInt::lookupId(LIGHT_STRUCT_NAMES[i] + LIGHT_TYPE_NAME);
            LIGHT_COLOR_NAMES[i] = StringToInt::lookupId(LIGHT_STRUCT_NAMES[i] + LIGHT_COLOR_NAME);
            LIGHT_INTENSITY_NAMES[i] = StringToInt::lookupId(LIGHT_STRUCT_NAMES[i] + LIGHT_INTENSITY_NAME);
        }
    }
}

RenderView::~RenderView()
{
    if (m_allocator == Q_NULLPTR) // Mainly needed for unit tests
        return;

    Q_FOREACH (RenderCommand *command, m_commands) {

        if (command->m_stateSet != Q_NULLPTR) // We do not delete the RenderState as that is stored statically
            m_allocator->deallocate<RenderStateSet>(command->m_stateSet);

        // Deallocate RenderCommand
        m_allocator->deallocate<RenderCommand>(command);
    }

    // Deallocate viewMatrix/viewProjectionMatrix
    m_allocator->deallocate<QMatrix4x4>(m_data->m_viewMatrix);
    m_allocator->deallocate<QMatrix4x4>(m_data->m_viewProjectionMatrix);
    // Deallocate viewport rect
    m_allocator->deallocate<QRectF>(m_viewport);
    // Deallocate clearColor
    m_allocator->deallocate<QColor>(m_clearColor);
    // Deallocate m_data
    m_allocator->deallocate<InnerData>(m_data);
    // Deallocate m_stateSet
    if (m_stateSet)
        m_allocator->deallocate<RenderStateSet>(m_stateSet);
}

// We need to overload the delete operator so that when the Renderer deletes the list of RenderViews, each RenderView
// can clear itself with the frame allocator and deletes its RenderViews
void RenderView::operator delete(void *ptr)
{
    RenderView *rView = static_cast<RenderView *>(ptr);
    if (rView != Q_NULLPTR && rView->m_allocator != Q_NULLPTR)
        rView->m_allocator->deallocateRawMemory(rView, sizeof(RenderView));
}

// Since placement new is used we need a matching operator delete, at least MSVC complains otherwise
void RenderView::operator delete(void *ptr, void *)
{
    RenderView *rView = static_cast<RenderView *>(ptr);
    if (rView != Q_NULLPTR && rView->m_allocator != Q_NULLPTR)
        rView->m_allocator->deallocateRawMemory(rView, sizeof(RenderView));
}

void RenderView::sort()
{
    // Compares the bitsetKey of the RenderCommands
    // Key[Depth | StateCost | Shader]
    std::sort(m_commands.begin(), m_commands.end(), compareCommands);

    // Minimize uniform changes
    int i = 0;
    while (i < m_commands.size()) {
        int j = i;

        // Advance while commands share the same shader
        while (i < m_commands.size() && m_commands[j]->m_shaderDna == m_commands[i]->m_shaderDna)
            ++i;

        if (i - j > 0) { // Several commands have the same shader, so we minimize uniform changes
            PackUniformHash cachedUniforms = m_commands[j++]->m_parameterPack.uniforms();

            while (j < i) {
                // We need the reference here as we are modifying the original container
                // not the copy
                PackUniformHash &uniforms = m_commands[j]->m_parameterPack.m_uniforms;
                PackUniformHash::iterator it = uniforms.begin();
                const PackUniformHash::iterator end = uniforms.end();

                while (it != end) {
                    // We are comparing the values:
                    // - raw uniform values
                    // - the texture Node id if the uniform represents a texture
                    // since all textures are assigned texture units before the RenderCommands
                    // sharing the same material (shader) are rendered, we can't have the case
                    // where two uniforms, referencing the same texture eventually have 2 different
                    // texture unit values
                    QUniformValue refValue = cachedUniforms.value(it.key());
                    if (refValue == it.value()) {
                        it = uniforms.erase(it);
                    } else {
                        cachedUniforms.insert(it.key(), it.value());
                        ++it;
                    }
                }
                ++j;
            }
        }
        ++i;
    }
}

void RenderView::setRenderer(Renderer *renderer)
{
    m_renderer = renderer;
    m_manager = renderer->nodeManagers();
    m_data->m_uniformBlockBuilder.shaderDataManager = m_manager->shaderDataManager();
}

// Returns an array of Passes with the accompanying Parameter list
RenderRenderPassList RenderView::passesAndParameters(ParameterInfoList *parameters, Entity *node, bool useDefaultMaterial)
{
    // Find the material, effect, technique and set of render passes to use
    Material *material = Q_NULLPTR;
    Effect *effect = Q_NULLPTR;
    if ((material = node->renderComponent<Material>()) != Q_NULLPTR && material->isEnabled())
        effect = m_renderer->nodeManagers()->effectManager()->lookupResource(material->effect());
    Technique *technique = findTechniqueForEffect(m_renderer, this, effect);

    // Order set:
    // 1 Pass Filter
    // 2 Technique Filter
    // 3 Material
    // 4 Effect
    // 5 Technique
    // 6 RenderPass

    // Add Parameters define in techniqueFilter and passFilter
    // passFilter have priority over techniqueFilter
    if (m_data->m_passFilter)
        parametersFromParametersProvider(parameters, m_renderer->nodeManagers()->parameterManager(),
                                         m_data->m_passFilter);
    if (m_data->m_techniqueFilter)
        parametersFromParametersProvider(parameters, m_renderer->nodeManagers()->parameterManager(),
                                         m_data->m_techniqueFilter);
    // Get the parameters for our selected rendering setup (override what was defined in the technique/pass filter)
    parametersFromMaterialEffectTechnique(parameters, m_manager->parameterManager(), material, effect, technique);

    RenderRenderPassList passes;
    if (technique) {
        passes = findRenderPassesForTechnique(m_manager, this, technique);
    } else if (useDefaultMaterial) {
        material = m_manager->data<Material, MaterialManager>(m_renderer->defaultMaterialHandle());
        effect = m_manager->data<Effect, EffectManager>(m_renderer->defaultEffectHandle());
        technique = m_manager->data<Technique, TechniqueManager>(m_renderer->defaultTechniqueHandle());
        passes << m_manager->data<RenderPass, RenderPassManager>(m_renderer->defaultRenderPassHandle());
    }
    return passes;
}

void RenderView::gatherLights(Entity *node)
{
    const QList<Light *> lights = node->renderComponents<Light>();
    if (!lights.isEmpty())
        m_lightSources.append(LightSource(node, lights));

    // Traverse children
    Q_FOREACH (Entity *child, node->children())
        gatherLights(child);
}

class LightSourceCompare
{
public:
    LightSourceCompare(Entity *node) { p = node->worldBoundingVolume()->center(); }
    bool operator()(const RenderView::LightSource &a, const RenderView::LightSource &b) const {
        const float distA = p.distanceToPoint(a.entity->worldBoundingVolume()->center());
        const float distB = p.distanceToPoint(b.entity->worldBoundingVolume()->center());
        return distA < distB;
    }

private:
    QVector3D p;
};

// TODO: Convert into a job that caches lookup of entities for layers
static void findEntitiesInLayers(Entity *e, const QVector<int> &filterLayerIds, QVector<Entity *> &entities)
{
    // Bail if sub-tree is disabled
    if (!e->isEnabled())
        return;

    // Check this entity
    if (isEntityInLayers(e, filterLayerIds))
        entities.push_back(e);

    // Recurse to children
    Q_FOREACH (Entity *child, e->children())
        findEntitiesInLayers(child, filterLayerIds, entities);
}

// Tries to order renderCommand by shader so as to minimize shader changes
void RenderView::buildRenderCommands(Entity *rootEntity, const Plane *planes)
{
#if defined(QT3D_RENDER_VIEW_JOB_TIMINGS)
    QElapsedTimer timer;
    timer.start();
#endif
    // Filter the entities according to the layers for this renderview
    QVector<Entity *> entities;
    const int entityCount = m_renderer->nodeManagers()->renderNodesManager()->count();
    entities.reserve(entityCount);
    findEntitiesInLayers(rootEntity, m_data->m_layerIds, entities);
#if defined(QT3D_RENDER_VIEW_JOB_TIMINGS)
    qDebug() << "Found" << entities.size() << "entities in layers" << m_data->m_layers
             << "in" << timer.nsecsElapsed() / 1.0e6 << "ms";
#endif

    Q_FOREACH (Entity *entity, entities) {
        if (m_compute) {
            // 1) Look for Compute Entities if Entity -> components [ ComputeJob, Material ]
            // when the RenderView is part of a DispatchCompute branch
            buildComputeRenderCommands(entity);
        } else {
            // 2) Look for Drawable  Entities if Entity -> components [ GeometryRenderer, Material ]
            // when the RenderView is not part of a DispatchCompute branch
            // Investigate if it's worth doing as separate jobs
            buildDrawRenderCommands(entity, planes);
        }
    }
}

void RenderView::buildDrawRenderCommands(Entity *node, const Plane *planes)
{
    if (m_frustumCulling && isEntityFrustumCulled(node, planes))
        return;

    GeometryRenderer *geometryRenderer = Q_NULLPTR;
    HGeometryRenderer geometryRendererHandle = node->componentHandle<GeometryRenderer, 16>();
    if (!geometryRendererHandle.isNull()
            && (geometryRenderer = m_manager->geometryRendererManager()->data(geometryRendererHandle)) != Q_NULLPTR
            && geometryRenderer->isEnabled()
            && !geometryRenderer->geometryId().isNull()) {

        // There is a geometry renderer with geometry
        ParameterInfoList parameters;
        RenderRenderPassList passes = passesAndParameters(&parameters, node, true);

        // 1 RenderCommand per RenderPass pass on an Entity with a Mesh
        Q_FOREACH (RenderPass *pass, passes) {
            // Add the RenderPass Parameters
            ParameterInfoList globalParameters = parameters;
            parametersFromParametersProvider(&globalParameters, m_manager->parameterManager(), pass);

            RenderCommand *command = m_allocator->allocate<RenderCommand>();
            command->m_depth = m_data->m_eyePos.distanceToPoint(node->worldBoundingVolume()->center());
            command->m_geometry = m_manager->lookupHandle<Geometry, GeometryManager, HGeometry>(geometryRenderer->geometryId());
            command->m_geometryRenderer = geometryRendererHandle;
            // For RenderPass based states we use the globally set RenderState
            // if no renderstates are defined as part of the pass. That means:
            // RenderPass { renderStates: [] } will use the states defined by
            // StateSet in the FrameGraph
            if (pass->hasRenderStates()) {
                command->m_stateSet = m_allocator->allocate<RenderStateSet>();
                addToRenderStateSet(command->m_stateSet, pass, m_manager->renderStateManager());

                // Merge per pass stateset with global stateset
                // so that the local stateset only overrides
                if (m_stateSet != Q_NULLPTR)
                    command->m_stateSet->merge(m_stateSet);
                command->m_changeCost = m_renderer->defaultRenderState()->changeCost(command->m_stateSet);
            }

            // Pick which lights to take in to account.
            // For now decide based on the distance by taking the MAX_LIGHTS closest lights.
            // Replace with more sophisticated mechanisms later.
            std::sort(m_lightSources.begin(), m_lightSources.end(), LightSourceCompare(node));
            QVector<LightSource> activeLightSources; // NB! the total number of lights here may still exceed MAX_LIGHTS
            int lightCount = 0;
            for (int i = 0; i < m_lightSources.count() && lightCount < MAX_LIGHTS; ++i) {
                activeLightSources.append(m_lightSources[i]);
                lightCount += m_lightSources[i].lights.count();
            }

            setShaderAndUniforms(command, pass, globalParameters, *(node->worldTransform()), activeLightSources);

            buildSortingKey(command);
            m_commands.append(command);
        }
    }
}

void RenderView::buildComputeRenderCommands(Entity *node)
{
    // If the RenderView contains only a ComputeDispatch then it cares about
    // A ComputeDispatch is also implicitely a NoDraw operation
    // enabled flag
    // layer component
    // material/effect/technique/parameters/filters/
    ComputeJob *computeJob = Q_NULLPTR;
    if (node->componentHandle<ComputeJob, 16>() != HComputeJob()
            && (computeJob = node->renderComponent<ComputeJob>()) != Q_NULLPTR
            && computeJob->isEnabled()) {

        ParameterInfoList parameters;
        RenderRenderPassList passes = passesAndParameters(&parameters, node, true);

        Q_FOREACH (RenderPass *pass, passes) {
            // Add the RenderPass Parameters
            ParameterInfoList globalParameters = parameters;
            parametersFromParametersProvider(&globalParameters, m_manager->parameterManager(), pass);

            RenderCommand *command = m_allocator->allocate<RenderCommand>();
            command->m_type = RenderCommand::Compute;
            setShaderAndUniforms(command,
                                 pass,
                                 globalParameters,
                                 *(node->worldTransform()),
                                 QVector<LightSource>());
            m_commands.append(command);
        }
    }
}

const AttachmentPack &RenderView::attachmentPack() const
{
    return m_attachmentPack;
}

void RenderView::setUniformValue(ShaderParameterPack &uniformPack, int nameId, const QVariant &value)
{
    Texture *tex = Q_NULLPTR;
    // At this point a uniform value can only be a scalar type
    // or a Qt3DCore::QNodeId corresponding to a Texture
    // ShaderData/Buffers would be handled as UBO/SSBO and would therefore
    // not be in the default uniform block
    if (static_cast<QMetaType::Type>(value.userType()) == qNodeIdTypeId) {
        // Speed up conversion to avoid using QVariant::value()
        const Qt3DCore::QNodeId texId = variant_value<Qt3DCore::QNodeId>(value);
        if ((tex = m_manager->textureManager()->lookupResource(texId))
                != Q_NULLPTR) {
            uniformPack.setTexture(nameId, tex->peerUuid());
            //TextureUniform *texUniform = m_allocator->allocate<TextureUniform>();
            QUniformValue texUniform;
            texUniform.setType(QUniformValue::TextureSampler);
            texUniform.setTextureId(tex->peerUuid());
            uniformPack.setUniform(nameId, texUniform);
        }
    } else {
        uniformPack.setUniform(nameId, QUniformValue(value));
    }
}

void RenderView::setStandardUniformValue(ShaderParameterPack &uniformPack, int glslNameId, int nameId, const QMatrix4x4 &worldTransform)
{
    uniformPack.setUniform(glslNameId, (this->*ms_standardUniformSetters[nameId])(worldTransform));
}

void RenderView::setUniformBlockValue(ShaderParameterPack &uniformPack,
                                      Shader *shader,
                                      const ShaderUniformBlock &block,
                                      const QVariant &value)
{
    Q_UNUSED(shader)

    if (static_cast<QMetaType::Type>(value.userType()) == qNodeIdTypeId) {

        Buffer *buffer = Q_NULLPTR;
        if ((buffer = m_manager->bufferManager()->lookupResource(value.value<Qt3DCore::QNodeId>())) != Q_NULLPTR) {
            BlockToUBO uniformBlockUBO;
            uniformBlockUBO.m_blockIndex = block.m_index;
            uniformBlockUBO.m_bufferID = buffer->peerUuid();
            uniformPack.setUniformBuffer(uniformBlockUBO);
            // Buffer update to GL buffer will be done at render time
        }


        //ShaderData *shaderData = Q_NULLPTR;
        // if ((shaderData = m_manager->shaderDataManager()->lookupResource(value.value<Qt3DCore::QNodeId>())) != Q_NULLPTR) {
        // UBO are indexed by <ShaderId, ShaderDataId> so that a same QShaderData can be used among different shaders
        // while still making sure that if they have a different layout everything will still work
        // If two shaders define the same block with the exact same layout, in that case the UBO could be shared
        // but how do we know that ? We'll need to compare ShaderUniformBlocks

        // Note: we assume that if a buffer is shared accross multiple shaders
        // then it implies that they share the same layout

        // Temporarly disabled

        //        BufferShaderKey uboKey(shaderData->peerUuid(),
        //                               shader->peerUuid());

        //        BlockToUBO uniformBlockUBO;
        //        uniformBlockUBO.m_blockIndex = block.m_index;
        //        uniformBlockUBO.m_shaderDataID = shaderData->peerUuid();
        //        bool uboNeedsUpdate = false;

        //        // build UBO at uboId if not created before
        //        if (!m_manager->glBufferManager()->contains(uboKey)) {
        //            m_manager->glBufferManager()->getOrCreateResource(uboKey);
        //            uboNeedsUpdate = true;
        //        }

        //        // If shaderData  has been updated (property has changed or one of the nested properties has changed)
        //        // foreach property defined in the QShaderData, we try to fill the value of the corresponding active uniform(s)
        //        // for all the updated properties (all the properties if the UBO was just created)
        //        if (shaderData->updateViewTransform(*m_data->m_viewMatrix) || uboNeedsUpdate) {
        //            // Clear previous values remaining in the hash
        //            m_data->m_uniformBlockBuilder.activeUniformNamesToValue.clear();
        //            // Update only update properties if uboNeedsUpdate is true, otherwise update the whole block
        //            m_data->m_uniformBlockBuilder.updatedPropertiesOnly = uboNeedsUpdate;
        //            // Retrieve names and description of each active uniforms in the uniform block
        //            m_data->m_uniformBlockBuilder.uniforms = shader->activeUniformsForUniformBlock(block.m_index);
        //            // Builds the name-value map for the block
        //            m_data->m_uniformBlockBuilder.buildActiveUniformNameValueMapStructHelper(shaderData, block.m_name);
        //            if (!uboNeedsUpdate)
        //                shaderData->markDirty();
        //            // copy the name-value map into the BlockToUBO
        //            uniformBlockUBO.m_updatedProperties = m_data->m_uniformBlockBuilder.activeUniformNamesToValue;
        //            uboNeedsUpdate = true;
        //        }

        //        uniformBlockUBO.m_needsUpdate = uboNeedsUpdate;
        //        uniformPack.setUniformBuffer(uniformBlockUBO);
        // }
    }
}

void RenderView::setShaderStorageValue(ShaderParameterPack &uniformPack,
                                       Shader *shader,
                                       const ShaderStorageBlock &block,
                                       const QVariant &value)
{
    Q_UNUSED(shader)
    if (static_cast<QMetaType::Type>(value.type()) == qNodeIdTypeId) {
        Buffer *buffer = Q_NULLPTR;
        if ((buffer = m_manager->bufferManager()->lookupResource(value.value<Qt3DCore::QNodeId>())) != Q_NULLPTR) {
            BlockToSSBO shaderStorageBlock;
            shaderStorageBlock.m_blockIndex = block.m_index;
            shaderStorageBlock.m_bufferID = buffer->peerUuid();
            uniformPack.setShaderStorageBuffer(shaderStorageBlock);
            // Buffer update to GL buffer will be done at render time
        }
    }
}

void RenderView::setDefaultUniformBlockShaderDataValue(ShaderParameterPack &uniformPack, Shader *shader, ShaderData *shaderData, const QString &structName)
{
    m_data->m_uniformBlockBuilder.activeUniformNamesToValue.clear();

    // updates transformed properties;
    shaderData->updateViewTransform(*m_data->m_viewMatrix);
    // Force to update the whole block
    m_data->m_uniformBlockBuilder.updatedPropertiesOnly = false;
    // Retrieve names and description of each active uniforms in the uniform block
    m_data->m_uniformBlockBuilder.uniforms = shader->activeUniformsForUniformBlock(-1);
    // Build name-value map for the block
    m_data->m_uniformBlockBuilder.buildActiveUniformNameValueMapStructHelper(shaderData, structName);
    // Set uniform values for each entrie of the block name-value map
    QHash<int, QVariant>::const_iterator activeValuesIt = m_data->m_uniformBlockBuilder.activeUniformNamesToValue.constBegin();
    const QHash<int, QVariant>::const_iterator activeValuesEnd = m_data->m_uniformBlockBuilder.activeUniformNamesToValue.constEnd();

    while (activeValuesIt != activeValuesEnd) {
        setUniformValue(uniformPack, activeValuesIt.key(), activeValuesIt.value());
        ++activeValuesIt;
    }
}

void RenderView::buildSortingKey(RenderCommand *command)
{
    // Build a bitset key depending on the SortingCriterion
    int sortCount = m_data->m_sortingCriteria.count();

    // If sortCount == 0, no sorting is applied

    // Handle at most 4 filters at once
    for (int i = 0; i < sortCount && i < 4; i++) {
        SortCriterion *sC = m_manager->lookupResource<SortCriterion, SortCriterionManager>(m_data->m_sortingCriteria[i]);

        switch (sC->sortType()) {
        case QSortCriterion::StateChangeCost:
            command->m_sortingType.sorts[i] = command->m_changeCost; // State change cost
            break;
        case QSortCriterion::BackToFront:
            command->m_sortBackToFront = true; // Depth value
            break;
        case QSortCriterion::Material:
            command->m_sortingType.sorts[i] = command->m_shaderDna; // Material
            break;
        default:
            Q_UNREACHABLE();
        }
    }
}

void RenderView::setShaderAndUniforms(RenderCommand *command, RenderPass *rPass, ParameterInfoList &parameters, const QMatrix4x4 &worldTransform,
                                      const QVector<LightSource> &activeLightSources)
{
    // The VAO Handle is set directly in the renderer thread so as to avoid having to use a mutex here
    // Set shader, technique, and effect by basically doing :
    // ShaderProgramManager[MaterialManager[frontentEntity->id()]->Effect->Techniques[TechniqueFilter->name]->RenderPasses[RenderPassFilter->name]];
    // The Renderer knows that if one of those is null, a default material / technique / effect as to be used

    // Find all RenderPasses (in order) matching values set in the RenderPassFilter
    // Get list of parameters for the Material, Effect, and Technique
    // For each ParameterBinder in the RenderPass -> create a QUniformPack
    // Once that works, improve that to try and minimize QUniformPack updates

    if (rPass != Q_NULLPTR) {
        // Index Shader by Shader UUID
        command->m_shader = m_manager->lookupHandle<Shader, ShaderManager, HShader>(rPass->shaderProgram());
        Shader *shader = Q_NULLPTR;
        if ((shader = m_manager->data<Shader, ShaderManager>(command->m_shader)) != Q_NULLPTR) {
            command->m_shaderDna = shader->dna();

            // Builds the QUniformPack, sets shader standard uniforms and store attributes name / glname bindings
            // If a parameter is defined and not found in the bindings it is assumed to be a binding of Uniform type with the glsl name
            // equals to the parameter name
            const QVector<int> uniformNamesIds = shader->uniformsNamesIds();
            const QVector<int> uniformBlockNamesIds = shader->uniformBlockNamesIds();
            const QVector<int> shaderStorageBlockNamesIds = shader->storageBlockNamesIds();
            const QVector<QString> attributeNames = shader->attributesNames();

            // Set fragData Name and index
            // Later on we might want to relink the shader if attachments have changed
            // But for now we set them once and for all
            QHash<QString, int> fragOutputs;
            if (!m_renderTarget.isNull() && !shader->isLoaded()) {
                Q_FOREACH (const Attachment &att, m_attachmentPack.attachments()) {
                    if (att.m_type <= QRenderAttachment::ColorAttachment15)
                        fragOutputs.insert(att.m_name, att.m_type);
                }
            }

            if (!uniformNamesIds.isEmpty() || !attributeNames.isEmpty() || !shaderStorageBlockNamesIds.isEmpty()) {

                // Set default standard uniforms without bindings
                Q_FOREACH (const int uniformNameId, uniformNamesIds) {
                    if (ms_standardUniformSetters.contains(uniformNameId))
                        setStandardUniformValue(command->m_parameterPack, uniformNameId, uniformNameId, worldTransform);
                }

                // Set default attributes
                Q_FOREACH (const QString &attributeName, attributeNames) {
                    if (ms_standardAttributesNames.contains(attributeName))
                        command->m_parameterAttributeToShaderNames.insert(attributeName, attributeName);
                }

                // Set uniforms and attributes explicitly binded
                Q_FOREACH (const ParameterMapping &binding, rPass->bindings()) {
                    const ParameterInfoList::const_iterator it = findParamInfo(&parameters, binding.parameterNameId());

                    if (it == parameters.end()) {
                        // A Parameters wasn't found with the name binding.parameterName
                        // -> we need to use the binding.shaderVariableName
                        switch (binding.bindingType()) {

                        case QParameterMapping::Attribute:
                            if (attributeNames.contains(binding.shaderVariableName())) {
                                command->m_parameterAttributeToShaderNames.insert(binding.parameterName(), binding.shaderVariableName());
                                break;
                            }
                        case QParameterMapping::StandardUniform:
                            if (uniformNamesIds.contains(binding.shaderVariableNameId())
                                    && ms_standardUniformSetters.contains(binding.parameterNameId())) {
                                setStandardUniformValue(command->m_parameterPack, binding.shaderVariableNameId(), binding.parameterNameId(), worldTransform);
                                break;
                            }

                        case QParameterMapping::FragmentOutput:
                            if (fragOutputs.contains(binding.parameterName())) {
                                fragOutputs.insert(binding.shaderVariableName(), fragOutputs.take(binding.parameterName()));
                                break;
                            }

                        case QParameterMapping::UniformBufferObject:
                            if (uniformBlockNamesIds.contains(binding.parameterNameId())) {
                                setUniformBlockValue(command->m_parameterPack, shader, shader->uniformBlockForBlockNameId(it->nameId), it->value);
                                break;
                            }

                        case QParameterMapping::ShaderStorageBufferObject:
                            if (shaderStorageBlockNamesIds.contains(binding.parameterNameId())) {
                                setShaderStorageValue(command->m_parameterPack, shader, shader->storageBlockForBlockNameId(it->nameId), it->value);
                                break;
                            }

                        default:
                            qCWarning(Render::Backend) << Q_FUNC_INFO << "Trying to bind a Parameter that hasn't been defined " << binding.parameterName();
                            break;
                        }
                    }
                }

                // Parameters remaining could be
                // -> uniform scalar / vector
                // -> uniform struct / arrays
                // -> uniform block / array (4.3)
                // -> ssbo block / array (4.3)

                if ((!uniformNamesIds.isEmpty() || !uniformBlockNamesIds.isEmpty() || !shaderStorageBlockNamesIds.isEmpty())
                        && !parameters.isEmpty()) {
                    ParameterInfoList::const_iterator it = parameters.cbegin();
                    const ParameterInfoList::const_iterator parametersEnd = parameters.cend();
                    while (it != parametersEnd) {
                        if (uniformNamesIds.contains(it->nameId)) { // Parameter is a regular uniform
                            setUniformValue(command->m_parameterPack, it->nameId, it->value);
                        } else if (uniformBlockNamesIds.indexOf(it->nameId) != -1) { // Parameter is a uniform block
                            setUniformBlockValue(command->m_parameterPack, shader, shader->uniformBlockForBlockNameId(it->nameId), it->value);
                        } else if (shaderStorageBlockNamesIds.indexOf(it->nameId) != -1) { // Parameters is a SSBO
                            setShaderStorageValue(command->m_parameterPack, shader, shader->storageBlockForBlockNameId(it->nameId), it->value);
                        } else { // Parameter is a struct
                            const QVariant &v = it->value;
                            ShaderData *shaderData = Q_NULLPTR;
                            if (static_cast<QMetaType::Type>(v.type()) == qNodeIdTypeId &&
                                    (shaderData = m_manager->shaderDataManager()->lookupResource(v.value<Qt3DCore::QNodeId>())) != Q_NULLPTR) {
                                // Try to check if we have a struct or array matching a QShaderData parameter
                                setDefaultUniformBlockShaderDataValue(command->m_parameterPack, shader, shaderData, StringToInt::lookupString(it->nameId));
                            }
                            // Otherwise: param unused by current shader
                        }
                        ++it;
                    }
                }

                // Lights

                int lightIdx = 0;
                Q_FOREACH (const LightSource &lightSource, activeLightSources) {
                    if (lightIdx == MAX_LIGHTS)
                        break;
                    Entity *lightEntity = lightSource.entity;
                    const QVector3D worldPos = lightEntity->worldBoundingVolume()->center();
                    Q_FOREACH (Light *light, lightSource.lights) {
                        if (lightIdx == MAX_LIGHTS)
                            break;
                        setUniformValue(command->m_parameterPack, LIGHT_POSITION_NAMES[lightIdx], worldPos);
                        setUniformValue(command->m_parameterPack, LIGHT_TYPE_NAMES[lightIdx], int(QLight::PointLight));
                        setUniformValue(command->m_parameterPack, LIGHT_COLOR_NAMES[lightIdx], QVector3D(1.0f, 1.0f, 1.0f));
                        setUniformValue(command->m_parameterPack, LIGHT_INTENSITY_NAMES[lightIdx], QVector3D(0.5f, 0.5f, 0.5f));
                        setDefaultUniformBlockShaderDataValue(command->m_parameterPack, shader, light, LIGHT_STRUCT_NAMES[lightIdx]);
                        ++lightIdx;
                    }
                }

                if (uniformNamesIds.contains(LIGHT_COUNT_NAME_ID))
                    setUniformValue(command->m_parameterPack, LIGHT_COUNT_NAME_ID, qMax(1, lightIdx));

                if (activeLightSources.isEmpty()) {
                    setUniformValue(command->m_parameterPack, LIGHT_POSITION_NAMES[0], QVector3D(10.0f, 10.0f, 0.0f));
                    setUniformValue(command->m_parameterPack, LIGHT_TYPE_NAMES[0], int(QLight::PointLight));
                    setUniformValue(command->m_parameterPack, LIGHT_COLOR_NAMES[0], QVector3D(1.0f, 1.0f, 1.0f));
                    setUniformValue(command->m_parameterPack, LIGHT_INTENSITY_NAMES[0], QVector3D(0.5f, 0.5f, 0.5f));
                }
            }
            // Set frag outputs in the shaders if hash not empty
            if (!fragOutputs.isEmpty())
                shader->setFragOutputs(fragOutputs);
        }
    }
    else {
        qCWarning(Render::Backend) << Q_FUNC_INFO << "Using default effect as none was provided";
    }
}

} // namespace Render
} // namespace Qt3DRender

QT_END_NAMESPACE
