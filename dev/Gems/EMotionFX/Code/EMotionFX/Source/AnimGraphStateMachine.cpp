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

#include <AzCore/Math/MathUtils.h>
#include <AzCore/RTTI/RTTI.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <EMotionFX/Source/ActorInstance.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphAttributeTypes.h>
#include <EMotionFX/Source/AnimGraphEntryNode.h>
#include <EMotionFX/Source/AnimGraphExitNode.h>
#include <EMotionFX/Source/AnimGraphInstance.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/AnimGraphNodeGroup.h>
#include <EMotionFX/Source/AnimGraphStateMachine.h>
#include <EMotionFX/Source/AnimGraphStateTransition.h>
#include <EMotionFX/Source/AnimGraphTransitionCondition.h>
#include <EMotionFX/Source/AnimGraphTriggerAction.h>
#include <EMotionFX/Source/BlendTree.h>
#include <EMotionFX/Source/EMotionFXConfig.h>
#include <EMotionFX/Source/EventManager.h>

namespace EMotionFX
{
    AZ_CLASS_ALLOCATOR_IMPL(AnimGraphStateMachine, AnimGraphAllocator, 0)
    AZ_CLASS_ALLOCATOR_IMPL(AnimGraphStateMachine::UniqueData, AnimGraphObjectUniqueDataAllocator, 0)

    AZ::u32 AnimGraphStateMachine::s_maxNumPasses = 10;

    AnimGraphStateMachine::AnimGraphStateMachine()
        : AnimGraphNode()
        , mEntryState(nullptr)
        , mEntryStateNodeNr(MCORE_INVALIDINDEX32)
        , m_entryStateId(AnimGraphNodeId::InvalidId)
        , m_alwaysStartInEntryState(true)
    {
        InitOutputPorts(1);
        SetupOutputPortAsPose("Output Pose", OUTPUTPORT_POSE, PORTID_OUTPUT_POSE);
    }

    AnimGraphStateMachine::~AnimGraphStateMachine()
    {
        // NOTE: base class automatically removes all child nodes (states)
        RemoveAllTransitions();
    }

    void AnimGraphStateMachine::RecursiveReinit()
    {
        // Re-initialize all child nodes and connections
        AnimGraphNode::RecursiveReinit();

        for (AnimGraphStateTransition* transition : mTransitions)
        {
            transition->RecursiveReinit();
        }
    }

    bool AnimGraphStateMachine::InitAfterLoading(AnimGraph* animGraph)
    {
        if (!AnimGraphNode::InitAfterLoading(animGraph))
        {
            return false;
        }

        for (AnimGraphStateTransition* transition : mTransitions)
        {
            transition->InitAfterLoading(animGraph);
        }

        // Needs to be called after anim graph is set (will iterate through anim graph instances).
        InitInternalAttributesForAllInstances();

        Reinit();
        return true;
    }

    void AnimGraphStateMachine::RemoveAllTransitions()
    {
        for (AnimGraphStateTransition* transition : mTransitions)
        {
            delete transition;
        }

        mTransitions.clear();
    }

    void AnimGraphStateMachine::Output(AnimGraphInstance* animGraphInstance)
    {
        ActorInstance* actorInstance = animGraphInstance->GetActorInstance();
        RequestPoses(animGraphInstance);
        AnimGraphPose* outputPose = GetOutputPose(animGraphInstance, OUTPUTPORT_POSE)->GetValue();

        if (mDisabled)
        {
            // Output bind pose in case state machine is disabled.
            outputPose->InitFromBindPose(actorInstance);
            return;
        }

        if (GetEMotionFX().GetIsInEditorMode())
        {
            SetHasError(animGraphInstance, false);
        }

        UniqueData* uniqueData = static_cast<UniqueData*>(FindUniqueNodeData(animGraphInstance));
        const bool isTransitioning = IsTransitioning(uniqueData);

        // Single active state, no active transition.
        if (!isTransitioning && uniqueData->mCurrentState)
        {
            uniqueData->mCurrentState->PerformOutput(animGraphInstance);
            *outputPose = *uniqueData->mCurrentState->GetMainOutputPose(animGraphInstance);
        }
        // One or more transitions active.
        else if (isTransitioning)
        {
            // Output all active states (current state as well as target states).
            const AZStd::vector<AnimGraphNode*>& activeStates = uniqueData->GetActiveStates();
            for (AnimGraphNode* activeState : activeStates)
            {
                OutputIncomingNode(animGraphInstance, activeState);
            }

            // Initialize output pose by pose of the oldest transition's source node.
            const size_t numActiveTransitions = uniqueData->m_activeTransitions.size();
            const AnimGraphStateTransition* startTransition = uniqueData->m_activeTransitions[numActiveTransitions - 1];
            const AnimGraphNode* startSourceNode = startTransition->GetSourceNode(animGraphInstance);
            const AnimGraphPose* startPose = startSourceNode ? startSourceNode->GetMainOutputPose(animGraphInstance) : nullptr;
            if (startPose)
            {
                *outputPose = *startPose;

                // Iterate through the transition stack from the oldest to the newest active transition.
                for (size_t i = 0; i < numActiveTransitions; ++i)
                {
                    AnimGraphStateTransition* activeTransition = uniqueData->m_activeTransitions[numActiveTransitions - i - 1];

                    const AnimGraphNode* targetNode = activeTransition->GetTargetNode();
                    const AnimGraphPose* targetPose = targetNode->GetMainOutputPose(animGraphInstance);
                    AZ_Assert(targetPose, "Transition target node has to provide a valid main output pose.");

                    // Processes the transition, calculate and blend the in between pose and use it as output for the state machine.
                    activeTransition->CalcTransitionOutput(animGraphInstance, *outputPose, *targetPose, outputPose);
                }
            }
            else
            {
                outputPose->InitFromBindPose(actorInstance);
            }
        }
        // No state active, output bind pose.
        else
        {
            outputPose->InitFromBindPose(actorInstance);
        }

        // Decrease pose ref counts for all states where we increased it.
        uniqueData->DecreasePoseRefCounts(animGraphInstance);

        if (GetEMotionFX().GetIsInEditorMode() && GetCanVisualize(animGraphInstance))
        {
            actorInstance->DrawSkeleton(outputPose->GetPose(), mVisualizeColor);
        }
    }

    // check all outgoing transitions for the given node for if they are ready
    void AnimGraphStateMachine::CheckConditions(AnimGraphNode* sourceNode, AnimGraphInstance* animGraphInstance, UniqueData* uniqueData, bool allowTransition)
    {
        // skip calculations in case the source node is not valid
        if (!sourceNode)
        {
            return;
        }

        // check if there is a state we can transition into, based on the transition conditions
        // variables that will hold the prioritized transition information
        AZ::s32 highestPriority = -1;
        AnimGraphStateTransition* prioritizedTransition = nullptr;
        bool requestInterruption = false;
        const bool isTransitioning = IsTransitioning(animGraphInstance);
        AnimGraphStateTransition* latestActiveTransition = GetLatestActiveTransition(uniqueData);
        const AnimGraphNodeId sourceNodeId = sourceNode->GetId();

        for (AnimGraphStateTransition* curTransition : mTransitions)
        {
            if (curTransition->GetIsDisabled())
            {
                continue;
            }

            const bool isWildcardTransition = curTransition->GetIsWildcardTransition();
            AnimGraphNode* transitionTargetNode = curTransition->GetTargetNode();

            // skip transitions that don't start from our given start node
            if (!isWildcardTransition && curTransition->GetSourceNode() != sourceNode)
            {
                continue;
            }
            // Wildcard transitions: The state filter holds the allowed states from which we can enter the wildcard transition.
            // An empty filter means transitioning is allowed from any other state. In case the wildcard transition has a filter specified and
            // the given source node is not part of the selection, we'll skip it and don't allow to transition.
            if (isWildcardTransition && !curTransition->CanWildcardTransitionFrom(sourceNode))
            {
                continue;
            }

            // check if the transition evaluates as valid (if the conditions evaluate to true)
            if (curTransition->CheckIfIsReady(animGraphInstance))
            {
                // compare the priority values and overwrite it in case it is more important
                const int32 transitionPriority = curTransition->GetPriority();
                if (transitionPriority > highestPriority)
                {
                    if (isTransitioning)
                    {
                        // The state machine is transitioning already, check if we can interrupt.
                        bool allowInterruption = false;

                        // Case 1: Interrupt by another transition. (Multiple active and blended together transitions)
                        if (curTransition->GetCanInterruptOtherTransitions() && // Can the transition candidate interrupt other/the active transition at all?
                            latestActiveTransition->CanBeInterruptedBy(curTransition, animGraphInstance) && // Can the latest active transition be interrupted by the transition candidate?
                            !IsTransitionActive(curTransition, animGraphInstance)) // Only allow interruption in case the transition candidate is no active yet.
                        {
                            allowInterruption = true;
                        }

                        // Case 2: Self interruption (rewinding the transition without blending)
                        if (latestActiveTransition == curTransition &&
                            curTransition->GetCanInterruptItself())
                        {
                            allowInterruption = true;
                        }

                        if (allowInterruption)
                        {
                            highestPriority = transitionPriority;
                            prioritizedTransition = curTransition;
                            requestInterruption = true;
                        }
                    }
                    else
                    {
                        // skip transitions that end in the currently active state
                        if (sourceNode != transitionTargetNode)
                        {
                            // in case we're not transitioning at the moment, just do normal
                            highestPriority = transitionPriority;
                            prioritizedTransition = curTransition;
                        }
                    }
                }
            }
        }

        // check if a transition condition fired and adjust the current state to the target node of the transition with the highest priority
        if (prioritizedTransition && allowTransition)
        {
            // Special case handling for self-interruption
            if (requestInterruption && latestActiveTransition == prioritizedTransition)
            {
                AnimGraphStateTransition* transition = latestActiveTransition;
                AnimGraphNode* transitionSourceNode = transition->GetSourceNode(animGraphInstance);
                AnimGraphNode* transitionTargetNode = transition->GetTargetNode();
                EventManager& eventManager = GetEventManager();

                transitionTargetNode->OnStateExit(animGraphInstance, transitionSourceNode, transition);
                transitionTargetNode->OnStateEnd(animGraphInstance, transitionSourceNode, transition);
                eventManager.OnStateExit(animGraphInstance, transitionTargetNode);
                eventManager.OnStateEnd(animGraphInstance, transitionTargetNode);

                transition->OnEndTransition(animGraphInstance);
                eventManager.OnEndTransition(animGraphInstance, transition);

                transitionSourceNode->Rewind(animGraphInstance);

                transitionSourceNode->OnStateEntering(animGraphInstance, transitionTargetNode, transition);
                transitionSourceNode->OnStateEnter(animGraphInstance, transitionTargetNode, transition);
                eventManager.OnStateEntering(animGraphInstance, transitionSourceNode);
                eventManager.OnStateEnter(animGraphInstance, transitionSourceNode);

                transition->ResetConditions(animGraphInstance);
                transition->OnStartTransition(animGraphInstance);
            }
            else
            {
                StartTransition(animGraphInstance, uniqueData, prioritizedTransition, /*calledFromWithinUpdate*/ true);
            }
        }
    }

    // update conditions for all transitions that start from the given state and all wild card transitions
    void AnimGraphStateMachine::UpdateConditions(AnimGraphInstance* animGraphInstance, AnimGraphNode* animGraphNode, float timePassedInSeconds)
    {
        if (!animGraphNode)
        {
            return;
        }
        const bool isTransitioning = IsTransitioning(animGraphInstance);

        for (const AnimGraphStateTransition* transition : mTransitions)
        {
            // get the current transition and skip it directly if in case it is disabled
            if (transition->GetIsDisabled())
            {
                continue;
            }

            // skip transitions that don't start from our given current node
            if (!transition->GetIsWildcardTransition() &&
                transition->GetSourceNode() != animGraphNode)
            {
                continue;
            }

            // skip transitions that are not made for interrupting when we are currently transitioning
            if (isTransitioning &&
                !transition->GetCanInterruptOtherTransitions())
            {
                continue;
            }

            const size_t numConditions = transition->GetNumConditions();
            for (size_t j = 0; j < numConditions; ++j)
            {
                AnimGraphTransitionCondition* condition = transition->GetCondition(j);
                condition->Update(animGraphInstance, timePassedInSeconds);
            }
        }
    }

    void AnimGraphStateMachine::StartTransition(AnimGraphInstance* animGraphInstance, UniqueData* uniqueData, AnimGraphStateTransition* transition, bool calledFromWithinUpdate)
    {
        AnimGraphNode* sourceNode = transition->GetSourceNode();
        AnimGraphNode* targetNode = transition->GetTargetNode();

        bool targetStateNeedsUpdate = false;
        {
            const AZStd::vector<AnimGraphNode*>& activeStates = GetActiveStates(animGraphInstance);
            if (AZStd::find(activeStates.begin(), activeStates.end(), targetNode) == activeStates.end())
            {
                targetStateNeedsUpdate = true;
            }
        }

        // Update the source node for the transition instance in case we're dealing with a wildcard transition.
        if (transition->GetIsWildcardTransition())
        {
            sourceNode = uniqueData->mCurrentState;
            transition->SetSourceNode(animGraphInstance, sourceNode);
        }

        // Rewind the target state and reset conditions of all outgoing transitions.
        if (targetNode != sourceNode)
        {
            targetNode->Rewind(animGraphInstance);

            ResetOutgoingTransitionConditions(animGraphInstance, targetNode);

            targetNode->OnStateEntering(animGraphInstance, sourceNode, transition);
            GetEventManager().OnStateEntering(animGraphInstance, targetNode);
        }

        transition->OnStartTransition(animGraphInstance);
        GetEventManager().OnStartTransition(animGraphInstance, transition);

        if (targetNode != sourceNode)
        {
            sourceNode->OnStateExit(animGraphInstance, targetNode, transition);
            GetEventManager().OnStateExit(animGraphInstance, sourceNode);
        }

        PushTransitionStack(uniqueData, transition);

        transition->Update(animGraphInstance, 0.0f);

        if (calledFromWithinUpdate &&
            targetNode &&
            targetStateNeedsUpdate)
        {
            uniqueData->IncreasePoseRefCountForNode(targetNode, animGraphInstance);
            uniqueData->IncreaseDataRefCountForNode(targetNode, animGraphInstance);

            targetNode->PerformUpdate(animGraphInstance, 0.0f);
        }

        // Enable the exit state reached flag when are entering an exit state or if the current state is an exit state.
        UpdateExitStateReachedFlag(animGraphInstance, uniqueData);
    }

    void AnimGraphStateMachine::EndTransition(AnimGraphStateTransition* transition, AnimGraphInstance* animGraphInstance, UniqueData* uniqueData)
    {
        AZ_Assert(transition, "Transition has to be valid in order to end it.");
        AnimGraphNode* targetState = transition->GetTargetNode();
        AnimGraphStateTransition* latestActiveTransition = GetLatestActiveTransition(uniqueData);
        const bool isLatestTransition = (latestActiveTransition == transition);
        const bool isDone = transition->GetIsDone(animGraphInstance);
        EventManager& eventManager = GetEventManager();

        // End transition and emit transition events.
        transition->OnEndTransition(animGraphInstance);
        eventManager.OnEndTransition(animGraphInstance, transition);

        // Reset the conditions of the transition that has just ended.
        transition->ResetConditions(animGraphInstance);

        targetState->OnStateEnter(animGraphInstance, uniqueData->mCurrentState, transition);
        eventManager.OnStateEnter(animGraphInstance, targetState);

        // Ending latest active transition.
        if (isLatestTransition)
        {
            // Emit end state events and adjust the previous and the active states in case the latest active transition is ending.
            // In other cases we're not leaving the current state yet as it is still active as a source state from another active transition.
            uniqueData->mCurrentState->OnStateEnd(animGraphInstance, targetState, transition);
            eventManager.OnStateEnd(animGraphInstance, uniqueData->mCurrentState);

            uniqueData->mPreviousState = uniqueData->mCurrentState;
            uniqueData->mCurrentState = targetState;
        }
        // Ending any interrupted transition on the transition stack that ended transitioning.
        else if (transition->GetIsDone(animGraphInstance))
        {
            targetState->OnStateEnd(animGraphInstance, targetState, latestActiveTransition);
            eventManager.OnStateEnd(animGraphInstance, targetState);
        }

        AZStd::vector<AnimGraphStateTransition*>& activeTransitions = uniqueData->m_activeTransitions;
        uniqueData->m_activeTransitions.erase(AZStd::remove(activeTransitions.begin(), activeTransitions.end(), transition));
    }

    void AnimGraphStateMachine::EndAllActiveTransitions(AnimGraphInstance* animGraphInstance)
    {
        UniqueData* uniqueData = static_cast<UniqueData*>(FindUniqueNodeData(animGraphInstance));
        EndAllActiveTransitions(animGraphInstance, uniqueData);
    }

    void AnimGraphStateMachine::EndAllActiveTransitions(AnimGraphInstance* animGraphInstance, UniqueData* uniqueData)
    {
        AZStd::vector<AnimGraphStateTransition*>& activeTransitions = uniqueData->m_activeTransitions;

        // End active transitions back to front.
        while (!activeTransitions.empty())
        {
            AnimGraphStateTransition* transition = uniqueData->m_activeTransitions[uniqueData->m_activeTransitions.size() - 1];
            EndTransition(transition, animGraphInstance, uniqueData);
        }
    }

    void AnimGraphStateMachine::Update(AnimGraphInstance* animGraphInstance, float timePassedInSeconds)
    {
        UniqueData* uniqueData = static_cast<UniqueData*>(FindUniqueNodeData(animGraphInstance));

        // Update all currently active transitions.
        for (AnimGraphStateTransition* transition : uniqueData->m_activeTransitions)
        {
            transition->Update(animGraphInstance, timePassedInSeconds);
        }

        // Update all currently active states and increase ref counts for them.
        {
            const AZStd::vector<EMotionFX::AnimGraphNode*>& activeStates = GetActiveStates(animGraphInstance);
            for (AnimGraphNode* activeState : activeStates)
            {
                uniqueData->IncreasePoseRefCountForNode(activeState, animGraphInstance);
                uniqueData->IncreaseDataRefCountForNode(activeState, animGraphInstance);
                activeState->PerformUpdate(animGraphInstance, timePassedInSeconds);
            }
        }

        // Update the conditions and trigger the right transition based on the conditions and priority levels etc.
        UpdateConditions(animGraphInstance, uniqueData->mCurrentState, timePassedInSeconds);
        CheckConditions(uniqueData->mCurrentState, animGraphInstance, uniqueData, /*calledFromWithinUpdate*/ true);

        // Check if our latest active transition is already done, end it and check for further transition candidates.
        // This can happen in the same frame directly after starting a new transition in case the blend time is 0.0.
        AZ::u32 numPasses = 0;
        while (GetLatestActiveTransition(uniqueData) && IsLatestActiveTransitionDone(animGraphInstance, uniqueData))
        {
            // End all transitions on the stack back to front
            EndAllActiveTransitions(animGraphInstance, uniqueData);

            UpdateConditions(animGraphInstance, uniqueData->mCurrentState, 0.0f);
            CheckConditions(uniqueData->mCurrentState, animGraphInstance, uniqueData, /*calledFromWithinUpdate*/ true);

            if (numPasses >= s_maxNumPasses)
            {
                AZ_Warning("EMotionFX", false, "%d state switches happened within a single frame. "
                    "This either means that the time delta of the update is too large or the blend times for several transitions are short and conditions are all set to trigger. "
                    "Please check the anim graph for transitions with small blend times and why they could transit so fastly or why the time delta is significantly bigger than the blend times. "
                    "Alternatively, you can increase the number of allowed passes within a frame by changing s_maxNumPasses (not recommended).", s_maxNumPasses);
                break;
            }
            numPasses++;
        }

        // Enable the exit state reached flag when are entering an exit state or if the current state is an exit state.
        UpdateExitStateReachedFlag(animGraphInstance, uniqueData);

        // Perform play speed synchronization when transitioning.
        if (uniqueData->mCurrentState)
        {
            uniqueData->Init(animGraphInstance, uniqueData->mCurrentState);

            if (IsTransitioning(uniqueData))
            {
                float newPlaySpeed = 1.0f;
                float newFactor = 1.0f;

                const size_t numActiveTransitions = uniqueData->m_activeTransitions.size();
                for (size_t i = 0; i < numActiveTransitions; ++i)
                {
                    const AnimGraphStateTransition* activeTransition = uniqueData->m_activeTransitions[numActiveTransitions - i - 1];
                    const AnimGraphNode* sourceState = activeTransition->GetSourceNode(animGraphInstance);
                    if (sourceState)
                    {
                        const AnimGraphNode* targetState = activeTransition->GetTargetNode();
                        const float transitionWeight = activeTransition->GetBlendWeight(animGraphInstance);
                        const ESyncMode syncMode = activeTransition->GetSyncMode();

                        // Calculate the playspeed and factors based on the source and the target states for the given transition.
                        float masterFactor;
                        float servantFactor;
                        float playSpeed;
                        AnimGraphNode::CalcSyncFactors(animGraphInstance, sourceState, targetState, syncMode, transitionWeight, &masterFactor, &servantFactor, &playSpeed);

                        // Sync to the shared source state.
                        if (i == 0)
                        {
                            // Store the new interpolated playspeed as well as the interpolated duration ratio (masterFactor)
                            // for the oldest transition on the stack. This is the transition where the first interruption happened.
                            newPlaySpeed = playSpeed;
                            newFactor = masterFactor;
                        }
                        else
                        {
                            // const AnimGraphNodeData* masterUniqueData = sourceState->FindUniqueNodeData(animGraphInstance);
                            const AnimGraphNodeData* servantUniqueData = targetState->FindUniqueNodeData(animGraphInstance);

                            // Interpolate the in-between factor from the previous iteration with the interpolated factor from the given transition.
                            newFactor = AZ::Lerp(newFactor, masterFactor, transitionWeight);

                            // Interpolate the in-between factor from the previous iteration with the target state's playspeed based on the weight
                            // of the given transition. As we're syncing to the source node, the target node acts as servant.
                            newPlaySpeed = AZ::Lerp(newPlaySpeed, servantUniqueData->GetPlaySpeed(), transitionWeight);
                        }
                    }
                }

                uniqueData->SetPlaySpeed(newPlaySpeed * newFactor);
            }
        }
        else
        {
            uniqueData->Clear();
        }
    }

    void AnimGraphStateMachine::UpdateExitStateReachedFlag(AnimGraphInstance* animGraphInstance, UniqueData* uniqueData)
    {
        AZ_UNUSED(animGraphInstance);

        // TODO: Should we only check the most recent transition on the stack or does it count already when any of the currently active transitions is blending to a exit state?
        const AZStd::vector<AnimGraphNode*>& activeStates = uniqueData->GetActiveStates();
        for (const AnimGraphNode* activeState : activeStates)
        {
            if (azrtti_typeid(activeState) == azrtti_typeid<AnimGraphExitNode>())
            {
                uniqueData->mReachedExitState = true;
                return;
            }
        }

        uniqueData->mReachedExitState = false;
    }

    void AnimGraphStateMachine::PostUpdate(AnimGraphInstance* animGraphInstance, float timePassedInSeconds)
    {
        RequestRefDatas(animGraphInstance);
        UniqueData* uniqueData = static_cast<UniqueData*>(FindUniqueNodeData(animGraphInstance));
        AnimGraphRefCountedData* data = uniqueData->GetRefCountedData();

        const AZStd::vector<EMotionFX::AnimGraphNode*>& activeStates = GetActiveStates(animGraphInstance);
        if (!activeStates.empty())
        {
            // Perform post update on all active states (Fill event buffers, spawn events, calculate motion extraction deltas).
            for (AnimGraphNode* activeState : activeStates)
            {
                activeState->PerformPostUpdate(animGraphInstance, timePassedInSeconds);
            }

            if (!IsTransitioning(uniqueData))
            {
                AnimGraphNode* activeState = uniqueData->mCurrentState;
                if (activeState)
                {
                    // Single active state, no active transition.
                    AnimGraphRefCountedData* activeStateData = activeState->FindUniqueNodeData(animGraphInstance)->GetRefCountedData();
                    if (activeStateData)
                    {
                        data->SetEventBuffer(activeStateData->GetEventBuffer());
                        data->SetTrajectoryDelta(activeStateData->GetTrajectoryDelta());
                        data->SetTrajectoryDeltaMirrored(activeStateData->GetTrajectoryDeltaMirrored());
                    }
                }
            }
            else
            {
                const size_t numActiveTransitions = uniqueData->m_activeTransitions.size();

                AnimGraphRefCountedData& prevData = uniqueData->m_prevData;
                prevData.ClearEventBuffer();
                prevData.ZeroTrajectoryDelta();

                // Start by filling our temporary data with the oldest source node's.
                const AnimGraphStateTransition* startTransition = uniqueData->m_activeTransitions[numActiveTransitions - 1];
                const AnimGraphNode* startSourceNode = startTransition->GetSourceNode(animGraphInstance);
                if (startSourceNode)
                {
                    const AnimGraphNodeData* startSourceNodeData = startSourceNode->FindUniqueNodeData(animGraphInstance);
                    if (startSourceNodeData)
                    {
                        const AnimGraphRefCountedData* startSourceData = startSourceNodeData->GetRefCountedData();
                        if (startSourceData)
                        {
                            *data = *startSourceData;

                            // Iterate through the transition stack from the oldest to the newest active transition.
                            for (size_t i = 0; i < numActiveTransitions; ++i)
                            {
                                // Store the current motion extraction delta and events as previous so that we can update the actual one.
                                prevData = *data;

                                AnimGraphStateTransition* activeTransition = uniqueData->m_activeTransitions[numActiveTransitions - i - 1];
                                AnimGraphNode* targetNode = activeTransition->GetTargetNode();
                                const float weight = activeTransition->GetBlendWeight(animGraphInstance);

                                // The prev data acts as source data for the transition.
                                FilterEvents(animGraphInstance, activeTransition->GetEventFilterMode(), &prevData, targetNode, weight, data);

                                // Calculate the motion extraction delta for the transition based on the previously evaluated data, the transition weight and the target node's data.
                                Transform delta;
                                Transform deltaMirrored;
                                activeTransition->ExtractMotion(animGraphInstance, &prevData, &delta, &deltaMirrored);

                                data->SetTrajectoryDelta(delta);
                                data->SetTrajectoryDeltaMirrored(deltaMirrored);
                            }
                        }
                    }
                }
            }
        }
        else
        {
            data->ZeroTrajectoryDelta();
            data->ClearEventBuffer();
        }

        // Decrease data ref counts for all states where we increased it.
        uniqueData->DecreaseDataRefCounts(animGraphInstance);
    }

    void AnimGraphStateMachine::SwitchToState(AnimGraphInstance* animGraphInstance, AnimGraphNode* targetState)
    {
        // get the unique data for this state machine in a given anim graph instance
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueNodeData(this));

        // rewind the target state and reset all outgoing transitions of it
        if (targetState)
        {
            // rewind the new final state and reset conditions of all outgoing transitions
            targetState->Rewind(animGraphInstance);
            ResetOutgoingTransitionConditions(animGraphInstance, targetState);
        }

        // tell the current node to which node we're exiting
        if (uniqueData->mCurrentState)
        {
            uniqueData->mCurrentState->OnStateExit(animGraphInstance, targetState, nullptr);
            uniqueData->mCurrentState->OnStateEnd(animGraphInstance, targetState, nullptr);
        }

        // tell the new current node from which node we're coming
        if (targetState)
        {
            targetState->OnStateEntering(animGraphInstance, uniqueData->mCurrentState, nullptr);
            targetState->OnStateEnter(animGraphInstance, uniqueData->mCurrentState, nullptr);
        }

        // Inform the event manager.
        EventManager& eventManager = GetEventManager();
        eventManager.OnStateExit(animGraphInstance, uniqueData->mCurrentState);
        eventManager.OnStateEntering(animGraphInstance, targetState);
        eventManager.OnStateEnd(animGraphInstance, uniqueData->mCurrentState);
        eventManager.OnStateEnter(animGraphInstance, targetState);

        uniqueData->mPreviousState = uniqueData->mCurrentState;
        uniqueData->mCurrentState = targetState;
        uniqueData->m_activeTransitions.clear();
    }

    // checks if there is a transition from the current to the target node and starts a transition towards it, in case there is no transition between them the target node just gets activated
    void AnimGraphStateMachine::TransitionToState(AnimGraphInstance* animGraphInstance, AnimGraphNode* targetState)
    {
        // get the currently activated state
        AnimGraphNode* currentState = GetCurrentState(animGraphInstance);

        // check if there is a transition between the current and the desired target state
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueNodeData(this));
        AnimGraphStateTransition* transition = FindTransition(animGraphInstance, currentState, targetState);
        if (transition && currentState)
        {
            StartTransition(animGraphInstance, uniqueData, transition, /*calledFromWithinUpdate*/ false);
        }
        else
        {
            SwitchToState(animGraphInstance, targetState);
        }
    }

    bool AnimGraphStateMachine::IsTransitioning(const AnimGraphInstance* animGraphInstance) const
    {
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueNodeData(this));
        return IsTransitioning(uniqueData);
    }

    bool AnimGraphStateMachine::IsLatestActiveTransitionDone(const AnimGraphInstance* animGraphInstance, const UniqueData* uniqueData) const
    {
        const AnimGraphStateTransition* transition = GetLatestActiveTransition(uniqueData);
        if (transition && transition->GetIsDone(animGraphInstance))
        {
            return true;
        }

        return false;
    }

    bool AnimGraphStateMachine::IsTransitioning(const UniqueData* uniqueData) const
    {
        return !uniqueData->m_activeTransitions.empty();
    }

    bool AnimGraphStateMachine::IsTransitionActive(const AnimGraphStateTransition* transition, const AnimGraphInstance* animGraphInstance) const
    {
        const AZStd::vector<AnimGraphStateTransition*>& activeTransitions = GetActiveTransitions(animGraphInstance);
        return AZStd::find(activeTransitions.begin(), activeTransitions.end(), transition) != activeTransitions.end();
    }

    AnimGraphStateTransition* AnimGraphStateMachine::GetLatestActiveTransition(const AnimGraphInstance* animGraphInstance) const
    {
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueNodeData(this));
        return GetLatestActiveTransition(uniqueData);
    }

    const AZStd::vector<AnimGraphStateTransition*>& AnimGraphStateMachine::GetActiveTransitions(const AnimGraphInstance* animGraphInstance) const
    {
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueNodeData(this));
        return uniqueData->m_activeTransitions;
    }

    void AnimGraphStateMachine::AddTransition(AnimGraphStateTransition* transition)
    {
        mTransitions.push_back(transition);
    }

    AnimGraphStateTransition* AnimGraphStateMachine::FindTransition(AnimGraphInstance* animGraphInstance, AnimGraphNode* currentState, AnimGraphNode* targetState) const
    {
        MCORE_UNUSED(animGraphInstance);

        // check if we actually want to transit into another state, in case the final state is nullptr we can return directly
        if (targetState == nullptr)
        {
            return nullptr;
        }

        if (currentState == targetState)
        {
            return nullptr;
        }

        // TODO: optimize by giving each animgraph node also an array of transitions?

        ///////////////////////////////////////////////////////////////////////
        // PASS 1: Check if there is a direct connection to the target state
        ///////////////////////////////////////////////////////////////////////

        // variables that will hold the prioritized transition information
        int32 highestPriority = -1;
        AnimGraphStateTransition* prioritizedTransition = nullptr;

        // first check if there is a ready transition that points directly to the target state
        for (AnimGraphStateTransition* transition : mTransitions)
        {
            // get the current transition and skip it directly if in case it is disabled
            if (transition->GetIsDisabled())
            {
                continue;
            }

            // only do normal state transitions that end in the desired final anim graph node, no wildcard transitions
            if (!transition->GetIsWildcardTransition() && transition->GetSourceNode() == currentState && transition->GetTargetNode() == targetState)
            {
                // compare the priority values and overwrite it in case it is more important
                const int32 transitionPriority = transition->GetPriority();
                if (transitionPriority > highestPriority)
                {
                    highestPriority = transitionPriority;
                    prioritizedTransition = transition;
                }
            }
        }

        // check if we have found a direct transition to the desired final state and return in this case
        if (prioritizedTransition)
        {
            return prioritizedTransition;
        }

        ///////////////////////////////////////////////////////////////////////
        // PASS 2: Check if there is a wild card connection to the target state
        ///////////////////////////////////////////////////////////////////////
        // in case there is no direct and no indirect transition ready, check for wildcard transitions
        // there is a maximum number of one for wild card transitions, so we don't need to check the priority values here
        for (AnimGraphStateTransition* transition : mTransitions)
        {
            // get the current transition and skip it directly if in case it is disabled
            if (transition->GetIsDisabled())
            {
                continue;
            }

            // only handle wildcard transitions for the given target node this time
            if (transition->GetIsWildcardTransition() && transition->GetTargetNode() == targetState)
            {
                return transition;
            }
        }

        // no transition found
        return nullptr;
    }

    AZ::Outcome<size_t> AnimGraphStateMachine::FindTransitionIndexById(AnimGraphConnectionId transitionId) const
    {
        const size_t numTransitions = mTransitions.size();
        for (size_t i = 0; i < numTransitions; ++i)
        {
            if (mTransitions[i]->GetId() == transitionId)
            {
                return AZ::Success(i);
            }
        }

        return AZ::Failure();
    }

    AZ::Outcome<size_t> AnimGraphStateMachine::FindTransitionIndex(const AnimGraphStateTransition* transition) const
    {
        const auto& iterator = AZStd::find(mTransitions.begin(), mTransitions.end(), transition);
        if (iterator != mTransitions.end())
        {
            const size_t index = iterator - mTransitions.begin();
            return AZ::Success(index);
        }

        return AZ::Failure();
    }

    AnimGraphStateTransition* AnimGraphStateMachine::FindTransitionById(AnimGraphConnectionId transitionId) const
    {
        const AZ::Outcome<size_t> transitionIndex = FindTransitionIndexById(transitionId);
        if (transitionIndex.IsSuccess())
        {
            return mTransitions[transitionIndex.GetValue()];
        }

        return nullptr;
    }

    bool AnimGraphStateMachine::CheckIfHasWildcardTransition(AnimGraphNode* state) const
    {
        for (const AnimGraphStateTransition* transition : mTransitions)
        {
            // check if the given transition is a wildcard transition and if the target node is the given one
            if (transition->GetTargetNode() == state && transition->GetIsWildcardTransition())
            {
                return true;
            }
        }

        return false;
    }

    void AnimGraphStateMachine::RemoveTransition(size_t transitionIndex, bool delFromMem)
    {
        if (delFromMem)
        {
            delete mTransitions[transitionIndex];
        }

        mTransitions.erase(mTransitions.begin() + transitionIndex);
    }

    AnimGraphNode* AnimGraphStateMachine::GetEntryState()
    {
        const AnimGraphNodeId entryStateId = GetEntryStateId();
        if (entryStateId.IsValid())
        {
            if (!mEntryState || (mEntryState && mEntryState->GetId() != entryStateId))
            {
                // Sync the entry state based on the id.
                mEntryState = FindChildNodeById(entryStateId);
            }
        }
        else
        {
            // Legacy file format way.
            if (!mEntryState)
            {
                if (mEntryStateNodeNr != MCORE_INVALIDINDEX32 && mEntryStateNodeNr < GetNumChildNodes())
                {
                    mEntryState = GetChildNode(mEntryStateNodeNr);
                }
            }
            // End: Legacy file format way.

            // TODO: Enable this line when deprecating the leagacy file format.
            //mEntryState = nullptr;
        }

        return mEntryState;
    }

    void AnimGraphStateMachine::SetEntryState(AnimGraphNode* entryState)
    {
        mEntryState = entryState;

        if (mEntryState)
        {
            m_entryStateId = mEntryState->GetId();
        }
        else
        {
            m_entryStateId = AnimGraphNodeId::InvalidId;
        }

        // Used for the legacy file format. Get rid of this along with the old file format.
        mEntryStateNodeNr = FindChildNodeIndex(mEntryState);
    }

    AnimGraphNode* AnimGraphStateMachine::GetCurrentState(AnimGraphInstance* animGraphInstance)
    {
        // get the unique data for this state machine in a given anim graph instance
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueNodeData(this));
        if (uniqueData == nullptr)
        {
            uniqueData = aznew UniqueData(this, animGraphInstance);
            uniqueData->mCurrentState = mEntryState;
            animGraphInstance->RegisterUniqueObjectData(uniqueData);
        }
        return uniqueData->mCurrentState;
    }

    bool AnimGraphStateMachine::GetExitStateReached(AnimGraphInstance* animGraphInstance) const
    {
        // get the unique data for this state machine in a given anim graph instance
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueNodeData(this));
        return uniqueData->mReachedExitState;
    }

    void AnimGraphStateMachine::RecursiveOnChangeMotionSet(AnimGraphInstance* animGraphInstance, MotionSet* newMotionSet)
    {
        for (AnimGraphStateTransition* transition : mTransitions)
        {
            transition->OnChangeMotionSet(animGraphInstance, newMotionSet);
        }

        // call the anim graph node
        AnimGraphNode::RecursiveOnChangeMotionSet(animGraphInstance, newMotionSet);
    }

    void AnimGraphStateMachine::OnRemoveNode(AnimGraph* animGraph, AnimGraphNode* nodeToRemove)
    {
        // is the node to remove the entry state?
        if (mEntryState == nodeToRemove)
        {
            SetEntryState(nullptr);
        }

        for (AnimGraphStateTransition* transition : mTransitions)
        {
            transition->OnRemoveNode(animGraph, nodeToRemove);
        }

        for (AnimGraphNode* childNode : mChildNodes)
        {
            childNode->OnRemoveNode(animGraph, nodeToRemove);
        }
    }

    void AnimGraphStateMachine::RecursiveResetUniqueData(AnimGraphInstance* animGraphInstance)
    {
        ResetUniqueData(animGraphInstance);

        for (AnimGraphNode* childNode : mChildNodes)
        {
            childNode->RecursiveResetUniqueData(animGraphInstance);
        }
    }

    void AnimGraphStateMachine::RecursiveOnUpdateUniqueData(AnimGraphInstance* animGraphInstance)
    {
        OnUpdateUniqueData(animGraphInstance);

        for (AnimGraphNode* childNode : mChildNodes)
        {
            childNode->RecursiveOnUpdateUniqueData(animGraphInstance);
        }

        for (AnimGraphStateTransition* transition : mTransitions)
        {
            transition->OnUpdateUniqueData(animGraphInstance);
        }
    }

    void AnimGraphStateMachine::OnUpdateUniqueData(AnimGraphInstance* animGraphInstance)
    {
        // get the unique data for this node, or create it
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueObjectData(this));
        if (uniqueData == nullptr)
        {
            uniqueData = aznew UniqueData(this, animGraphInstance);
            uniqueData->mCurrentState = mEntryState;
            animGraphInstance->RegisterUniqueObjectData(uniqueData);
        }

        OnUpdateTriggerActionsUniqueData(animGraphInstance);

        // check if any of the active states are invalid and reset them if they are
        if (uniqueData->mCurrentState && FindChildNodeIndex(uniqueData->mCurrentState) == MCORE_INVALIDINDEX32)
        {
            uniqueData->mCurrentState = nullptr;
        }
        if (uniqueData->mPreviousState && FindChildNodeIndex(uniqueData->mPreviousState) == MCORE_INVALIDINDEX32)
        {
            uniqueData->mPreviousState = nullptr;
        }

        // Check if the currently active transitions are valid and remove them from the transition stack if not.
        const size_t numActiveTransitions = uniqueData->m_activeTransitions.size();
        for (size_t i = 0; i < numActiveTransitions; ++i)
        {
            const size_t transitionIndex = numActiveTransitions - i - 1;
            AnimGraphStateTransition* transition = uniqueData->m_activeTransitions[transitionIndex];

            const bool isTransitionValid = transition &&
                FindTransitionIndex(transition).IsSuccess() &&
                FindChildNodeIndex(transition->GetSourceNode(animGraphInstance)) != MCORE_INVALIDINDEX32 &&
                FindChildNodeIndex(transition->GetTargetNode()) != MCORE_INVALIDINDEX32;

            if (!isTransitionValid)
            {
                uniqueData->m_activeTransitions.erase(uniqueData->m_activeTransitions.begin() + transitionIndex);
            }
        }

        for (AnimGraphStateTransition* transition : mTransitions)
        {
            transition->OnUpdateUniqueData(animGraphInstance);

            const size_t numConditions = transition->GetNumConditions();
            for (size_t c = 0; c < numConditions; ++c)
            {
                AnimGraphTransitionCondition* condition = transition->GetCondition(c);
                condition->OnUpdateUniqueData(animGraphInstance);
            }

            const size_t numActions = transition->GetTriggerActionSetup().GetNumActions();
            for (size_t a = 0; a < numActions; ++a)
            {
                AnimGraphTriggerAction* action = transition->GetTriggerActionSetup().GetAction(a);
                action->OnUpdateUniqueData(animGraphInstance);
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void AnimGraphStateMachine::UniqueData::Reset()
    {
        m_activeTransitions.clear();
        mCurrentState = nullptr;
        mPreviousState = nullptr;
        mReachedExitState = false;
    }

    const AZStd::vector<AnimGraphNode*>& AnimGraphStateMachine::UniqueData::GetActiveStates()
    {
        m_activeStates.clear();

        if (mCurrentState)
        {
            m_activeStates.emplace_back(mCurrentState);
        }

        // Add target state for all active transitions to the active states.
        for (const AnimGraphStateTransition* transition : m_activeTransitions)
        {
            AnimGraphNode* targetState = transition->GetTargetNode();
            if (AZStd::find(m_activeStates.begin(), m_activeStates.end(), targetState) == m_activeStates.end())
            {
                m_activeStates.emplace_back(targetState);
            }
        }

        return m_activeStates;
    }

    void AnimGraphStateMachine::Rewind(AnimGraphInstance* animGraphInstance)
    {
        // get the unique data for this state machine in a given anim graph instance
        UniqueData* uniqueData = static_cast<UniqueData*>(animGraphInstance->FindUniqueObjectData(this));

        // get the entry state, this function call is needed as we have to update the pointer based on the node number
        AnimGraphNode* entryState = GetEntryState();

        // call the base class rewind
        AnimGraphNode::Rewind(animGraphInstance);
        uniqueData->SetPreSyncTime(uniqueData->GetCurrentPlayTime());

        // rewind the state machine
        if (m_alwaysStartInEntryState && entryState)
        {
            if (uniqueData->mCurrentState)
            {
                uniqueData->mCurrentState->OnStateExit(animGraphInstance, entryState, nullptr);
                uniqueData->mCurrentState->OnStateEnd(animGraphInstance, entryState, nullptr);

                GetEventManager().OnStateExit(animGraphInstance, uniqueData->mCurrentState);
                GetEventManager().OnStateEnd(animGraphInstance, uniqueData->mCurrentState);
            }

            // rewind the entry state and reset conditions of all outgoing transitions
            entryState->Rewind(animGraphInstance);
            ResetOutgoingTransitionConditions(animGraphInstance, entryState);

            mEntryState->OnStateEntering(animGraphInstance, uniqueData->mCurrentState, nullptr);
            mEntryState->OnStateEnter(animGraphInstance, uniqueData->mCurrentState, nullptr);
            GetEventManager().OnStateEntering(animGraphInstance, entryState);
            GetEventManager().OnStateEnter(animGraphInstance, entryState);

            // reset the the unique data of the state machine and overwrite the current state as that is not nullptr but the entry state
            uniqueData->Reset();
            uniqueData->mCurrentState = entryState;
        }
    }

    void AnimGraphStateMachine::RecursiveResetFlags(AnimGraphInstance* animGraphInstance, uint32 flagsToDisable)
    {
        // clear the output for all child nodes, just to make sure
        for (const AnimGraphNode* childNode : mChildNodes)
        {
            animGraphInstance->DisableObjectFlags(childNode->GetObjectIndex(), flagsToDisable);
        }

        // Reset flags for this state machine.
        animGraphInstance->DisableObjectFlags(mObjectIndex, flagsToDisable);

        // Reset flags recursively for all active states within this state machine.
        const AZStd::vector<EMotionFX::AnimGraphNode*>& activeStates = GetActiveStates(animGraphInstance);
        for (AnimGraphNode* activeState : activeStates)
        {
            activeState->RecursiveResetFlags(animGraphInstance, flagsToDisable);
        }
    }

    bool AnimGraphStateMachine::GetIsDeletable() const
    {
        // Only the root state machine is not deletable.
        return GetParentNode() != nullptr;
    }

    void AnimGraphStateMachine::ResetOutgoingTransitionConditions(AnimGraphInstance* animGraphInstance, AnimGraphNode* state)
    {
        for (AnimGraphStateTransition* transition : mTransitions)
        {
            // get the transition, check if it is a possible outgoing connection for our given state and reset it in this case
            if (transition->GetIsWildcardTransition() ||
                (!transition->GetIsWildcardTransition() && transition->GetSourceNode() == state))
            {
                transition->ResetConditions(animGraphInstance);
            }
        }
    }

    uint32 AnimGraphStateMachine::CalcNumIncomingTransitions(AnimGraphNode* state) const
    {
        uint32 result = 0;
        for (const AnimGraphStateTransition* transition : mTransitions)
        {
            if (transition->GetTargetNode() == state)
            {
                result++;
            }
        }
        return result;
    }

    uint32 AnimGraphStateMachine::CalcNumWildcardTransitions(AnimGraphNode* state) const
    {
        uint32 result = 0;
        for (const AnimGraphStateTransition* transition : mTransitions)
        {
            if (transition->GetIsWildcardTransition() && transition->GetTargetNode() == state)
            {
                result++;
            }
        }
        return result;
    }

    AZ::u32 AnimGraphStateMachine::GetMaxNumPasses()
    {
        return s_maxNumPasses;
    }

    AnimGraphStateMachine* AnimGraphStateMachine::GetGrandParentStateMachine(const AnimGraphNode* state)
    {
        AnimGraphStateMachine* parentStateMachine = azdynamic_cast<AnimGraphStateMachine*>(state->GetParentNode());
        if (parentStateMachine)
        {
            return azdynamic_cast<AnimGraphStateMachine*>(parentStateMachine->GetParentNode());
        }

        return nullptr;
    }

    uint32 AnimGraphStateMachine::CalcNumOutgoingTransitions(AnimGraphNode* state) const
    {
        uint32 result = 0;
        for (const AnimGraphStateTransition* transition : mTransitions)
        {
            if (!transition->GetIsWildcardTransition() && transition->GetSourceNode() == state)
            {
                result++;
            }
        }
        return result;
    }

    void AnimGraphStateMachine::RecursiveCollectObjects(MCore::Array<AnimGraphObject*>& outObjects) const
    {
        for (const AnimGraphStateTransition* transition : mTransitions)
        {
            transition->RecursiveCollectObjects(outObjects); // this will automatically add all transition conditions as well
        }
        // add the node and its children
        AnimGraphNode::RecursiveCollectObjects(outObjects);
    }

    void AnimGraphStateMachine::RecursiveCollectObjectsOfType(const AZ::TypeId& objectType, AZStd::vector<AnimGraphObject*>& outObjects) const
    {
        AnimGraphNode::RecursiveCollectObjectsOfType(objectType, outObjects);

        const size_t numTransitions = GetNumTransitions();
        for (size_t i = 0; i < numTransitions; ++i)
        {
            const AnimGraphStateTransition* transition = GetTransition(i);
            if (azrtti_istypeof(objectType, transition))
            {
                outObjects.emplace_back(const_cast<AnimGraphStateTransition*>(transition));
            }

            // get the number of conditions and iterate through them
            const size_t numConditions = transition->GetNumConditions();
            for (size_t j = 0; j < numConditions; ++j)
            {
                // check if the given condition is of the given type and add it to the output array in this case
                AnimGraphTransitionCondition* condition = transition->GetCondition(j);
                if (azrtti_istypeof(objectType, condition))
                {
                    outObjects.emplace_back(condition);
                }
            }
        }
    }

    const AZStd::vector<AnimGraphNode*>& AnimGraphStateMachine::GetActiveStates(AnimGraphInstance* animGraphInstance) const
    {
        UniqueData* uniqueData = static_cast<UniqueData*>(FindUniqueNodeData(animGraphInstance));
        return uniqueData->GetActiveStates();
    }

    void AnimGraphStateMachine::TopDownUpdate(AnimGraphInstance* animGraphInstance, float timePassedInSeconds)
    {
        UniqueData* uniqueData = static_cast<UniqueData*>(FindUniqueNodeData(animGraphInstance));

        if (!IsTransitioning(uniqueData))
        {
            AnimGraphNode* activeState = uniqueData->mCurrentState;
            if (activeState)
            {
                // Single active state, no active transition.
                HierarchicalSyncInputNode(animGraphInstance, activeState, uniqueData);
                activeState->PerformTopDownUpdate(animGraphInstance, timePassedInSeconds);
            }
        }
        else
        {
            // Iterate through the transition stack from the oldest to the newest active transition.
            const size_t numActiveTransitions = uniqueData->m_activeTransitions.size();
            for (size_t i = 0; i < numActiveTransitions; ++i)
            {
                AnimGraphStateTransition* activeTransition = uniqueData->m_activeTransitions[numActiveTransitions - i - 1];
                AnimGraphNode* sourceNode = activeTransition->GetSourceNode(animGraphInstance);
                AnimGraphNode* targetNode = activeTransition->GetTargetNode();
                const float weight = activeTransition->GetBlendWeight(animGraphInstance);

                if (sourceNode)
                {
                    // mark this node recursively as synced
                    const ESyncMode syncMode = activeTransition->GetSyncMode();
                    if (syncMode != SYNCMODE_DISABLED)
                    {
                        if (animGraphInstance->GetIsObjectFlagEnabled(mObjectIndex, AnimGraphInstance::OBJECTFLAGS_SYNCED) == false)
                        {
                            sourceNode->RecursiveSetUniqueDataFlag(animGraphInstance, AnimGraphInstance::OBJECTFLAGS_SYNCED, true);
                            animGraphInstance->SetObjectFlags(sourceNode->GetObjectIndex(), AnimGraphInstance::OBJECTFLAGS_IS_SYNCMASTER, true);
                            targetNode->RecursiveSetUniqueDataFlag(animGraphInstance, AnimGraphInstance::OBJECTFLAGS_SYNCED, true);
                        }

                        HierarchicalSyncInputNode(animGraphInstance, sourceNode, uniqueData);

                        // Adjust the playspeed of the source node to the precalculated transition playspeed.
                        sourceNode->SetPlaySpeed(animGraphInstance, uniqueData->GetPlaySpeed());
                        targetNode->AutoSync(animGraphInstance, sourceNode, weight, syncMode, false);
                    }
                    else
                    {
                        // child node speed propagation in case the transition is set to not syncing the states
                        sourceNode->SetPlaySpeed(animGraphInstance, uniqueData->GetPlaySpeed());
                        targetNode->SetPlaySpeed(animGraphInstance, uniqueData->GetPlaySpeed());
                    }

                    sourceNode->FindUniqueNodeData(animGraphInstance)->SetGlobalWeight(uniqueData->GetGlobalWeight() * (1.0f - weight));
                    sourceNode->FindUniqueNodeData(animGraphInstance)->SetLocalWeight(1.0f - weight);
                    sourceNode->PerformTopDownUpdate(animGraphInstance, timePassedInSeconds);

                    // update both top-down
                    targetNode->FindUniqueNodeData(animGraphInstance)->SetGlobalWeight(uniqueData->GetGlobalWeight() * weight);
                    targetNode->FindUniqueNodeData(animGraphInstance)->SetLocalWeight(weight);
                    targetNode->PerformTopDownUpdate(animGraphInstance, timePassedInSeconds);
                }
            }
        }
    }

    AnimGraphStateMachine::UniqueData::UniqueData(AnimGraphNode* node, AnimGraphInstance* animGraphInstance)
        : AnimGraphNodeData(node, animGraphInstance)
        , NodeDataAutoRefCountMixin()
    {
        Reset();
    }

    uint32 AnimGraphStateMachine::UniqueData::Save(uint8* outputBuffer) const
    {
        uint8* destBuffer = outputBuffer;
        uint32 resultSize = 0;

        uint32 chunkSize = AnimGraphNodeData::Save(destBuffer);
        if (destBuffer)
        {
            destBuffer += chunkSize;
        }
        resultSize += chunkSize;

        SaveVectorOfObjects<AnimGraphStateTransition*>(m_activeTransitions, &destBuffer, resultSize);
        SaveChunk((uint8*)&mCurrentState, sizeof(AnimGraphNode*), &destBuffer, resultSize);
        SaveChunk((uint8*)&mPreviousState, sizeof(AnimGraphNode*), &destBuffer, resultSize);

        return resultSize;
    }

    uint32 AnimGraphStateMachine::UniqueData::Load(const uint8* dataBuffer)
    {
        uint8* sourceBuffer = (uint8*)dataBuffer;
        uint32 resultSize = 0;

        uint32 chunkSize = AnimGraphNodeData::Load(sourceBuffer);
        sourceBuffer += chunkSize;
        resultSize += chunkSize;

        LoadVectorOfObjects<AnimGraphStateTransition*>(m_activeTransitions, &sourceBuffer, resultSize);
        LoadChunk((uint8*)&mCurrentState, sizeof(AnimGraphNode*), &sourceBuffer, resultSize);
        LoadChunk((uint8*)&mPreviousState, sizeof(AnimGraphNode*), &sourceBuffer, resultSize);

        return resultSize;
    }

    void AnimGraphStateMachine::RecursiveSetUniqueDataFlag(AnimGraphInstance* animGraphInstance, uint32 flag, bool enabled)
    {
        // Set flag for this state machine.
        animGraphInstance->SetObjectFlags(mObjectIndex, flag, enabled);

        // Set flag recursively for all active states within this state machine.
        const AZStd::vector<EMotionFX::AnimGraphNode*>& activeStates = GetActiveStates(animGraphInstance);
        for (AnimGraphNode* activeState : activeStates)
        {
            activeState->RecursiveSetUniqueDataFlag(animGraphInstance, flag, enabled);
        }
    }

    void AnimGraphStateMachine::RecursiveCollectActiveNodes(AnimGraphInstance* animGraphInstance, MCore::Array<AnimGraphNode*>* outNodes, const AZ::TypeId& nodeType) const
    {
        // check and add this node
        if (azrtti_typeid(this) == nodeType || nodeType.IsNull())
        {
            if (animGraphInstance->GetIsOutputReady(mObjectIndex)) // if we processed this node
            {
                outNodes->Add(const_cast<AnimGraphStateMachine*>(this));
            }
        }

        // Recurse into all active states within this state machine.
        const AZStd::vector<EMotionFX::AnimGraphNode*>& activeStates = GetActiveStates(animGraphInstance);
        for (const AnimGraphNode* activeState : activeStates)
        {
            activeState->RecursiveCollectActiveNodes(animGraphInstance, outNodes, nodeType);
        }
    }

    void AnimGraphStateMachine::RecursiveCollectActiveNetTimeSyncNodes(AnimGraphInstance* animGraphInstance, AZStd::vector<AnimGraphNode*>* outNodes) const
    {
        const AZStd::vector<EMotionFX::AnimGraphNode*>& activeStates = GetActiveStates(animGraphInstance);
        for (const AnimGraphNode* activeState : activeStates)
        {
            activeState->RecursiveCollectActiveNetTimeSyncNodes(animGraphInstance, outNodes);
        }
    }

    void AnimGraphStateMachine::ReserveTransitions(size_t numTransitions)
    {
        mTransitions.reserve(numTransitions);
    }

    void AnimGraphStateMachine::SetEntryStateId(AnimGraphNodeId entryStateId)
    {
        m_entryStateId = entryStateId;
    }

    void AnimGraphStateMachine::SetAlwaysStartInEntryState(bool alwaysStartInEntryState)
    {
        m_alwaysStartInEntryState = alwaysStartInEntryState;
    }

    void AnimGraphStateMachine::LogTransitionStack(const char* stateDescription, AnimGraphInstance* animGraphInstance, const UniqueData* uniqueData) const
    {
        AZ_Printf("EMotionFX", "=== Transition Stack (%s) ===", stateDescription);
        const size_t numActiveTransitions = uniqueData->m_activeTransitions.size();
        for (size_t i = 0; i < numActiveTransitions; ++i)
        {
            const AnimGraphStateTransition* transition = uniqueData->m_activeTransitions[i];
            const AnimGraphNode* sourceNode = transition->GetSourceNode(animGraphInstance);
            const AnimGraphNode* targetNode = transition->GetTargetNode();
            AZ_Printf("EMotionFX", "    #%d (%s->%s): Weight=%.2f", i, sourceNode->GetName(), targetNode->GetName(), transition->GetBlendWeight(animGraphInstance));
        }
    }

    void AnimGraphStateMachine::PushTransitionStack(UniqueData* uniqueData, AnimGraphStateTransition* transition)
    {
        uniqueData->m_activeTransitions.emplace(uniqueData->m_activeTransitions.begin(), transition);
    }

    AnimGraphStateTransition* AnimGraphStateMachine::GetLatestActiveTransition(const UniqueData* uniqueData) const
    {
        if (!uniqueData->m_activeTransitions.empty())
        {
            return uniqueData->m_activeTransitions[0];
        }

        return nullptr;
    }

    void AnimGraphStateMachine::Reflect(AZ::ReflectContext* context)
    {
        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext)
        {
            return;
        }

        serializeContext->Class<AnimGraphStateMachine, AnimGraphNode>()
            ->Version(1)
            ->Field("entryStateId", &AnimGraphStateMachine::m_entryStateId)
            ->Field("transitions", &AnimGraphStateMachine::mTransitions)
            ->Field("alwaysStartInEntryState", &AnimGraphStateMachine::m_alwaysStartInEntryState);

        AZ::EditContext* editContext = serializeContext->GetEditContext();
        if (!editContext)
        {
            return;
        }

        editContext->Class<AnimGraphStateMachine>("State Machine", "State machine attributes")
            ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
            ->Attribute(AZ::Edit::Attributes::AutoExpand, "")
            ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
            ->DataElement(AZ::Edit::UIHandlers::Default, &AnimGraphStateMachine::m_alwaysStartInEntryState, "Always Start In Entry State", "Set state machine back to entry state when it gets activated?");
    }
} // namespace EMotionFX
