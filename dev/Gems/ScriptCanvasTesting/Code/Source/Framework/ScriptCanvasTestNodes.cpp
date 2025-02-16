/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright EntityRef license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/


#include "ScriptCanvasTestNodes.h"
#include <ScriptCanvas/Core/Core.h>
#include <ScriptCanvas/Core/Graph.h>
#include <ScriptCanvas/Core/SlotConfigurationDefaults.h>

#include <gtest/gtest.h>

namespace TestNodes
{
    //////////////////////////////////////////////////////////////////////////////
    void TestResult::OnInit()
    {        
        Node::AddSlot(ScriptCanvas::CommonSlots::GeneralInSlot());
        Node::AddSlot(ScriptCanvas::CommonSlots::GeneralOutSlot());

        ScriptCanvas::DynamicDataSlotConfiguration slotConfiguration;

        slotConfiguration.m_name = "Value";
        slotConfiguration.m_dynamicDataType = ScriptCanvas::DynamicDataType::Any;
        slotConfiguration.SetConnectionType(ScriptCanvas::ConnectionType::Input);

        AddSlot(slotConfiguration);
    }

    void TestResult::OnInputSignal(const ScriptCanvas::SlotId&)
    {
        auto valueDatum = GetInput(GetSlotId("Value"));
        if (!valueDatum)
        {
            return;
        }

        valueDatum->ToString(m_string);

        // technically, I should remove this, make it an object that holds a string, with an untyped slot, and this could be a local value
        if (!m_string.empty())
        {
            AZ_TracePrintf("Script Canvas", "%s\n", m_string.c_str());
        }

        SignalOutput(GetSlotId(ScriptCanvas::CommonSlots::GeneralOutSlot::GetName()));
    }

    void TestResult::Reflect(AZ::ReflectContext* context)
    {
        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (serializeContext)
        {
            serializeContext->Class<TestResult, Node>()
                ->Version(5)
                ->Field("m_string", &TestResult::m_string)
                ;

            AZ::EditContext* editContext = serializeContext->GetEditContext();
            if (editContext)
            {
                editContext->Class<TestResult>("TestResult", "Development node, will be replaced by a Log node")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Icon, "Editor/Icons/ScriptCanvas/TestResult.png")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TestResult::m_string, "String", "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    void ContractNode::Reflect(AZ::ReflectContext* reflection)
    {
        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(reflection);
        if (serializeContext)
        {
            serializeContext->Class<ContractNode, Node>()
                ->Version(1)
                ;
        }
    }

    void ContractNode::OnInit()
    {
        using namespace ScriptCanvas;

        SlotId inSlotId = Node::AddSlot(ScriptCanvas::CommonSlots::GeneralInSlot());
        Node::AddSlot(ScriptCanvas::CommonSlots::GeneralOutSlot());

        auto func = []() { return aznew DisallowReentrantExecutionContract{}; };
        ContractDescriptor descriptor{ AZStd::move(func) };
        GetSlot(inSlotId)->AddContract(descriptor);

        AddSlot(ScriptCanvas::DataSlotConfiguration(Data::Type::String(), "Set String", ScriptCanvas::ConnectionType::Input));
        AddSlot(ScriptCanvas::DataSlotConfiguration(Data::Type::String(), "Get String", ScriptCanvas::ConnectionType::Output));

        AddSlot(ScriptCanvas::DataSlotConfiguration(Data::Type::Number(), "Set Number", ScriptCanvas::ConnectionType::Input));
        AddSlot(ScriptCanvas::DataSlotConfiguration(Data::Type::Number(), "Get Number", ScriptCanvas::ConnectionType::Output));
    }

    void ContractNode::OnInputSignal(const ScriptCanvas::SlotId&)
    {
        SignalOutput(GetSlotId(ScriptCanvas::CommonSlots::GeneralOutSlot::GetName()));
    }

    //////////////////////////////////////////////////////////////////////////////
    void InfiniteLoopNode::Reflect(AZ::ReflectContext* reflection)
    {
        if (AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(reflection))
        {
            serializeContext->Class<InfiniteLoopNode, Node>()
                ->Version(0)
                ;
        }
    }

    void InfiniteLoopNode::OnInputSignal(const ScriptCanvas::SlotId&)
    {
        SignalOutput(GetSlotId("Before Infinity"));
    }

    void InfiniteLoopNode::OnInit()
    {
        AddSlot(ScriptCanvas::CommonSlots::GeneralInSlot());
        AddSlot(ScriptCanvas::ExecutionSlotConfiguration("Before Infinity", ScriptCanvas::ConnectionType::Output));
        AddSlot(ScriptCanvas::ExecutionSlotConfiguration("After Infinity", ScriptCanvas::ConnectionType::Output));
    }

    //////////////////////////////////////////////////////////////////////////////
    void UnitTestErrorNode::Reflect(AZ::ReflectContext* reflection)
    {
        if (AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(reflection))
        {
            serializeContext->Class<UnitTestErrorNode, Node>()
                ->Version(0)
                ;

        }
    }

    void UnitTestErrorNode::OnInit()
    {
        AddSlot(ScriptCanvas::CommonSlots::GeneralInSlot());
        AddSlot(ScriptCanvas::CommonSlots::GeneralOutSlot());

        ScriptCanvas::DynamicDataSlotConfiguration slotConfiguration;

        slotConfiguration.SetConnectionType(ScriptCanvas::ConnectionType::Output);
        slotConfiguration.m_name = "This";
        slotConfiguration.m_dynamicDataType = ScriptCanvas::DynamicDataType::Any;

        AddSlot(slotConfiguration);
    }

    void UnitTestErrorNode::OnInputSignal(const ScriptCanvas::SlotId&)
    {
        SCRIPTCANVAS_REPORT_ERROR((*this), "Unit test error!");

        SignalOutput(GetSlotId("Out"));
    }

    //////////////////////////////////////////////////////////////////////////////

    void AddNodeWithRemoveSlot::Reflect(AZ::ReflectContext* reflection)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(reflection))
        {
            serializeContext->Class<AddNodeWithRemoveSlot, ScriptCanvas::Node>()
                ->Version(0)
                ->Field("m_dynamicSlotIds", &AddNodeWithRemoveSlot::m_dynamicSlotIds)
                ;

        }
    }

    ScriptCanvas::SlotId AddNodeWithRemoveSlot::AddSlot(AZStd::string_view slotName)
    {
        ScriptCanvas::SlotId addedSlotId = FindSlotIdForDescriptor(slotName, ScriptCanvas::SlotDescriptors::DataIn());
        if (!addedSlotId.IsValid())
        {
            ScriptCanvas::DataSlotConfiguration slotConfiguration;

            slotConfiguration.m_name = slotName;
            slotConfiguration.SetDefaultValue(0.0);
            slotConfiguration.SetConnectionType(ScriptCanvas::ConnectionType::Input);

            addedSlotId = Node::AddSlot(slotConfiguration);

            m_dynamicSlotIds.push_back(addedSlotId);
        }

        return addedSlotId;
    }

    bool AddNodeWithRemoveSlot::RemoveSlot(const ScriptCanvas::SlotId& slotId)
    {
        auto dynamicSlotIt = AZStd::find(m_dynamicSlotIds.begin(), m_dynamicSlotIds.end(), slotId);
        if (dynamicSlotIt != m_dynamicSlotIds.end())
        {
            m_dynamicSlotIds.erase(dynamicSlotIt);
        }
        return Node::RemoveSlot(slotId);
    }

    void AddNodeWithRemoveSlot::OnInputSignal(const ScriptCanvas::SlotId& slotId)
    {
        if (slotId == GetSlotId("In"))
        {
            ScriptCanvas::Data::NumberType result{};
            for (const ScriptCanvas::SlotId& dynamicSlotId : m_dynamicSlotIds)
            {
                if (auto numberInput = GetInput(dynamicSlotId))
                {
                    if (auto argValue = numberInput->GetAs<ScriptCanvas::Data::NumberType>())
                    {
                        result += *argValue;
                    }
                }
            }

            auto resultType = GetSlotDataType(m_resultSlotId);
            EXPECT_TRUE(resultType.IsValid());
            ScriptCanvas::Datum output(result);;
            PushOutput(output, *GetSlot(m_resultSlotId));
            SignalOutput(GetSlotId("Out"));
        }
    }

    void AddNodeWithRemoveSlot::OnInit()
    {
        Node::AddSlot(ScriptCanvas::CommonSlots::GeneralInSlot());
        Node::AddSlot(ScriptCanvas::CommonSlots::GeneralOutSlot());

        for (AZStd::string slotName : {"A", "B", "C"})
        {
            ScriptCanvas::SlotId slotId = FindSlotIdForDescriptor(slotName, ScriptCanvas::SlotDescriptors::DataIn());

            if (!slotId.IsValid())
            {
                ScriptCanvas::DataSlotConfiguration slotConfiguration;

                slotConfiguration.m_name = slotName;
                slotConfiguration.SetDefaultValue(0);
                slotConfiguration.SetConnectionType(ScriptCanvas::ConnectionType::Input);

                m_dynamicSlotIds.push_back(Node::AddSlot(slotConfiguration));
            }
        }

        {
            ScriptCanvas::DataSlotConfiguration slotConfiguration;

            slotConfiguration.m_name = "Result";
            slotConfiguration.SetType(ScriptCanvas::Data::Type::Number());
            slotConfiguration.SetConnectionType(ScriptCanvas::ConnectionType::Output);

            m_resultSlotId = Node::AddSlot(slotConfiguration);
        }
    }

    void StringView::OnInit()
    {
        AddSlot(ScriptCanvas::CommonSlots::GeneralInSlot());
        AddSlot(ScriptCanvas::CommonSlots::GeneralOutSlot());

        {
            ScriptCanvas::DataSlotConfiguration slotConfiguration;

            slotConfiguration.m_name = "View";
            slotConfiguration.m_toolTip = "Input string_view object";
            slotConfiguration.ConfigureDatum(AZStd::move(ScriptCanvas::Datum(ScriptCanvas::Data::Type::String(), ScriptCanvas::Datum::eOriginality::Copy)));
            slotConfiguration.SetConnectionType(ScriptCanvas::ConnectionType::Input);

            AddSlot(slotConfiguration);
        }

        {
            ScriptCanvas::DataSlotConfiguration slotConfiguration;

            slotConfiguration.m_name = "Result";
            slotConfiguration.m_toolTip = "Output string object";
            slotConfiguration.SetAZType<AZStd::string>();
            slotConfiguration.SetConnectionType(ScriptCanvas::ConnectionType::Output);

            m_resultSlotId = AddSlot(slotConfiguration);
        }
    }

    //////////////////////////////////////////////////////////////////////////////

    void StringView::OnInputSignal(const ScriptCanvas::SlotId&)
    {
        auto viewDatum = GetInput(GetSlotId("View"));
        if (!viewDatum)
        {
            return;
        }

        ScriptCanvas::Data::StringType result;
        viewDatum->ToString(result);

        ScriptCanvas::Datum output(result);
        auto resultSlot = GetSlot(m_resultSlotId);
        if (resultSlot)
        {
            PushOutput(output, *resultSlot);
        }
        SignalOutput(GetSlotId("Out"));
    }

    void StringView::Reflect(AZ::ReflectContext* context)
    {
        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (serializeContext)
        {
            serializeContext->Class<StringView, Node>()
                ->Version(0)
                ;
        }
    }

    //////////////////////////////////////////////////////////////////////////////

    void InsertSlotConcatNode::Reflect(AZ::ReflectContext* reflection)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(reflection))
        {
            serializeContext->Class<InsertSlotConcatNode, ScriptCanvas::Node>()
                ->Version(0)
                ;

        }
    }

    ScriptCanvas::SlotId InsertSlotConcatNode::InsertSlot(AZ::s64 index, AZStd::string_view slotName)
    {
        using namespace ScriptCanvas;
        SlotId addedSlotId = FindSlotIdForDescriptor(slotName, ScriptCanvas::SlotDescriptors::DataIn());
        if (!addedSlotId.IsValid())
        {
            DataSlotConfiguration dataConfig;
            dataConfig.m_name = slotName;
            dataConfig.m_toolTip = "";
            dataConfig.SetConnectionType(ScriptCanvas::ConnectionType::Input);
            dataConfig.SetDefaultValue(Data::StringType());

            addedSlotId = Node::InsertSlot(index, dataConfig);
        }

        return addedSlotId;
    }


    void InsertSlotConcatNode::OnInputSignal(const ScriptCanvas::SlotId& slotId)
    {
        if (slotId == GetSlotId(ScriptCanvas::CommonSlots::GeneralInSlot::GetName()))
        {
            ScriptCanvas::Data::StringType result{};

            for (const ScriptCanvas::Slot* concatSlot : GetAllSlotsByDescriptor(ScriptCanvas::SlotDescriptors::DataIn()))
            {
                if (auto inputDatum = GetInput(concatSlot->GetId()))
                {
                    ScriptCanvas::Data::StringType stringArg;
                    if (inputDatum->ToString(stringArg))
                    {
                        result += stringArg;
                    }
                }
            }

            auto resultSlotId = GetSlotId("Result");
            auto resultType = GetSlotDataType(resultSlotId);
            EXPECT_TRUE(resultType.IsValid());
            if (auto resultSlot = GetSlot(resultSlotId))
            {
                PushOutput(ScriptCanvas::Datum(result), *resultSlot);
            }
            SignalOutput(GetSlotId("Out"));
        }
    }

    void InsertSlotConcatNode::OnInit()
    {
        Node::AddSlot(ScriptCanvas::CommonSlots::GeneralInSlot());
        Node::AddSlot(ScriptCanvas::CommonSlots::GeneralOutSlot());
        Node::AddSlot(ScriptCanvas::DataSlotConfiguration(ScriptCanvas::Data::Type::String(), "Result", ScriptCanvas::ConnectionType::Output));
    }

    /////////////////////////////
    // ConfigurableUnitTestNode
    /////////////////////////////

    void ConfigurableUnitTestNode::Reflect(AZ::ReflectContext* reflection)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(reflection))
        {
            serializeContext->Class<ConfigurableUnitTestNode, ScriptCanvas::Node>()
                ->Version(0)
                ;
        }
    }

    ScriptCanvas::Slot* ConfigurableUnitTestNode::AddTestingSlot(ScriptCanvas::SlotConfiguration& slotConfiguration)
    {
        ScriptCanvas::SlotId slotId = AddSlot(slotConfiguration);

        return GetSlot(slotId);
    }

    ScriptCanvas::Datum* ConfigurableUnitTestNode::FindDatum(const ScriptCanvas::SlotId& slotId)
    {
        return ModInput(slotId);
    }

    void ConfigurableUnitTestNode::TestClearDisplayType(const AZ::Crc32& dynamicGroup)
    {
        ClearDisplayType(dynamicGroup);
    }

    void ConfigurableUnitTestNode::TestSetDisplayType(const AZ::Crc32& dynamicGroup, const ScriptCanvas::Data::Type& dataType)
    {
        SetDisplayType(dynamicGroup, dataType);
    }

    bool ConfigurableUnitTestNode::TestHasConcreteDisplayType(const AZ::Crc32& dynamicGroup) const
    {
        return HasConcreteDisplayType(dynamicGroup);
    }

    bool ConfigurableUnitTestNode::TestIsSlotConnectedToConcreteDisplayType(const ScriptCanvas::Slot& slot, ExploredDynamicGroupCache& exploredGroupCache) const
    {
        return IsSlotConnectedToConcreteDisplayType(slot, exploredGroupCache);
    }    
}