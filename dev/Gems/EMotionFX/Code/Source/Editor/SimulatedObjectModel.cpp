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

#include <EMotionFX/Source/AnimGraphInstance.h>
#include <EMotionFX/Source/BlendTreeSimulatedObjectNode.h>
#include <EMotionFX/Source/Skeleton.h>
#include <EMotionFX/CommandSystem/Source/CommandManager.h>
#include <EMotionFX/CommandSystem/Source/SimulatedObjectCommands.h>
#include <EMotionFX/CommandSystem/Source/ColliderCommands.h>
#include <Source/Editor/SimulatedObjectModel.h>
#include <QtGui/QFont>
#include <Editor/QtMetaTypes.h>

namespace EMotionFX
{
    int SimulatedObjectModel::s_columnCount = 1;

    SimulatedObjectModel::SimulatedObjectModel()
        : m_selectionModel(new QItemSelectionModel(this))
        , m_objectIcon(":/EMotionFX/SimulatedObject_White.png")
    {
        m_selectionModel->setModel(this);

        ActorInstance* selectedActorInstance = nullptr;
        ActorEditorRequestBus::BroadcastResult(selectedActorInstance, &ActorEditorRequests::GetSelectedActorInstance);
        if (selectedActorInstance)
        {
            SetActorInstance(selectedActorInstance);
        }
        else
        {
            Actor* selectedActor = nullptr;
            ActorEditorRequestBus::BroadcastResult(selectedActor, &ActorEditorRequests::GetSelectedActor);
            SetActor(selectedActor);
        }

        RegisterCommandCallbacks();
    }

    SimulatedObjectModel::~SimulatedObjectModel()
    {
        for (auto* callback : m_commandCallbacks)
        {
            CommandSystem::GetCommandManager()->RemoveCommandCallback(callback, false);
            delete callback;
        }
    }

    void SimulatedObjectModel::RegisterCommandCallbacks()
    {
        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAddSimulatedObjectPreCallback>(CommandAddSimulatedObject::s_commandName, m_commandCallbacks, this, true, true);
        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAddSimulatedObjectPostCallback>(CommandAddSimulatedObject::s_commandName, m_commandCallbacks, this);

        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandRemoveSimulatedObjectPreCallback>(CommandRemoveSimulatedObject::s_commandName, m_commandCallbacks, this, true, true);
        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandRemoveSimulatedObjectPostCallback>(CommandRemoveSimulatedObject::s_commandName, m_commandCallbacks, this);

        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAdjustSimulatedObjectPostCallback>(CommandAdjustSimulatedObject::s_commandName, m_commandCallbacks, this, false, false);

        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAddSimulatedJointsPreCallback>(CommandAddSimulatedJoints::s_commandName, m_commandCallbacks, this, true, true);
        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAddSimulatedJointsPostCallback>(CommandAddSimulatedJoints::s_commandName, m_commandCallbacks, this);

        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandRemoveSimulatedJointsPreCallback>(CommandRemoveSimulatedJoints::s_commandName, m_commandCallbacks, this, true, true);
        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandRemoveSimulatedJointsPostCallback>(CommandRemoveSimulatedJoints::s_commandName, m_commandCallbacks, this);

        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAdjustSimulatedJointPostCallback>(CommandAdjustSimulatedJoint::s_commandName, m_commandCallbacks, this, false, false);

        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAddColliderPostCallback>(CommandAddCollider::s_commandName, m_commandCallbacks, this, /*executePreUndo=*/false, /*executePreCommandfalse=*/false);
        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandAdjustColliderPostCallback>(CommandAdjustCollider::s_commandName, m_commandCallbacks, this, /*executePreUndo=*/false, /*executePreCommandfalse=*/false);
        CommandSystem::GetCommandManager()->RegisterCommandCallback<CommandRemoveColliderPostCallback>(CommandRemoveCollider::s_commandName, m_commandCallbacks, this, /*executePreUndo=*/false, /*executePreCommandfalse=*/false);
    }

    void SimulatedObjectModel::SetActor(Actor* actor)
    {
        m_actorInstance = nullptr;
        m_actor = actor;
        m_skeleton = nullptr;
        if (m_actor)
        {
            m_skeleton = actor->GetSkeleton();
        }
        InitModel(actor);
    }

    void SimulatedObjectModel::SetActorInstance(ActorInstance* actorInstance)
    {
        m_actorInstance = actorInstance;
        m_actor = nullptr;
        m_skeleton = nullptr;
        if (m_actorInstance)
        {
            m_actor = actorInstance->GetActor();
            m_skeleton = m_actor->GetSkeleton();
        }
        InitModel(m_actor);
    }

    void SimulatedObjectModel::InitModel(Actor* actor)
    {
        AZ_Assert(actor == m_actor, "Expected actor member to already be equal to specified actor pointer.");

        // Clear the model contents.
        beginResetModel();
        endResetModel();
    }

    QModelIndex SimulatedObjectModel::index(int row, int column, const QModelIndex& parent) const
    {
        if (!m_actor)
        {
            return QModelIndex();
        }

        const SimulatedObjectSetup* simulatedObjectSetup = m_actor->GetSimulatedObjectSetup().get();
        if (!simulatedObjectSetup || simulatedObjectSetup->GetNumSimulatedObjects() == 0)
        {
            // Can't build model index because there isn't any simulated object
            return QModelIndex();
        }

        if (!parent.isValid())
        {
            // Parent are not valid. This must be a simulated object.
            if (row >= static_cast<int>(simulatedObjectSetup->GetNumSimulatedObjects()))
            {
                return QModelIndex();
            }

            SimulatedObject* object = simulatedObjectSetup->GetSimulatedObject(row);
            return createIndex(row, column, object);
        }
        else
        {
            // Parent are valid. Is parent a simulated object or a simulated joint?
            const SimulatedCommon* common = static_cast<const SimulatedCommon*>(parent.internalPointer());
            if (azrtti_istypeof<SimulatedJoint>(common))
            {
                const SimulatedJoint* parentJoint = static_cast<SimulatedJoint*>(parent.internalPointer());
                SimulatedJoint* childJoint = parentJoint->FindChildSimulatedJoint(row);
                return createIndex(row, column, childJoint);
            }
            else
            {
                SimulatedObject* object = static_cast<SimulatedObject*>(parent.internalPointer());
                SimulatedJoint* joint = object->GetSimulatedRootJoint(row);
                return createIndex(row, column, joint);
            }

            return QModelIndex();
        }
    }

    QModelIndex SimulatedObjectModel::parent(const QModelIndex& child) const
    {
        if (!m_actor)
        {
            AZ_Assert(false, "Cannot get parent model index. Actor invalid.");
            return QModelIndex();
        }

        const SimulatedObjectSetup* simulatedObjectSetup = m_actor->GetSimulatedObjectSetup().get();
        AZ_Assert(child.isValid(), "Expected valid child model index.");
        const SimulatedCommon* common = static_cast<const SimulatedCommon*>(child.internalPointer());
        if (azrtti_istypeof<SimulatedJoint>(common))
        {
            const SimulatedJoint* childJoint = static_cast<const SimulatedJoint*>(common);
            SimulatedObject* simulatedObject = simulatedObjectSetup->FindSimulatedObjectByJoint(childJoint);
            if (simulatedObject)
            {
                SimulatedJoint* parentJoint = childJoint->FindParentSimulatedJoint();
                if (parentJoint)
                {
                    return createIndex(parentJoint->CalculateChildIndex(), 0, parentJoint);
                }
                else
                {
                    const AZ::Outcome<size_t> simulatedObjectIndex = simulatedObjectSetup->FindSimulatedObjectIndex(simulatedObject);
                    if (simulatedObjectIndex.IsSuccess())
                    {
                        return createIndex(static_cast<int>(simulatedObjectIndex.GetValue()), 0, simulatedObject);
                    }
                }
            }
        }

        return QModelIndex();
    }

    int SimulatedObjectModel::rowCount(const QModelIndex& parent) const
    {
        if (!m_actor)
        {
            return 0;
        }

        const SimulatedObjectSetup* simulatedObjectSetup = m_actor->GetSimulatedObjectSetup().get();
        if (!simulatedObjectSetup || simulatedObjectSetup->GetNumSimulatedObjects() == 0)
        {
            return 0;
        }

        if (parent.isValid())
        {
            const SimulatedCommon* common = static_cast<const SimulatedCommon*>(parent.internalPointer());
            if (azrtti_istypeof<SimulatedJoint>(common))
            {
                const SimulatedJoint* joint = static_cast<SimulatedJoint*>(parent.internalPointer());
                const size_t childJointCount = joint->CalculateNumChildSimulatedJoints();
                return static_cast<int>(childJointCount);
            }
            else
            {
                const SimulatedObject* simulatedObject = static_cast<const SimulatedObject*>(common);
                return static_cast<int>(simulatedObject->GetNumSimulatedRootJoints());
            }
        }
        else
        {
            return static_cast<int>(simulatedObjectSetup->GetNumSimulatedObjects());
        }
    }

    int SimulatedObjectModel::columnCount(const QModelIndex& parent) const
    {
        return s_columnCount;
    }

    QVariant SimulatedObjectModel::headerData(int section, Qt::Orientation orientation, int role) const
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        {
            switch (section)
            {
            case COLUMN_NAME:
                return "Name";
            default:
                return "";
            }
        }

        return QVariant();
    }

    Qt::ItemFlags SimulatedObjectModel::flags(const QModelIndex& index) const
    {
        Qt::ItemFlags result = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;

        if (!m_skeleton || !index.isValid())
        {
            AZ_Assert(false, "Cannot get model data. Skeleton or model index invalid.");
            return Qt::NoItemFlags;
        }

        return result;
    }

    QVariant SimulatedObjectModel::data(const QModelIndex& index, int role) const
    {
        if (!m_actor || !m_skeleton || !index.isValid())
        {
            AZ_Assert(false, "Cannot get model data. Skeleton or model index invalid.");
            return QVariant();
        }

        SimulatedObject* object = nullptr;
        SimulatedJoint* joint = nullptr;
        const SimulatedObjectSetup* simulatedObjectSetup = m_actor->GetSimulatedObjectSetup().get();

        SimulatedCommon* simulatedCommon = static_cast<SimulatedCommon*>(index.internalPointer());
        if (azrtti_istypeof<SimulatedJoint>(simulatedCommon))
        {
            joint = static_cast<SimulatedJoint*>(simulatedCommon);
        }
        else
        {
            object = static_cast<SimulatedObject*>(simulatedCommon);
        }

        switch (role)
        {
        case Qt::DisplayRole:
        {
            switch (index.column())
            {
            case COLUMN_NAME:
            {
                if (object)
                {
                    return object->GetName().c_str();
                }
                if (joint)
                {
                    EMotionFX::Node* node = m_skeleton->GetNode(joint->GetSkeletonJointIndex());
                    return node->GetName();
                }
            }
            default:
                break;
            }
            break;
        }
        case Qt::CheckStateRole:
        {
            break;
        }
        case Qt::DecorationRole:
        {
            if (index.column() == COLUMN_NAME && object)
            {
                return m_objectIcon;
            }
            break;
        }
        case ROLE_OBJECT_PTR:
        {
            return QVariant::fromValue(object);
        }
        case ROLE_OBJECT_INDEX:
        {
            if (!object)
            {
                object = simulatedObjectSetup->FindSimulatedObjectByJoint(joint);
            }
            AZ::Outcome<size_t> outcome = simulatedObjectSetup->FindSimulatedObjectIndex(object);
            if (outcome.IsSuccess())
            {
                return static_cast<quint64>(outcome.GetValue());
            }
        }
        case ROLE_JOINT_PTR:
        {
            return QVariant::fromValue(joint);
        }
        case ROLE_JOINT_BOOL:
        {
            return joint != nullptr;
        }
        case ROLE_ACTOR_PTR:
        {
            return QVariant::fromValue(m_actor);
        }
        default:
            break;
        }

        return QVariant();
    }

    void SimulatedObjectModel::PreAddObject()
    {
        if (!m_actor || !m_actor->GetSimulatedObjectSetup())
        {
            return;
        }

        const SimulatedObjectSetup* simulatedObjectSetup = m_actor->GetSimulatedObjectSetup().get();
        const int first = static_cast<int>(simulatedObjectSetup->GetNumSimulatedObjects());
        beginInsertRows(QModelIndex(), first, first);
    }

    void SimulatedObjectModel::PostAddObject()
    {
        if (!m_actor || !m_actor->GetSimulatedObjectSetup())
        {
            return;
        }

        endInsertRows();
    }

    void SimulatedObjectModel::PreRemoveObject(size_t objectIndex)
    {
        if (!m_actor || !m_actor->GetSimulatedObjectSetup())
        {
            return;
        }

        const int first = static_cast<int>(objectIndex);
        beginRemoveRows(QModelIndex(), first, first);
    }

    void SimulatedObjectModel::PostRemoveObject()
    {
        if (!m_actor || !m_actor->GetSimulatedObjectSetup())
        {
            return;
        }

        endRemoveRows();
    }

    // Update the unique data on any blend tree simulated object node because the objects / joints has been modified.
    void SimulatedObjectModel::OnModelModified()
    {
        if (m_actorInstance)
        {
            AnimGraphInstance* animGraphInstance = m_actorInstance->GetAnimGraphInstance();
            if (animGraphInstance)
            {
                AZStd::vector<EMotionFX::AnimGraphNode*> simulatedObjectNodes;
                animGraphInstance->GetAnimGraph()->RecursiveCollectNodesOfType(azrtti_typeid<EMotionFX::BlendTreeSimulatedObjectNode>(), &simulatedObjectNodes);
                for (EMotionFX::AnimGraphNode* simulatedObjectNode : simulatedObjectNodes)
                {
                    simulatedObjectNode->OnUpdateUniqueData(animGraphInstance);
                }
            }
        }
    }
}
