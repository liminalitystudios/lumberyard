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

#include <AzTest/AzTest.h>
#include <AzCore/UnitTest/TestTypes.h>

#include <EMotionFX/CommandSystem/Source/CommandSystemConfig.h>
#include <AzCore/std/containers/vector.h>
#include <MCore/Source/Command.h>
#include <MCore/Source/StringIdPool.h>

#include <EMotionFX/CommandSystem/Source/AnimGraphConnectionCommands.h>
#include <EMotionFX/CommandSystem/Source/CommandManager.h>

#include <AzCore/std/sort.h>
#include <EMotionFX/Source/ActorInstance.h>
#include <EMotionFX/Source/ActorManager.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphInstance.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/AnimGraphNode.h>
#include <EMotionFX/Source/AnimGraphParameterCondition.h>
#include <EMotionFX/Source/AnimGraphTagCondition.h>
#include <EMotionFX/Source/AnimGraphStateMachine.h>
#include <EMotionFX/Source/BlendTreeParameterNode.h>
#include <EMotionFX/Source/Parameter/Parameter.h>
#include <EMotionFX/Source/Parameter/ParameterFactory.h>
#include <EMotionFX/Source/Parameter/ValueParameter.h>
#include <MCore/Source/AttributeFactory.h>
#include <MCore/Source/ReflectionSerializer.h>

namespace AnimGraphParameterCommandsTests
{
    namespace AZStd
    {
        using namespace ::AZStd;
    } // namespace AZStd

    namespace AZ
    {
        using namespace ::AZ;
    } // namespace AZ

    namespace MCore
    {
        using ::MCore::Command;
        using ::MCore::CommandGroup;
        using ::MCore::CommandLine;
        using ::MCore::CommandSyntax;
        using ::MCore::MCORE_MEMCATEGORY_COMMANDSYSTEM;
        using ::MCore::MCORE_DEFAULT_ALIGNMENT;
        using ::MCore::AlignedAllocate;
        using ::MCore::AlignedFree;
        using ::MCore::GetStringIdPool;
        using ::MCore::ReflectionSerializer;
        using ::MCore::LogWarning;
        using ::MCore::Array;
    } // namespace MCore

    namespace EMotionFX
    {
        // Import real implementations that are not mocked
        using ::EMotionFX::AnimGraphNodeId;
        using ::EMotionFX::AnimGraphConnectionId;

        // Forward declare types that will be mocked
        class Actor;
        class AnimGraph;
        class AnimGraphInstance;
        class AnimGraphNode;
        class AnimGraphObject;
        class AnimGraphObjectData;
        class AnimGraphStateTransition;
        class AnimGraphTransitionCondition;
        class GroupParameter;
        class Parameter;
        class ParameterFactory;
        class ValueParameter;
        class BlendTreeConnection;

        using ParameterVector = AZStd::vector<Parameter*>;
        using ValueParameterVector = AZStd::vector<ValueParameter*>;
        using GroupParameterVector = AZStd::vector<GroupParameter*>;
    } // namespace EMotionFX

    namespace CommandSystem
    {
        using ::CommandSystem::SelectionList;

        void DeleteNodeConnection(MCore::CommandGroup* commandGroup, const EMotionFX::AnimGraphNode* targetNode, const EMotionFX::BlendTreeConnection* connection) {}
    } // namespace CommandSystem

#include <Tests/Mocks/ActorManager.h>
#include <Tests/Mocks/AnimGraph.h>
#include <Tests/Mocks/AnimGraphInstance.h>
#include <Tests/Mocks/AnimGraphManager.h>
#include <Tests/Mocks/AnimGraphObject.h>
#include <Tests/Mocks/AnimGraphObjectData.h>
#include <Tests/Mocks/AnimGraphNode.h>
#include <Tests/Mocks/AnimGraphStateTransition.h>
#include <Tests/Mocks/EMotionFXManager.h>
#include <Tests/Mocks/ObjectAffectedByParameterChanges.h>
#include <Tests/Mocks/Parameter.h>
#include <Tests/Mocks/ParameterFactory.h>
#include <Tests/Mocks/GroupParameter.h>
#include <Tests/Mocks/ValueParameter.h>
#include <Tests/Mocks/CommandManager.h>
#include <Tests/Mocks/CommandSystemCommandManager.h>
#include <Tests/Mocks/BlendTreeParameterNode.h>

#include <EMotionFX/CommandSystem/Source/AnimGraphParameterCommands.cpp>

    inline testing::PolymorphicMatcher<testing::internal::StrEqualityMatcher<AZStd::string> >
        StrEq(const AZStd::string& str) {
        return ::testing::MakePolymorphicMatcher(testing::internal::StrEqualityMatcher<AZStd::string>(
          str, true, true));
    }

    class TestParameter
        : public EMotionFX::ValueParameter
    {
    public:
        AZ_RTTI(TestParameter, "{6C91B0BE-EFCF-4270-A356-28B1C4612CCE}", EMotionFX::ValueParameter);
    };

    class AnimGraphParameterCommandsFixture
        : public UnitTest::AllocatorsTestFixture
    {
    public:
        void SetUp() override
        {
            UnitTest::AllocatorsTestFixture::SetUp();
            ::MCore::Initializer::Init(); // create the MCoreSystem object for MCore containers
        }

        void TearDown() override
        {
            ::MCore::Initializer::Shutdown();
            UnitTest::AllocatorsTestFixture::TearDown();
        }
    };

    TEST_F(AnimGraphParameterCommandsFixture, CreatingAParameterUpdatesObjectsAfterParameterIsAddedToInstances)
    {
        EMotionFX::EMotionFXManager& manager = EMotionFX::GetEMotionFX();
        EMotionFX::AnimGraphManager animGraphManager;
        EMotionFX::AnimGraph animGraph;
        EMotionFX::AnimGraphInstance animGraphInstance0;
        EMotionFX::BlendTreeParameterNode parameterNode;
        TestParameter parameter;

        EXPECT_CALL(manager, GetAnimGraphManager())
            .WillRepeatedly(testing::Return(&animGraphManager));

        EXPECT_CALL(animGraph, GetID())
            .WillRepeatedly(testing::Return(0));
        EXPECT_CALL(animGraph, GetNumParameters())
            .WillOnce(testing::Return(0));
        EXPECT_CALL(animGraph, AddParameter(&parameter, nullptr))
            .WillOnce(testing::Return(true));
        EXPECT_CALL(animGraph, FindParameterByName(StrEq("testParameter")))
            .WillOnce(testing::Return(nullptr));
        EXPECT_CALL(animGraph, FindParameterIndex(&parameter))
            .WillOnce(testing::Return(AZ::Success<size_t>(0)));
        EXPECT_CALL(animGraph, FindParameter(0))
            .WillOnce(testing::Return(&parameter));
        EXPECT_CALL(animGraph, FindValueParameterIndex(&parameter))
            .WillOnce(testing::Return(AZ::Success<size_t>(0)));
        EXPECT_CALL(animGraph, GetNumAnimGraphInstances())
            .WillRepeatedly(testing::Return(1));
        EXPECT_CALL(animGraph, GetAnimGraphInstance(0))
            .WillRepeatedly(testing::Return(&animGraphInstance0));
        AZStd::vector<EMotionFX::AnimGraphObject*> objectsAffectedByParameterChanges {&parameterNode};
        EXPECT_CALL(animGraph, RecursiveCollectObjectsOfType(azrtti_typeid<EMotionFX::ObjectAffectedByParameterChanges>(), testing::_))
            .WillRepeatedly(testing::SetArgReferee<1>(objectsAffectedByParameterChanges));
        EXPECT_CALL(animGraph, GetDirtyFlag())
            .WillOnce(testing::Return(false));
        EXPECT_CALL(animGraph, SetDirtyFlag(true));

        EXPECT_CALL(animGraphManager, FindAnimGraphByID(animGraph.GetID()))
            .WillRepeatedly(testing::Return(&animGraph));
        EXPECT_CALL(animGraphManager, RecursiveCollectObjectsAffectedBy(&animGraph, testing::_));

        EXPECT_CALL(*EMotionFX::Internal::GetParameterFactory(), CreateImpl(azrtti_typeid<TestParameter>()))
            .WillOnce(testing::Return(&parameter));

        EXPECT_CALL(parameter, SetName(StrEq("testParameter")));
        EXPECT_CALL(parameter, SetDescription(StrEq("The Test Parameter Description")));

        {
            testing::InSequence sequence;
            // AnimGraphInstance::InsertParameterValue must be called before
            // AnimGraphNode::ParameterAdded
            EXPECT_CALL(animGraphInstance0, InsertParameterValue(0));
            EXPECT_CALL(parameterNode, ParameterAdded(0));
        }

        MCore::CommandLine parameters(R"(-name testParameter -animGraphID 0 -type "{6C91B0BE-EFCF-4270-A356-28B1C4612CCE}" -description "The Test Parameter Description")");

        AZStd::string outResult;
        CommandSystem::CommandAnimGraphCreateParameter command;
        EXPECT_TRUE(command.Execute(parameters, outResult)) << outResult.c_str();

    }
} // namespace AnimGraphParameterCommandsTests
