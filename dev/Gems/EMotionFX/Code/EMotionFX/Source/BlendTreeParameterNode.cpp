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

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/std/sort.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphInstance.h>
#include <EMotionFX/Source/BlendTreeParameterNode.h>
#include <EMotionFX/Source/EventManager.h>


namespace EMotionFX
{
    AZ_CLASS_ALLOCATOR_IMPL(BlendTreeParameterNode, AnimGraphAllocator, 0)

    static const char* sParameterNamesMember = "parameterNames";

    BlendTreeParameterNode::BlendTreeParameterNode()
        : AnimGraphNode()
    {
        // since this node dynamically sets the ports we don't really pre-create anything
        // The reinit function handles that
    }


    BlendTreeParameterNode::~BlendTreeParameterNode()
    {
    }


    void BlendTreeParameterNode::Reinit()
    {
        // Sort the parameter name mask in the way the parameters are stored in the anim graph.
        SortParameterNames(mAnimGraph, m_parameterNames);

        // Iterate through the parameter name mask and find the corresponding cached value parameter indices.
        // This expects the parameter names to be sorted in the way the parameters are stored in the anim graph.
        m_parameterIndices.clear();
        for (const AZStd::string& parameterName : m_parameterNames)
        {
            const AZ::Outcome<size_t> parameterIndex = mAnimGraph->FindValueParameterIndexByName(parameterName);
            // during removal of parameters, we could end up with a parameter that was removed until the node gets the mask updated
            if (parameterIndex.IsSuccess())
            {
                m_parameterIndices.push_back(static_cast<uint32>(parameterIndex.GetValue()));
            }
        }

        RemoveInternalAttributesForAllInstances();

        if (m_parameterIndices.empty())
        {
            // Parameter mask is empty, add ports for all parameters.
            const ValueParameterVector& valueParameters = mAnimGraph->RecursivelyGetValueParameters();
            const uint32 valueParameterCount = static_cast<uint32>(valueParameters.size());
            InitOutputPorts(valueParameterCount);

            for (uint32 i = 0; i < valueParameterCount; ++i)
            {
                const ValueParameter* parameter = valueParameters[i];
                SetOutputPortName(static_cast<uint32>(i), parameter->GetName().c_str());

                mOutputPorts[i].mPortID = i;
                mOutputPorts[i].mCompatibleTypes[0] = parameter->GetType();
                if (GetTypeSupportsFloat(parameter->GetType()))
                {
                    mOutputPorts[i].mCompatibleTypes[1] = MCore::AttributeFloat::TYPE_ID;
                }
            }
        }
        else
        {
            // Parameter mask not empty, only add ports for the parameters in the mask.
            const size_t parameterCount = m_parameterIndices.size();
            InitOutputPorts(static_cast<uint32>(parameterCount));

            for (size_t i = 0; i < parameterCount; ++i)
            {
                const ValueParameter* parameter = mAnimGraph->FindValueParameter(m_parameterIndices[i]);
                SetOutputPortName(static_cast<uint32>(i), parameter->GetName().c_str());

                mOutputPorts[i].mPortID = static_cast<uint32>(i);
                mOutputPorts[i].mCompatibleTypes[0] = parameter->GetType();
                if (GetTypeSupportsFloat(parameter->GetType()))
                {
                    mOutputPorts[i].mCompatibleTypes[1] = MCore::AttributeFloat::TYPE_ID;
                }
            }
        }

        InitInternalAttributesForAllInstances();

        AnimGraphNode::Reinit();
        SyncVisualObject();
    }


    bool BlendTreeParameterNode::InitAfterLoading(AnimGraph* animGraph)
    {
        if (!AnimGraphNode::InitAfterLoading(animGraph))
        {
            return false;
        }

        InitInternalAttributesForAllInstances();

        Reinit();
        return true;
    }


    // get the palette name
    const char* BlendTreeParameterNode::GetPaletteName() const
    {
        return "Parameters";
    }


    // get the palette category
    AnimGraphObject::ECategory BlendTreeParameterNode::GetPaletteCategory() const
    {
        return AnimGraphObject::CATEGORY_SOURCES;
    }


    // the main process method of the final node
    void BlendTreeParameterNode::Update(AnimGraphInstance* animGraphInstance, float timePassedInSeconds)
    {
        MCORE_UNUSED(timePassedInSeconds);

        if (m_parameterIndices.empty())
        {
            // output all anim graph instance parameter values into the output ports
            const uint32 numParameters = static_cast<uint32>(mOutputPorts.size());
            for (uint32 i = 0; i < numParameters; ++i)
            {
                GetOutputValue(animGraphInstance, i)->InitFrom(animGraphInstance->GetParameterValue(i));
            }
        }
        else
        {
            // output only the parameter values that have been selected in the parameter mask
            const size_t parameterCount = m_parameterIndices.size();
            for (size_t i = 0; i < parameterCount; ++i)
            {
                GetOutputValue(animGraphInstance, static_cast<AZ::u32>(i))->InitFrom(animGraphInstance->GetParameterValue(m_parameterIndices[i]));
            }
        }
    }


    // get the parameter index based on the port number
    uint32 BlendTreeParameterNode::GetParameterIndex(uint32 portNr) const
    {
        // check if the parameter mask is empty
        if (m_parameterIndices.empty())
        {
            return portNr;
        }

        // get the mapped parameter index in case the given port is valid
        if (portNr < m_parameterIndices.size())
        {
            return m_parameterIndices[portNr];
        }

        // return failure, the input port is not in range
        return MCORE_INVALIDINDEX32;
    }


    bool BlendTreeParameterNode::GetTypeSupportsFloat(uint32 parameterType)
    {
        switch (parameterType)
        {
        case MCore::AttributeBool::TYPE_ID:
        case MCore::AttributeInt32::TYPE_ID:
            return true;
        default:
            // MCore::AttributeFloat is not considered because float->float conversion is not required
            return false;
        }
    }


    void BlendTreeParameterNode::SortParameterNames(AnimGraph* animGraph, AZStd::vector<AZStd::string>& outParameterNames)
    {
        // Iterate over all value parameters in the anim graph in the order they are stored.
        size_t currentIndex = 0;
        const size_t parameterCount = animGraph->GetNumValueParameters();
        for (size_t i = 0; i < parameterCount; ++i)
        {
            const ValueParameter* parameter = animGraph->FindValueParameter(i);

            // Check if the parameter is part of the parameter mask.
            auto parameterIterator = AZStd::find(outParameterNames.begin(), outParameterNames.end(), parameter->GetName());
            if (parameterIterator != outParameterNames.end())
            {
                // We found the parameter in the parameter mask. Swap the found element position with the current parameter index.
                // Increase the current parameter index as we found another parameter that got inserted.
                AZStd::iter_swap(outParameterNames.begin() + currentIndex, parameterIterator);
                currentIndex++;
            }
        }
    }


    const AZStd::vector<AZ::u32>& BlendTreeParameterNode::GetParameterIndices() const
    {
        return m_parameterIndices;
    }


    AZ::Color BlendTreeParameterNode::GetVisualColor() const
    {
        return AZ::Color(0.59f, 0.59f, 0.59f, 1.0f);
    }


    void BlendTreeParameterNode::AddParameter(const AZStd::string& parameterName)
    {
        m_parameterNames.emplace_back(parameterName);
        Reinit();
    }


    void BlendTreeParameterNode::SetParameters(const AZStd::vector<AZStd::string>& parameterNames)
    {
        m_parameterNames = parameterNames;
        if (mAnimGraph)
        {
            Reinit();
        }
    }


    void BlendTreeParameterNode::SetParameters(const AZStd::string& parameterNamesWithSemicolons)
    {
        AZStd::vector<AZStd::string> parameterNames;
        AzFramework::StringFunc::Tokenize(parameterNamesWithSemicolons.c_str(), parameterNames, ";", false, true);

        SetParameters(parameterNames);
    }


    AZStd::string BlendTreeParameterNode::ConstructParameterNamesString() const
    {
        return ConstructParameterNamesString(m_parameterNames);
    }


    AZStd::string BlendTreeParameterNode::ConstructParameterNamesString(const AZStd::vector<AZStd::string>& parameterNames)
    {
        AZStd::string result;

        for (const AZStd::string& parameterName : parameterNames)
        {
            if (!result.empty())
            {
                result += ';';
            }

            result += parameterName;
        }

        return result;
    }


    AZStd::string BlendTreeParameterNode::ConstructParameterNamesString(const AZStd::vector<AZStd::string>& parameterNames, const AZStd::vector<AZStd::string>& excludedParameterNames)
    {
        AZStd::string result;

        for (const AZStd::string& parameterName : parameterNames)
        {
            if (AZStd::find(excludedParameterNames.begin(), excludedParameterNames.end(), parameterName) == excludedParameterNames.end())
            {
                if (!result.empty())
                {
                    result += ';';
                }

                result += parameterName;
            }
        }

        return result;
    }


    void BlendTreeParameterNode::RemoveParameterByName(const AZStd::string& parameterName)
    {
        m_parameterNames.erase(AZStd::remove(m_parameterNames.begin(), m_parameterNames.end(), parameterName), m_parameterNames.end());
        if (mAnimGraph)
        {
            Reinit();
        }
    }


    void BlendTreeParameterNode::RenameParameterName(const AZStd::string& currentName, const AZStd::string& newName)
    {
        bool somethingChanged = false;
        for (AZStd::string& parameterName : m_parameterNames)
        {
            if (parameterName == currentName)
            {
                somethingChanged = true;
                parameterName = newName;
                break; // we should have only one parameter with this name
            }
        }
        if (somethingChanged && mAnimGraph)
        {
            Reinit();
        }
    }


    void BlendTreeParameterNode::Reflect(AZ::ReflectContext* context)
    {
        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext)
        {
            return;
        }

        serializeContext->Class<BlendTreeParameterNode, AnimGraphNode>()
            ->Version(1)
            ->Field(sParameterNamesMember, &BlendTreeParameterNode::m_parameterNames)
        ;

        AZ::EditContext* editContext = serializeContext->GetEditContext();
        if (!editContext)
        {
            return;
        }

        editContext->Class<BlendTreeParameterNode>("Parameters", "Parameter node attributes")
            ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
            ->Attribute(AZ::Edit::Attributes::AutoExpand, "")
            ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
            ->DataElement(AZ_CRC("AnimGraphParameterMask", 0x67dd0993), &BlendTreeParameterNode::m_parameterNames, "Mask", "The visible and available of the node.")
            ->Attribute(AZ::Edit::Attributes::ContainerCanBeModified, false)
            ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::HideChildren)
        ;
    }

    AZStd::vector<AZStd::string> BlendTreeParameterNode::GetParameters() const
    {
        return m_parameterNames;
    }

    AnimGraph* BlendTreeParameterNode::GetParameterAnimGraph() const
    {
        return GetAnimGraph();
    }

    void BlendTreeParameterNode::ParameterMaskChanged(const AZStd::vector<AZStd::string>& newParameterMask)
    {
        if (newParameterMask.empty())
        {
            AZStd::vector<AZStd::string> newOutputPorts;
            const ValueParameterVector& valueParameters = GetAnimGraph()->RecursivelyGetValueParameters();
            for (const ValueParameter* valueParameter : valueParameters)
            {
                newOutputPorts.emplace_back(valueParameter->GetName());
            }
            GetEventManager().OnOutputPortsChanged(this, newOutputPorts, sParameterNamesMember, newParameterMask);
        }
        else
        {
            AZStd::vector<AZStd::string> newOutputPorts = newParameterMask;
            SortAndRemoveDuplicates(GetAnimGraph(), newOutputPorts);
            GetEventManager().OnOutputPortsChanged(this, newOutputPorts, sParameterNamesMember, newOutputPorts);
        }
    }

    void BlendTreeParameterNode::AddRequiredParameters(AZStd::vector<AZStd::string>& parameterNames) const
    {
        // Add all connected parameters
        for (const AnimGraphNode::Port& port : GetOutputPorts())
        {
            if (port.mConnection)
            {
                parameterNames.emplace_back(port.GetNameString());
            }
        }

        SortAndRemoveDuplicates(GetAnimGraph(), parameterNames);
    }

    void BlendTreeParameterNode::ParameterAdded(size_t newParameterIndex)
    {
        AZ_UNUSED(newParameterIndex);

        AZStd::vector<AZStd::string> newOutputPorts;
        const ValueParameterVector& valueParameters = GetAnimGraph()->RecursivelyGetValueParameters();
        for (const ValueParameter* valueParameter : valueParameters)
        {
            newOutputPorts.emplace_back(valueParameter->GetName());
        }

        if (m_parameterNames.empty())
        {
            // We don't use the parameter mask and show all of them. Pass an empty vector as serialized member value
            // so that the parameter mask won't be adjusted in the callbacks.
            GetEventManager().OnOutputPortsChanged(this, newOutputPorts, sParameterNamesMember, AZStd::vector<AZStd::string>());
        }
        else
        {
            GetEventManager().OnOutputPortsChanged(this, newOutputPorts, sParameterNamesMember, newOutputPorts);
        }
    }

    void BlendTreeParameterNode::ParameterRenamed(const AZStd::string& oldParameterName, const AZStd::string& newParameterName)
    {
        bool somethingChanged = false;
        AZStd::vector<AZStd::string> newOutputPorts = m_parameterNames;
        for (AZStd::string& outputPort : newOutputPorts)
        {
            if (outputPort == oldParameterName)
            {
                outputPort = newParameterName;
                somethingChanged = true;
                break;
            }
        }

        if (somethingChanged)
        {
            GetEventManager().OnOutputPortsChanged(this, newOutputPorts, sParameterNamesMember, newOutputPorts);
        }
    }

    void BlendTreeParameterNode::ParameterOrderChanged(const ValueParameterVector& beforeChange, const ValueParameterVector& afterChange)
    {
        AZ_UNUSED(beforeChange);

        // Check if any of the indices have changed
        // If we are looking at all the parameters, then something changed
        if (m_parameterNames.empty())
        {
            AZStd::vector<AZStd::string> newOutputPorts;
            const ValueParameterVector& valueParameters = GetAnimGraph()->RecursivelyGetValueParameters();
            for (const ValueParameter* valueParameter : valueParameters)
            {
                newOutputPorts.emplace_back(valueParameter->GetName());
            }
            // Keep the member variable as it is (thats why we pass m_parameterNames)
            GetEventManager().OnOutputPortsChanged(this, newOutputPorts, sParameterNamesMember, m_parameterNames);
        }
        else
        {
            // if not, we have to see if for all the parameters, the index is maintained between the before and after
            bool somethingChanged = false;
            const size_t parameterCount = m_parameterNames.size();
            const size_t afterChangeParameterCount = afterChange.size();
            for (size_t valueParameterIndex = 0; valueParameterIndex < parameterCount; ++valueParameterIndex)
            {
                if (valueParameterIndex >= afterChangeParameterCount || afterChange[valueParameterIndex]->GetName() != m_parameterNames[valueParameterIndex])
                {
                    somethingChanged = true;
                    break;
                }
            }
            if (somethingChanged)
            {
                // The list of parameters is the same, we just need to re-sort it
                AZStd::vector<AZStd::string> newParameterNames = m_parameterNames;
                SortAndRemoveDuplicates(GetAnimGraph(), newParameterNames);
                GetEventManager().OnOutputPortsChanged(this, newParameterNames, sParameterNamesMember, newParameterNames);
            }
        }
    }

    void BlendTreeParameterNode::ParameterRemoved(const AZStd::string& oldParameterName)
    {
        AZ_UNUSED(oldParameterName);
        // This may look unnatural, but the method ParameterOrderChanged deals with this as well, we just need to pass an empty before the change
        // and the current parameters after the change
        ParameterOrderChanged(ValueParameterVector(), GetAnimGraph()->RecursivelyGetValueParameters());
    }

} // namespace EMotionFX

