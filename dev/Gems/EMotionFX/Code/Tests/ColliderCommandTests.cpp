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

#include "ActorFixture.h"
#include <MCore/Source/CommandGroup.h>
#include <EMotionFX/Source/Actor.h>
#include <EMotionFX/CommandSystem/Source/CommandManager.h>
#include <EMotionFX/CommandSystem/Source/ColliderCommands.h>


namespace EMotionFX
{
    class ColliderCommandTests
        : public ActorFixture
    {
        void SetUp()
        {
            ActorFixture::SetUp();
        }

        void TearDown()
        {
            ActorFixture::TearDown();
        }
    };

    size_t CountColliders(const Actor* actor, PhysicsSetup::ColliderConfigType colliderConfigType, bool ignoreShapeType = true, Physics::ShapeType shapeTypeToCount = Physics::ShapeType::Box)
    {
        const AZStd::shared_ptr<PhysicsSetup>& physicsSetup = actor->GetPhysicsSetup();
        Physics::CharacterColliderConfiguration* colliderConfig = physicsSetup->GetColliderConfigByType(colliderConfigType);
        if (!colliderConfig)
        {
            return 0;
        }

        size_t result = 0;
        for (const Physics::CharacterColliderNodeConfiguration& nodeConfig : colliderConfig->m_nodes)
        {
            if (ignoreShapeType)
            {
                // Count in all colliders.
                result += nodeConfig.m_shapes.size();
            }
            else
            {
                // Count in only the given collider type.
                for (const Physics::ShapeConfigurationPair& shapeConfigPair : nodeConfig.m_shapes)
                {
                    if (shapeConfigPair.second->GetShapeType() == shapeTypeToCount)
                    {
                        result++;
                    }
                }
            }
        }

        return result;
    }

    TEST_F(ColliderCommandTests, AddRemoveColliders)
    {
        AZStd::string result;
        CommandSystem::CommandManager commandManager;
        MCore::CommandGroup commandGroup;

        const AZ::u32 actorId = m_actor->GetID();
        const AZStd::vector<AZStd::string> jointNames = GetTestJointNames();
        const size_t jointCount = jointNames.size();


        // 1. Add colliders
        const AZStd::string serializedBeforeAdd = SerializePhysicsSetup(m_actor);
        for (const AZStd::string& jointName : jointNames)
        {
            CommandColliderHelpers::AddCollider(actorId, jointName, PhysicsSetup::HitDetection, azrtti_typeid<Physics::BoxShapeConfiguration>(), &commandGroup);
            CommandColliderHelpers::AddCollider(actorId, jointName, PhysicsSetup::HitDetection, azrtti_typeid<Physics::CapsuleShapeConfiguration>(), &commandGroup);
            CommandColliderHelpers::AddCollider(actorId, jointName, PhysicsSetup::HitDetection, azrtti_typeid<Physics::SphereShapeConfiguration>(), &commandGroup);
        }

        EXPECT_TRUE(commandManager.ExecuteCommandGroup(commandGroup, result));
            const AZStd::string serializedAfterAdd = SerializePhysicsSetup(m_actor);
            EXPECT_EQ(jointCount * 3, CountColliders(m_actor, PhysicsSetup::HitDetection));
            EXPECT_EQ(jointCount, CountColliders(m_actor, PhysicsSetup::HitDetection, /*ignoreShapeType*/false, Physics::ShapeType::Box));

        EXPECT_TRUE(commandManager.Undo(result));
            EXPECT_EQ(0, CountColliders(m_actor, PhysicsSetup::HitDetection));
            EXPECT_EQ(serializedBeforeAdd, SerializePhysicsSetup(m_actor));

        EXPECT_TRUE(commandManager.Redo(result));
            EXPECT_EQ(jointCount * 3, CountColliders(m_actor, PhysicsSetup::HitDetection));
            EXPECT_EQ(jointCount, CountColliders(m_actor, PhysicsSetup::HitDetection, /*ignoreShapeType*/false, Physics::ShapeType::Box));
            EXPECT_EQ(serializedAfterAdd, SerializePhysicsSetup(m_actor));


        // 2. Remove colliders
        commandGroup.RemoveAllCommands();
        const AZStd::string serializedBeforeRemove = SerializePhysicsSetup(m_actor);

        size_t colliderIndexToRemove = 1;
        for (const AZStd::string& jointName : jointNames)
        {
            CommandColliderHelpers::RemoveCollider(actorId, jointName, PhysicsSetup::HitDetection, colliderIndexToRemove, &commandGroup);
        }

        EXPECT_TRUE(commandManager.ExecuteCommandGroup(commandGroup, result));
            const AZStd::string serializedAfterRemove = SerializePhysicsSetup(m_actor);
            EXPECT_EQ(jointCount * 2, CountColliders(m_actor, PhysicsSetup::HitDetection));
            EXPECT_EQ(0, CountColliders(m_actor, PhysicsSetup::HitDetection, /*ignoreShapeType*/false, Physics::ShapeType::Capsule));

        EXPECT_TRUE(commandManager.Undo(result));
            EXPECT_EQ(jointCount * 3, CountColliders(m_actor, PhysicsSetup::HitDetection));
            EXPECT_EQ(serializedBeforeRemove, SerializePhysicsSetup(m_actor));

        EXPECT_TRUE(commandManager.Redo(result));
            EXPECT_EQ(jointCount * 2, CountColliders(m_actor, PhysicsSetup::HitDetection));
            EXPECT_EQ(0, CountColliders(m_actor, PhysicsSetup::HitDetection, /*ignoreShapeType*/false, Physics::ShapeType::Capsule));
            EXPECT_EQ(serializedAfterRemove, SerializePhysicsSetup(m_actor));
    }

    ///////////////////////////////////////////////////////////////////////////

    struct EditColliderCommandTestParameter
    {
        AZ::TypeId m_shapeType;
        bool m_isTrigger;
        AZ::Vector3 m_position;
        AZ::Quaternion m_rotation;
        std::string m_tag;
        float m_radius;
        float m_height;
        AZ::Vector3 m_dimensions;
    };

    class EditColliderCommandFixture
        : public ActorFixture
        , public ::testing::WithParamInterface<EditColliderCommandTestParameter>
    {
    };

    TEST_P(EditColliderCommandFixture, EditColliderCommandTest)
    {
        AZStd::string result;
        CommandSystem::CommandManager commandManager;
        const EditColliderCommandTestParameter param = GetParam();
        const AZStd::string m_jointName = "l_ankle";
        const PhysicsSetup::ColliderConfigType m_configType = PhysicsSetup::ColliderConfigType::HitDetection;

        // Add collider to the given joint first.
        const AZStd::shared_ptr<PhysicsSetup>& physicsSetup = m_actor->GetPhysicsSetup();
        EXPECT_TRUE(CommandColliderHelpers::AddCollider(m_actor->GetID(), m_jointName, m_configType, param.m_shapeType));
        Physics::CharacterColliderConfiguration* characterColliderConfig = physicsSetup->GetColliderConfigByType(m_configType);
        ASSERT_TRUE(characterColliderConfig != nullptr);
        Physics::CharacterColliderNodeConfiguration* nodeConfig = CommandColliderHelpers::GetCreateNodeConfig(m_actor, m_jointName, *characterColliderConfig, result);
        ASSERT_TRUE(nodeConfig != nullptr);
        EXPECT_EQ(nodeConfig->m_shapes.size(), 1);

        Physics::ShapeConfigurationPair& shapeConfigPair = nodeConfig->m_shapes[0];
        Physics::ColliderConfiguration* colliderConfig = shapeConfigPair.first.get();
        Physics::ShapeConfiguration* shapeConfig = shapeConfigPair.second.get();
        Physics::BoxShapeConfiguration* boxShapeConfig = azdynamic_cast<Physics::BoxShapeConfiguration*>(shapeConfig);
        Physics::CapsuleShapeConfiguration* capsuleShapeConfig = azdynamic_cast<Physics::CapsuleShapeConfiguration*>(shapeConfig);

        // Create the adjust collider command and using the data from the test parameter.
        MCore::Command* orgCommand = CommandSystem::GetCommandManager()->FindCommand(CommandAdjustCollider::s_commandName);
        CommandAdjustCollider* command = aznew CommandAdjustCollider(m_actor->GetID(), m_jointName, m_configType, /*colliderIndex=*/0, orgCommand);
        command->SetOldIsTrigger(colliderConfig->m_isTrigger);
        command->SetIsTrigger(param.m_isTrigger);
        command->SetOldPosition(colliderConfig->m_position);
        command->SetPosition(param.m_position);
        command->SetOldRotation(colliderConfig->m_rotation);
        command->SetRotation(param.m_rotation);
        command->SetOldTag(colliderConfig->m_tag);
        command->SetTag(param.m_tag.c_str());
        if (capsuleShapeConfig)
        {
            command->SetOldRadius(capsuleShapeConfig->m_radius);
            command->SetRadius(param.m_radius);
            command->SetOldHeight(capsuleShapeConfig->m_height);
            command->SetHeight(param.m_height);
        }
        if (boxShapeConfig)
        {
            command->SetOldDimensions(boxShapeConfig->m_dimensions);
            command->SetDimensions(param.m_dimensions);
        }

        // Check execute.
        const AZStd::string serializedBeforeExecute = SerializePhysicsSetup(m_actor);
        EXPECT_TRUE(CommandSystem::GetCommandManager()->ExecuteCommand(command, result));
        const AZStd::string serializedAfterExecute = SerializePhysicsSetup(m_actor);

        EXPECT_EQ(colliderConfig->m_isTrigger, param.m_isTrigger);
        EXPECT_EQ(colliderConfig->m_position, param.m_position);
        EXPECT_EQ(colliderConfig->m_rotation, param.m_rotation);
        EXPECT_EQ(colliderConfig->m_tag, param.m_tag.c_str());
        if (capsuleShapeConfig)
        {
            EXPECT_EQ(capsuleShapeConfig->m_radius, param.m_radius);
            EXPECT_EQ(capsuleShapeConfig->m_height, param.m_height);
        }
        if (boxShapeConfig)
        {
            EXPECT_EQ(boxShapeConfig->m_dimensions, param.m_dimensions);
        }

        // Check undo.
        EXPECT_TRUE(CommandSystem::GetCommandManager()->Undo(result));
        const AZStd::string serializedAfterUndo = SerializePhysicsSetup(m_actor);
        EXPECT_EQ(serializedAfterUndo, serializedBeforeExecute);

        // Check redo.
        EXPECT_TRUE(CommandSystem::GetCommandManager()->Redo(result));
        const AZStd::string serializedAfterRedo = SerializePhysicsSetup(m_actor);
        EXPECT_EQ(serializedAfterRedo, serializedAfterExecute);
    }

    std::vector<EditColliderCommandTestParameter> editColliderCommandTestParameters
    {
        {
            azrtti_typeid<Physics::BoxShapeConfiguration>(),
            false,
            AZ::Vector3::CreateZero(),
            AZ::Quaternion::CreateRotationX(0.0f),
            "Tag1",
            0.0f,
            0.0f,
            AZ::Vector3(1.0f, 2.0f, 3.0f),
        },
        {
            azrtti_typeid<Physics::BoxShapeConfiguration>(),
            true,
            AZ::Vector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
            AZ::Quaternion::CreateRotationX(180.0f),
            "Tag2",
            0.0f,
            0.0f,
            AZ::Vector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
        },
        {
            azrtti_typeid<Physics::BoxShapeConfiguration>(),
            true,
            AZ::Vector3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()),
            AZ::Quaternion::CreateRotationX(180.0f),
            "Tag2",
            0.0f,
            0.0f,
            AZ::Vector3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()),
        },
        {
            azrtti_typeid<Physics::CapsuleShapeConfiguration>(),
            false,
            AZ::Vector3::CreateAxisX(99.0f),
            AZ::Quaternion::CreateRotationX(45.0f),
            "Tag3",
            1.0f,
            3.0f,
            AZ::Vector3::CreateZero(),
        },
        {
            azrtti_typeid<Physics::CapsuleShapeConfiguration>(),
            true,
            AZ::Vector3::CreateAxisY(1.0f),
            AZ::Quaternion::CreateRotationX(-90.0f),
            "",
            FLT_MAX,
            FLT_MAX,
            AZ::Vector3::CreateZero(),
        }
    };

    INSTANTIATE_TEST_CASE_P(EditColliderCommandTests,
        EditColliderCommandFixture,
        ::testing::ValuesIn(editColliderCommandTestParameters)
    );
} // namespace EMotionFX