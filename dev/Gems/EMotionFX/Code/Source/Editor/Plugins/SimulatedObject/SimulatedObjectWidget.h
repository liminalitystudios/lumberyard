/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#pragma once

#include <EMotionFX/Tools/EMotionStudio/EMStudioSDK/Source/DockWidgetPlugin.h>
#include <Editor/Plugins/SimulatedObject/SimulatedObjectActionManager.h>
#include <Editor/Plugins/SkeletonOutliner/SkeletonOutlinerBus.h>
#include <Editor/SimulatedObjectBus.h>
#include <Editor/SimulatedObjectModel.h>
#include <MCore/Source/Command.h>
#include <Source/Editor/ObjectEditor.h>

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTreeView)

namespace EMotionFX
{
    class Actor;
    class ActorInstance;
    class SimulatedJointWidget;

    class SimulatedObjectWidget
        : public EMStudio::DockWidgetPlugin
        , private EMotionFX::SkeletonOutlinerNotificationBus::Handler
        , private EMotionFX::SimulatedObjectRequestBus::Handler
        , private EMotionFX::ActorEditorNotificationBus::Handler
    {
        Q_OBJECT //AUTOMOC

    public:
        SimulatedObjectWidget();
        ~SimulatedObjectWidget() override;
        SimulatedObjectWidget(const SimulatedObjectWidget&) = delete;
        SimulatedObjectWidget(SimulatedObjectWidget&&) = delete;
        SimulatedObjectWidget& operator=(const SimulatedObjectWidget&) = delete;
        SimulatedObjectWidget& operator=(SimulatedObjectWidget&&) = delete;

        // EMStudioPlugin overrides
        const char* GetName() const override { return "Simulated Object"; }
        uint32 GetClassID() const override { return 0x00861164; }
        bool GetIsClosable() const override { return true; }
        bool GetIsFloatable() const override { return true; }
        bool GetIsVertical() const override { return false; }
        EMStudioPlugin* Clone() override { return new SimulatedObjectWidget(); }
        bool Init() override;
        void Reinit();

        // Render
        void Render(EMStudio::RenderPlugin* renderPlugin, RenderInfo* renderInfo) override;
        void RenderJointRadius(const SimulatedJoint* joint, ActorInstance* actorInstance, const AZ::Color& color);

        SimulatedObjectModel* GetSimulatedObjectModel() const;

        // SkeletonOutlinerNotificationBus overrides
        void OnContextMenu(QMenu* menu, const QModelIndexList& selectedRowIndices) override;

        // SimulatedObjectRequestBus overrides
        void UpdateWidget() override;

        // ActorEditorNotificationBus overrides
        void ActorSelectionChanged(Actor* actor) override;
        void ActorInstanceSelectionChanged(EMotionFX::ActorInstance* actorInstance) override;

        EMStudio::SimulatedObjectActionManager* GetActionManager() const { return m_actionManager.get(); }

    public slots:
        void OnContextMenu(const QPoint& position);
        void OnRemoveSimulatedObject(const QModelIndex& objectIndex);
        void OnRemoveSimulatedJoint(const QModelIndex& jointIndex, bool removeChildren);
        void OnRemoveSimulatedJoints(const QModelIndexList& jointIndices);

        void OnAddCollider();
        void OnClearColliders();

    private:
        EMotionFX::Actor* m_actor = nullptr;
        EMotionFX::ActorInstance* m_actorInstance = nullptr;
        QWidget* m_mainWidget = nullptr;
        QLabel* m_noSelectionWidget = nullptr;
        QWidget* m_selectionWidget = nullptr;
        QTreeView* m_treeView = nullptr;
        AZStd::unique_ptr<SimulatedObjectModel> m_simulatedObjectModel = nullptr;
        AZStd::unique_ptr<EMStudio::SimulatedObjectActionManager> m_actionManager;
        QWidget* m_contentsWidget = nullptr;
        MysticQt::DockWidget* m_simulatedObjectInspectorDock = nullptr;
        SimulatedJointWidget* m_simulatedJointWidget = nullptr;
        QPushButton* m_addSimulatedObjectButton = nullptr;

        // Rendering
        AZStd::vector<AZ::Vector3> m_vertexBuffer;
        AZStd::vector<AZ::u32> m_indexBuffer;
        AZStd::vector<AZ::Vector3> m_lineBuffer;
        AZStd::vector<bool> m_lineValidityBuffer;

        // Callbacks
        MCORE_DEFINECOMMANDCALLBACK(DataChangedCallback);
        // static bool DataChanged(AZ::u32 actorId);
        AZStd::vector<MCore::Command::Callback*> m_commandCallbacks;
    };
} // namespace EMotionFX
