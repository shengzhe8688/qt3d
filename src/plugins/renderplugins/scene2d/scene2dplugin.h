/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd and/or its subsidiary(-ies).
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

#ifndef QT3DRENDER_SCENE2DPLUGIN_H
#define QT3DRENDER_SCENE2DPLUGIN_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of other Qt classes.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <private/qrenderaspect_p.h>
#include <private/qrenderplugin_p.h>
#include <private/abstractrenderer_p.h>
#include <private/qresourcemanager_p.h>
#include <private/scene2d_p.h>

QT_BEGIN_NAMESPACE

namespace Qt3DRender {
namespace Render {

class Scene2DNode;

class Scene2DNodeManager : public Qt3DCore::QResourceManager<
        Render::Quick::Scene2D,
        Qt3DCore::QNodeId,
        16,
        Qt3DCore::ObjectLevelLockingPolicy>
{
};

class Scene2DPlugin : public QRenderPlugin
{
public:
    Scene2DPlugin();
    ~Scene2DPlugin();
    bool registerBackendTypes(QRenderAspect *aspect, AbstractRenderer *renderer) Q_DECL_OVERRIDE;
    bool unregisterBackendTypes(QRenderAspect *aspect) Q_DECL_OVERRIDE;
private:
    Render::Scene2DNodeManager *m_scene2dNodeManager;
};

} // Render
} // namespace Qt3DRender

QT_END_NAMESPACE

#endif // QT3DRENDER_SCENE2DPLUGIN_H
