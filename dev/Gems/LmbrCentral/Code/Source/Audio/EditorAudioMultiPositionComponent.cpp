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

#include "LmbrCentral_precompiled.h"
#include "EditorAudioMultiPositionComponent.h"

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace LmbrCentral
{
    //=========================================================================
    void EditorAudioMultiPositionComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorAudioMultiPositionComponent, EditorComponentBase>()
                ->Version(0)
                ->Field("Entity Refs", &EditorAudioMultiPositionComponent::m_entityRefs)
                ->Field("Behavior Type", &EditorAudioMultiPositionComponent::m_behaviorType)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Enum<Audio::MultiPositionBehaviorType>("Behavior Type", "How multiple position audio behaves")
                    ->Value("Separate", Audio::MultiPositionBehaviorType::Separate)
                    ->Value("Blended", Audio::MultiPositionBehaviorType::Blended)
                    ;

                editContext->Class<EditorAudioMultiPositionComponent>("Multi-Position Audio", "Provides the ability to apply multiple positions to a sound via entity references")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Audio")
                        ->Attribute(AZ::Edit::Attributes::Icon, "Editor/Icons/Components/AudioMultiPosition.png")
                        ->Attribute(AZ::Edit::Attributes::ViewportIcon, "Editor/Icons/Components/Viewport/AudioMultiPosition.png")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC("Game", 0x232b318c))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                        // Followup: Need Help URL
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorAudioMultiPositionComponent::m_entityRefs, "Entity References", "The entities from which positions will be obtained for multi-position audio")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &EditorAudioMultiPositionComponent::m_behaviorType, "Behavior Type", "Determines how multi-postion sounds are treated, Separate or Blended")
                    ;
            }
        }
    }

    //=========================================================================
    void EditorAudioMultiPositionComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        gameEntity->CreateComponent<AudioMultiPositionComponent>(m_entityRefs, m_behaviorType);
    }

} // namespace LmbrCentral
